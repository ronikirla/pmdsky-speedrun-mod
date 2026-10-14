#include "uplink.h"
#include "dspico_card.h"
#include "uplink_sampler.h"

#include <string.h>

// Main-thread stack high-water (bytes used), maintained by threads.c's
// paint/scan instrumentation. Spelled `unsigned int` (not uint32_t) to
// match the pmdsky.h typedef used at the definition site exactly.
extern volatile unsigned int mod_main_stack_used;

static bool s_inited = false;
static bool s_sampling = false;
static bool s_config_active = false;

//--------------------------------------------------------------------+
// Firmware protocol: the TinyUSB device stack runs in the DSpico firmware
// (usb_cdc_bridge.c). The NDS never calls into TinyUSB; it ships sample
// frames over WRITE_DATA (0xE9) and reads host commands over READ_DATA
// (0xEA). The firmware is a transparent byte pipe (docs/uplink-protocol.md).
//--------------------------------------------------------------------+

// 0xEA endpoint field selects the returned 512-byte block content:
#define UPLINK_LOCAL_RX_EP 1
// 0xE9 endpoint field (ignored by the firmware, kept meaningful for logs)
#define UPLINK_LOCAL_TX_EP 0x82

// Poll cadence: host-command refresh every ~15 frames (~250 ms at 60 Hz).
// This replaces the v1 status poll one-for-one: same 0xEA transaction
// rate, same MCCNT1 settings (LATENCY2(63)), so the card-bus read-path
// exposure stays identical to the validated configuration.
#define UPLINK_LOCAL_POLL_EVERY 15

// Config watchdog: ~2 s (120 frames) without any host byte aborts a
// stalled configuration (host gone / host stalled). Matches the host's
// 2 s ACK timeout scale.
#define UPLINK_CONFIG_WATCHDOG_FRAMES 120

// One shared 512-byte scratch for all card data phases (frame chunks and
// host-command reads). Per-poll sequencing keeps the uses disjoint:
// frame chunks are shipped first, the RX read happens afterwards.
static uint8_t s_block_buf[512];

// Host command stream accumulator. Longest command: CONFIG_ENTRY with 4
// steps = 20 bytes.
static uint8_t s_cmd[24];
static uint8_t s_cmd_len = 0;

static uint32_t s_poll_cnt = 0;
static uint16_t s_config_watchdog = 0;
// Entries applied during the current RX read (drives the OK ENTRIES reply)
static uint16_t s_entries_this_feed = 0;
// Diagnostic: total nonzero host bytes ever parsed (PING reply reports it;
// distinguishes "the mod never saw the batch" from "saw it but did not
// process it" when an upload fails).
static uint32_t s_rx_bytes = 0;
// Set once the boot greeting has been sent (see UplinkInit).
static bool s_greeted = false;

static void uplink_cmd_clear(void);

// Minimal decimal formatting (no libc on the NDS)
static uint8_t uplink_utoa(uint32_t v, uint8_t *out)
{
  uint8_t tmp[12];
  uint8_t n = 0;
  if (v == 0)
  {
    out[0] = '0';
    return 1;
  }
  while (v > 0)
  {
    tmp[n++] = (uint8_t)('0' + v % 10);
    v /= 10;
  }
  for (uint8_t i = 0; i < n; i++)
  {
    out[i] = tmp[n - 1 - i];
  }
  return n;
}

//--------------------------------------------------------------------+
// Card transactions (locked)
//--------------------------------------------------------------------+

// Locked single card transaction, no data phase.
// Returns false when the transaction was skipped (card lock unavailable or
// card bus dead) so callers can retry later instead of running unlocked.
static bool uplink_local_send_cmd(uint64_t command)
{
  uint16_t lock = dspico_card_lock_wait_persistent();
  if (lock == DSPICO_LOCK_ID_INVALID)
  {
    uplink_card_lock_skips++;
    return false;
  }
  dspico_card_set_cmd(command);
  dspico_card_start_xfer(DSPICO_USB_DEFAULT_COMMAND_SETTINGS, false);
  dspico_card_wait_busy();
  dspico_card_lock_release_persistent(lock);
  return true;
}

// Ship up to 512 valid bytes to the firmware CDC TX ring (the rest of the
// 512-byte phase is zero-padded by dspico_card_cpu_write; the firmware
// forwards only the declared valid byte count).
// Returns false when the transaction was skipped (card lock unavailable or
// card bus dead); the payload is then NOT shipped.
static bool uplink_local_write_block(const uint8_t *src, uint32_t valid)
{
  if (valid > 512)
  {
    valid = 512;
  }
  uint16_t lock = dspico_card_lock_wait_persistent();
  if (lock == DSPICO_LOCK_ID_INVALID)
  {
    uplink_card_lock_skips++;
    return false;
  }
  dspico_card_set_cmd(DSPICO_CMD_USB_WRITE_DATA(0, UPLINK_LOCAL_TX_EP, 1, valid));
  dspico_card_start_xfer(DSPICO_USB_WRITE_DATA_SETTINGS, false);
  dspico_card_cpu_write(src, 512 / 4, valid);
  dspico_card_lock_release_persistent(lock);
  return true;
}

// Read a 512-byte block of host bytes from the firmware RX ring into dst
// (zero-filled by the firmware when the ring is empty). Returns false
// when the transaction was skipped (card lock unavailable or card bus
// dead); the contents of dst are then undefined.
static bool uplink_local_read_rx(uint8_t *dst)
{
  uint16_t lock = dspico_card_lock_wait_persistent();
  if (lock == DSPICO_LOCK_ID_INVALID)
  {
    uplink_card_lock_skips++;
    return false;
  }
  dspico_card_set_cmd(DSPICO_CMD_USB_READ_DATA(0, UPLINK_LOCAL_RX_EP));
  dspico_card_start_xfer(DSPICO_USB_READ_DATA_SETTINGS, false);
  dspico_card_cpu_read(dst, 512 / 4);
  dspico_card_lock_release_persistent(lock);
  return !dspico_card_is_dead();
}

//--------------------------------------------------------------------+
// Control replies (ASCII lines, shipped immediately; best-effort)
//--------------------------------------------------------------------+

// Returns false when the transaction was skipped (card lock unavailable
// or card bus dead); the reply is then NOT shipped and the caller may
// retry later (the boot greeting does).
static bool uplink_reply_send(const uint8_t *data, uint32_t len)
{
  return uplink_local_write_block(data, len);
}

// Small reply builder: text pieces and u32 values, CRLF-terminated.
// 96 bytes: the PING reply carries five counters plus cfg=a/b and must
// not truncate its diagnostic tail once the counters grow.
struct uplink_reply
{
  uint8_t buf[96];
  uint8_t len;
};

static void reply_add_str(struct uplink_reply *r, const char *s)
{
  while (*s && r->len < sizeof(r->buf) - 2)
  {
    r->buf[r->len++] = (uint8_t)*s++;
  }
}

static void reply_add_u32(struct uplink_reply *r, uint32_t v)
{
  uint8_t tmp[12];
  uint8_t n = uplink_utoa(v, tmp);
  for (uint8_t i = 0; i < n && r->len < sizeof(r->buf) - 2; i++)
  {
    r->buf[r->len++] = tmp[i];
  }
}

static bool reply_send(struct uplink_reply *r)
{
  if (r->len <= sizeof(r->buf) - 2)
  {
    r->buf[r->len++] = '\r';
    r->buf[r->len++] = '\n';
  }
  return uplink_reply_send(r->buf, r->len);
}

static void uplink_send_err(const char *reason)
{
  struct uplink_reply r = {.len = 0};
  reply_add_str(&r, "ERR ");
  reply_add_str(&r, reason);
  reply_send(&r);
}

// The UPLINK status line (PING reply format): seq/sent/drop counters,
// main-thread stack usage, rx byte counter and cfg=<applied>/<declared>.
// Sent as a PING reply AND once unsolicited at boot (the greeting) so a
// still-connected reader learns that the table was wiped and re-uploads
// immediately instead of waiting out its silence watchdog. Returns false
// when the write was skipped (the greeting caller then retries).
static bool uplink_send_status(void)
{
  struct uplink_reply r = {.len = 0};
  reply_add_str(&r, "UPLINK seq=");
  reply_add_u32(&r, uplink_sampler_get_seq());
  reply_add_str(&r, " sent=");
  reply_add_u32(&r, uplink_frames_sent);
  reply_add_str(&r, " drop=");
  reply_add_u32(&r, uplink_frames_dropped);
  reply_add_str(&r, " stack=");
  reply_add_u32(&r, mod_main_stack_used);
  reply_add_str(&r, " rx=");
  reply_add_u32(&r, s_rx_bytes);
  reply_add_str(&r, " cfg=");
  reply_add_u32(&r, uplink_sampler_staging_count());
  reply_add_str(&r, "/");
  reply_add_u32(&r, uplink_sampler_declared_count());
  return reply_send(&r);
}

//--------------------------------------------------------------------+
// Config state machine
//--------------------------------------------------------------------+

static void uplink_config_begin(uint8_t ver, uint16_t count)
{
  enum uplink_cfg_err e = uplink_sampler_configure_begin(ver, count);
  if (e != UPLINK_CFG_OK)
  {
    uplink_send_err(e == UPLINK_CFG_ERR_VER ? "VER" : "COUNT");
    return;
  }
  s_config_active = true;
  s_config_watchdog = 0;
  s_entries_this_feed = 0;

  struct uplink_reply r = {.len = 0};
  reply_add_str(&r, "OK CONFIG count=");
  reply_add_u32(&r, uplink_sampler_declared_count());
  reply_add_str(&r, " max=");
  reply_add_u32(&r, UPLINK_MAX_SAMPLES);
  reply_send(&r);
}

static void uplink_config_commit(void)
{
  // Callers guard with s_config_active; stale COMMITs are consumed
  // silently by the command handler.
  if (uplink_sampler_staging_count() != uplink_sampler_declared_count())
  {
    // Integrity check: the host must send exactly the declared entries.
    uplink_sampler_configure_abort();
    s_config_active = false;
    uplink_send_err("COUNT");
    return;
  }
  uplink_sampler_configure_commit();
  s_config_active = false;

  // Reply BEFORE starting to stream: once frames flow, every reply write
  // competes with ~60 frame transactions/s for the card lock, and this
  // one-shot reply is then likely to be skipped — the reader would time
  // out and discard a perfectly good, fully applied table (seen on
  // device: cfg=14/14 while the reader kept restarting). The reader also
  // re-sends COMMIT on timeout and the handler re-acks idempotently, so
  // a skipped reply here still recovers.
  struct uplink_reply r = {.len = 0};
  reply_add_str(&r, "OK STREAMING epoch=");
  reply_add_u32(&r, uplink_sampler_epoch());
  reply_send(&r);

  s_sampling = true; // auto-start on commit
}

static void uplink_config_abort_cmd(void)
{
  uplink_sampler_configure_abort();
  s_config_active = false;

  struct uplink_reply r = {.len = 0};
  reply_add_str(&r, "OK ABORTED");
  reply_send(&r);
}

// Silent abort (config watchdog fired: host gone or stalled)
static void uplink_config_abort_silent(void)
{
  uplink_sampler_configure_abort();
  s_config_active = false;
  s_config_watchdog = 0;
}

//--------------------------------------------------------------------+
// Host command handling
//--------------------------------------------------------------------+

// Returns false when the accumulator was cleared (config aborted) and the
// parse loop must stop.
static bool uplink_handle_host_command(uint8_t const *buf, uint16_t len)
{
  switch (buf[0])
  {
  case UPLINK_CMD_START:
    s_sampling = true;
    break;
  case UPLINK_CMD_STOP:
    s_sampling = false;
    uplink_sampler_frame_abort();
    break;
  case UPLINK_CMD_PING:
    uplink_send_status();
    break;
  case UPLINK_CMD_CONFIG_BEGIN:
    // (Re)configuration is allowed any time; it resets the staging area.
    uplink_config_begin(buf[1], (uint16_t)(buf[2] | ((uint16_t)buf[3] << 8)));
    break;
  case UPLINK_CMD_CONFIG_ENTRY:
  {
    if (!s_config_active)
    {
      // Stale bytes from an aborted/timed-out config: consume the
      // command silently (an unsolicited ERR here would make the reader
      // fail its NEXT upload attempt, and clearing the accumulator could
      // eat a valid CONFIG_BEGIN later in the same stream).
      break;
    }
    enum uplink_cfg_err e = uplink_sampler_configure_entry(buf, len);
    if (e != UPLINK_CFG_OK)
    {
      // Any entry error discards the whole config; the host restarts.
      uplink_sampler_configure_abort();
      s_config_active = false;
      // enum order: OK, VER, COUNT, FULL, IDX, WIDTH, STEPS, OFFSET, BASE,
      // STATE — the table skips OK.
      static const char *const reasons[] = {"VER", "COUNT", "FULL", "IDX",
                                            "WIDTH", "STEPS", "OFFSET", "BASE",
                                            "STATE"};
      uplink_send_err(reasons[e - 1]);
      uplink_cmd_clear();
      return false;
    }
    s_entries_this_feed++;
    break;
  }
  case UPLINK_CMD_CONFIG_COMMIT:
    if (s_config_active)
    {
      uplink_config_commit();
    }
    else if (uplink_sampler_declared_count() > 0 &&
             uplink_sampler_entry_count() == uplink_sampler_declared_count())
    {
      // Idempotent re-ack: the reader re-sends COMMIT when its OK
      // STREAMING reply was lost (e.g. skipped because the card lock was
      // busy with frame writes). The table is already complete — just
      // re-reply, no state change, no epoch bump.
      struct uplink_reply r = {.len = 0};
      reply_add_str(&r, "OK STREAMING epoch=");
      reply_add_u32(&r, uplink_sampler_epoch());
      reply_send(&r);
    } // otherwise stale: consumed silently
    break;
  case UPLINK_CMD_CONFIG_ABORT:
    if (s_config_active)
    {
      uplink_config_abort_cmd();
    } // otherwise stale: consumed silently
    break;
  default:
    break;
  }
  return true;
}

//--------------------------------------------------------------------+
// Host command stream parsing
//--------------------------------------------------------------------+

// Host command stream parsing. Commands arrive as a byte stream over the
// firmware RX ring (two commands may share one 512-byte block, and a
// command may be split across blocks), so bytes are accumulated and
// complete commands are emitted as they become available. Zero padding
// after the last command in a block is skipped as unknown bytes.
static void uplink_cmd_drop_first(void)
{
  for (uint8_t i = 0; i + 1 < s_cmd_len; i++)
  {
    s_cmd[i] = s_cmd[i + 1];
  }
  s_cmd_len--;
}

static void uplink_cmd_parse(void)
{
  for (;;)
  {
    if (s_cmd_len == 0)
    {
      return;
    }
    uint16_t need = 0;
    switch (s_cmd[0])
    {
    case UPLINK_CMD_START:
    case UPLINK_CMD_STOP:
    case UPLINK_CMD_PING:
    case UPLINK_CMD_CONFIG_COMMIT:
    case UPLINK_CMD_CONFIG_ABORT:
      need = 1;
      break;
    case UPLINK_CMD_CONFIG_BEGIN:
      need = 5;
      break;
    case UPLINK_CMD_CONFIG_ENTRY:
      if (s_cmd_len < 8)
      {
        return; // meta byte (offset 7) not yet available
      }
      {
        uint8_t nsteps = (s_cmd[7] >> 2) & 7u;
        if (nsteps > UPLINK_MAX_STEPS)
        {
          if (s_config_active)
          {
            // Malformed entry during a live config: reject and resync (the
            // host restarts the config after the ERR reply).
            uplink_send_err("STEPS");
            uplink_sampler_configure_abort();
            s_config_active = false;
            uplink_cmd_clear();
            return;
          }
          // Stale/garbage bytes with no config in progress: resync one byte
          // at a time (same as unknown bytes) instead of sending an
          // unsolicited ERR that would fail the host's NEXT upload attempt.
          uplink_cmd_drop_first();
          continue;
        }
        need = (uint16_t)(8 + 3 * nsteps);
      }
      break;
    default:
      uplink_cmd_drop_first(); // unknown byte (incl. zero padding); resync
      continue;
    }
    if (s_cmd_len < need)
    {
      return; // incomplete command; wait for more bytes
    }
    if (!uplink_handle_host_command(s_cmd, need))
    {
      return; // accumulator cleared by an abort
    }
    for (uint16_t i = 0; i + need < s_cmd_len; i++)
    {
      s_cmd[i] = s_cmd[i + need];
    }
    s_cmd_len = (uint8_t)(s_cmd_len - need);
  }
}

// Feed host bytes into the command stream parser. Bytes are appended one
// at a time and the parser runs after each byte, so a command executes
// the moment its bytes complete and the firmware's trailing zero padding
// hits an empty accumulator (accumulating the whole 512-byte block first
// let ~500 padding bytes overflow the accumulator and evict the real
// commands — the reader then never saw a CONFIG ACK at all).
// Zero bytes are padding ONLY while no command is being accumulated;
// command payloads contain legitimate zero bytes (reserved fields,
// offset 0) and must be kept. Returns true when the block contained any
// nonzero byte (host activity; resets the config watchdog).
static bool uplink_cmd_feed(const uint8_t *data, uint32_t len)
{
  bool any = false;
  s_entries_this_feed = 0;
  for (uint32_t i = 0; i < len; i++)
  {
    uint8_t b = data[i];
    if (b == 0 && s_cmd_len == 0)
    {
      continue; // zero padding / empty ring between commands
    }
    if (b != 0)
    {
      any = true;
      s_rx_bytes++;
    }
    if (s_cmd_len >= sizeof(s_cmd))
    {
      uplink_cmd_drop_first(); // overflow; drop the oldest byte
    }
    s_cmd[s_cmd_len++] = b;
    uplink_cmd_parse(); // executes complete commands immediately
  }
  if (s_entries_this_feed > 0 && s_config_active)
  {
    struct uplink_reply r = {.len = 0};
    reply_add_str(&r, "OK ENTRIES n=");
    reply_add_u32(&r, uplink_sampler_staging_count());
    reply_send(&r);
  }
  return any;
}

// Clear the accumulator (used after config aborts to skip the rest of a
// meaningless batch).
static void uplink_cmd_clear(void)
{
  s_cmd_len = 0;
}

//--------------------------------------------------------------------+
// Frame streaming
//--------------------------------------------------------------------+

// Stream one frame: hold the card lock across the frame's 1-3 whole
// 512-byte transactions (less lock churn than per-transaction), aborting
// cleanly when the lock is unavailable or the card dies mid-frame.
static void uplink_stream_frame(void)
{
  uint16_t lock = dspico_card_lock_wait_persistent();
  if (lock == DSPICO_LOCK_ID_INVALID)
  {
    uplink_card_lock_skips++;
    uplink_frames_dropped++;
    return;
  }
  if (!uplink_sampler_frame_start())
  {
    dspico_card_lock_release_persistent(lock);
    return;
  }
  bool ok = true;
  for (;;)
  {
    uint32_t n = uplink_sampler_frame_fill(s_block_buf, sizeof(s_block_buf));
    if (n == 0)
    {
      break; // frame complete
    }
    dspico_card_set_cmd(DSPICO_CMD_USB_WRITE_DATA(0, UPLINK_LOCAL_TX_EP, 1, (uint16_t)n));
    dspico_card_start_xfer(DSPICO_USB_WRITE_DATA_SETTINGS, false);
    dspico_card_cpu_write(s_block_buf, 512 / 4, n);
    if (dspico_card_is_dead())
    {
      ok = false;
      break;
    }
  }
  dspico_card_lock_release_persistent(lock);
  if (ok)
  {
    uplink_frames_sent++;
  }
  else
  {
    uplink_sampler_frame_abort();
    uplink_frames_dropped++;
  }
}

//--------------------------------------------------------------------+
// Public API
//--------------------------------------------------------------------+

void UplinkInit(void)
{
  if (s_inited)
  {
    return;
  }
  uplink_sampler_init();

  // Reserve the uplink's persistent card lock id (best effort: if no
  // id is free the transactions fall back to per-transaction
  // acquisition).
  (void)dspico_card_lock_reserve();

  // Hand USB enumeration over to the DSpico firmware: its local TinyUSB
  // stack (usb_cdc_bridge.c) initializes the RP2040 SIE and pulls up D+.
  // The NDS keeps no USB state; UplinkPoll() ships sample frames and
  // polls the firmware RX block.
  // If the card lock cannot be acquired right now (id pool momentarily
  // exhausted) the command is skipped and retried on a later frame:
  // MainRoutine calls UplinkInit every iteration until s_inited is set.
  if (!uplink_local_send_cmd(DSPICO_CMD_USB_COMMAND_LOCAL_STACK))
  {
    return;
  }

  s_inited = true;
}

void UplinkPoll(void)
{
  // Once the card bus wedged and was force-aborted, never touch it again:
  // the game keeps running, just without USB streaming.
  if (!s_inited || dspico_card_is_dead())
  {
    return;
  }

  // 0. Boot greeting: one unsolicited UPLINK status line right after the
  //    uplink came up (deferred from UplinkInit so the card lock is
  //    available). A reader that stayed connected through a soft reset
  //    receives it, sees cfg=0/0 (table wiped) and re-uploads
  //    immediately — no silence-watchdog wait. Retried on later frames
  //    until the write actually goes out (the lock may be busy).
  if (!s_greeted && uplink_send_status())
  {
    s_greeted = true;
  }

  // 1. Stream one sample frame (when configured, started and idle of
  //    config). Frames flow only with a committed, non-empty table.
  if (s_sampling && !s_config_active && uplink_sampler_entry_count() > 0)
  {
    uplink_stream_frame();
  }

  // 2. Host commands, every ~15 frames (~250 ms) — ALWAYS, including
  //    during config uploads. The rate must not be raised: every 0xEA
  //    read makes the firmware's RX handler do its heavy prep (ring
  //    drain + 512-byte memset) inside the card IRQ, and 60 reads/s
  //    (15x the validated rate) turned every upload into a card-bus
  //    wedge lottery — one lost prep race marks the bus dead and the
  //    uplink never recovers (the reader then fails every retry).
  //    ~250 ms round trips are well within the reader's 2 s ACK timeouts.
  //
  //    EDIT: this comment may be incorrect, testing without
  // if (++s_poll_cnt >= UPLINK_LOCAL_POLL_EVERY)
  //{
  s_poll_cnt = 0;
  if (uplink_local_read_rx(s_block_buf))
  {
    // Quick reject: an all-zero block is an empty RX ring (the
    // firmware zero-fills); skip the parser entirely.
    bool any = false;
    for (uint32_t i = 0; i < sizeof(s_block_buf); i++)
    {
      if (s_block_buf[i] != 0)
      {
        any = true;
        break;
      }
    }
    if (any)
    {
      if (uplink_cmd_feed(s_block_buf, sizeof(s_block_buf)))
      {
        s_config_watchdog = 0; // host activity
      }
    }
    else if (s_config_active)
    {
      // Host reachable but silent: only THIS counts against the config
      // watchdog. When the read itself fails (the game holds the card
      // during saves/loading screens), the watchdog must freeze — the
      // host is waiting patiently and the config must survive until
      // the game frees up.
      s_config_watchdog++;
    }
    //}
  }

  // 3. Config watchdog: abort a config the host ABANDONED (reachable
  //    but silent — see the increment site above). A busy game freezing
  //    the reads does NOT advance it.
  if (s_config_active)
  {
    if (s_config_watchdog >= UPLINK_CONFIG_WATCHDOG_FRAMES)
    {
      uplink_config_abort_silent();
    }
  }
}
