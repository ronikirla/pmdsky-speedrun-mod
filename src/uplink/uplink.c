#include "uplink.h"
#include "dspico_card.h"
#include "uplink_sampler.h"

#include <string.h>

static bool s_inited = false;
static bool s_sampling = false;

//--------------------------------------------------------------------+
// Firmware protocol: the TinyUSB device stack runs in the DSpico firmware
// (usb_cdc_bridge.c). The NDS never calls into TinyUSB; it ships sample
// blocks over WRITE_DATA (0xE9) and polls a status block plus host RX
// over READ_DATA (0xEA).
//--------------------------------------------------------------------+

// 0xEA endpoint field selects the returned 512-byte block content:
#define UPLINK_LOCAL_STATUS_EP 0
#define UPLINK_LOCAL_RX_EP 1
// 0xE9 endpoint field (ignored by the firmware, kept meaningful for logs)
#define UPLINK_LOCAL_TX_EP 0x82

// Cached firmware status (refreshed every UPLINK_LOCAL_POLL_EVERY frames)
typedef struct
{
  bool valid;
  bool configured;    // SET_CONFIGURATION accepted (tud_mounted on DSpico)
  bool cdc_connected; // CDC data interface open
  bool dtr;
  bool rts;
  uint32_t tx_bytes;   // bytes pumped to the host
  uint32_t tx_drops;   // bytes dropped (firmware TX ring overflow)
  uint32_t rx_pending; // host bytes waiting in the firmware RX ring
} uplink_local_status_t;

static uplink_local_status_t s_local;
static uint8_t s_rx_buf[512];

// Host replies (e.g. PING) staged here and shipped on the next poll
static uint8_t s_tx_stage[512];
static uint32_t s_tx_stage_len = 0;

// Poll cadence: status/RX refresh every ~15 frames (~250 ms at 60 Hz)
#define UPLINK_LOCAL_POLL_EVERY 15

static uint32_t uplink_local_le32(uint8_t const *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

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
// 512-byte phase is zero-padded by dspico_card_cpu_write).
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

// Read a 512-byte block (status or host RX) from the firmware.
// Returns 0 when the transaction was skipped (card lock unavailable or card
// bus dead); the contents of dst are then undefined.
static uint32_t uplink_local_read_block(uint8_t *dst, uint32_t endpoint)
{
  uint16_t lock = dspico_card_lock_wait_persistent();
  if (lock == DSPICO_LOCK_ID_INVALID)
  {
    uplink_card_lock_skips++;
    return 0;
  }
  dspico_card_set_cmd(DSPICO_CMD_USB_READ_DATA(0, endpoint));
  dspico_card_start_xfer(DSPICO_USB_READ_DATA_SETTINGS, false);
  dspico_card_cpu_read(dst, 512 / 4);
  dspico_card_lock_release_persistent(lock);
  return 512;
}

// Refresh s_local from the firmware status block; returns true on success
static bool uplink_local_poll_status(void)
{
  uint8_t block[512];
  if (uplink_local_read_block(block, UPLINK_LOCAL_STATUS_EP) != 512)
  {
    return false;
  }
  uint32_t w0 = uplink_local_le32(block + 0);
  s_local.valid = true;
  s_local.configured = (w0 & 2u) != 0;
  s_local.cdc_connected = (w0 & 4u) != 0;
  s_local.dtr = (w0 & 8u) != 0;
  s_local.rts = (w0 & 16u) != 0;
  s_local.tx_bytes = uplink_local_le32(block + 4);
  s_local.tx_drops = uplink_local_le32(block + 8);
  s_local.rx_pending = uplink_local_le32(block + 12);
  return true;
}

// Send host-bound data (command replies). Staged and shipped over
// WRITE_DATA on the next UplinkPoll iteration.
static void uplink_send_host_reply(const uint8_t *data, uint32_t len)
{
  if (s_tx_stage_len + len > sizeof(s_tx_stage))
  {
    uplink_local_write_block(s_tx_stage, s_tx_stage_len); // flush, no drops (replies are small)
    s_tx_stage_len = 0;
  }
  memcpy(s_tx_stage + s_tx_stage_len, data, len);
  s_tx_stage_len += len;
}

//--------------------------------------------------------------------+
// Host command protocol (PC -> NDS over CDC):
//   [0x01]           start sampling
//   [0x02]           stop sampling
//   [0x03 idx u32]   retarget sample slot `idx` to address (0 disables)
//   [0x04]           ping -> "UPLINK seq=<n> sent=<n> drop=<n>\r\n"
//--------------------------------------------------------------------+

static uint32_t uplink_le32(uint8_t const *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

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

static void uplink_handle_host_command(uint8_t const *buf, uint16_t len)
{
  if (len == 0)
  {
    return;
  }
  switch (buf[0])
  {
  case 0x01: // START
    s_sampling = true;
    break;
  case 0x02: // STOP
    s_sampling = false;
    uplink_sampler_discard_block();
    break;
  case 0x03: // SET_ADDR idx u32
    if (len >= 5)
    {
      uplink_sampler_set_addr(buf[1], uplink_le32(buf + 2));
    }
    break;
  case 0x04:
  { // PING
    char msg[64];
    uint8_t *p = (uint8_t *)msg;
    uint8_t n = 0;
    const char *prefix = "UPLINK seq=";
    while (*prefix)
      p[n++] = (uint8_t)*prefix++;
    n += uplink_utoa(uplink_sampler_get_seq(), p + n);
    const char *mid = " sent=";
    while (*mid)
      p[n++] = (uint8_t)*mid++;
    n += uplink_utoa(uplink_frames_sent, p + n);
    const char *tail = " drop=";
    while (*tail)
      p[n++] = (uint8_t)*tail++;
    n += uplink_utoa(uplink_frames_dropped, p + n);
    p[n++] = '\r';
    p[n++] = '\n';
    uplink_send_host_reply((uint8_t *)msg, n);
    break;
  }
  default:
    break;
  }
}

// Host command stream parsing. Commands arrive as a byte stream over the
// firmware RX ring (two commands may share one 512-byte block), so bytes are
// accumulated and complete commands are emitted as they become available.
static uint8_t s_cmd[6]; // longest command: SET_ADDR (0x03 idx u32) = 6 bytes
static uint8_t s_cmd_len = 0;

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
    uint8_t need = 0;
    switch (s_cmd[0])
    {
    case 0x01:
    case 0x02:
    case 0x04:
      need = 1;
      break;
    case 0x03:
      need = 6;
      break;
    default:
      uplink_cmd_drop_first(); // unknown byte; resynchronize
      continue;
    }
    if (s_cmd_len < need)
    {
      return; // incomplete command; wait for more bytes
    }
    uplink_handle_host_command(s_cmd, need);
    for (uint8_t i = 0; i + need < s_cmd_len; i++)
    {
      s_cmd[i] = s_cmd[i + need];
    }
    s_cmd_len -= need;
  }
}

// Feed host bytes into the command stream parser (commands are executed
// as their bytes complete, regardless of USB packet boundaries).
static void uplink_cmd_feed(const uint8_t *data, uint32_t len)
{
  for (uint32_t i = 0; i < len; i++)
  {
    if (s_cmd_len >= sizeof(s_cmd))
    {
      uplink_cmd_drop_first(); // overflow; drop the oldest byte
    }
    s_cmd[s_cmd_len++] = data[i];
  }
  uplink_cmd_parse();
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
  // The NDS keeps no USB state; UplinkPoll() ships sample blocks and polls
  // the firmware status block.
  // If the card lock cannot be acquired right now (id pool momentarily
  // exhausted) the command is skipped and retried on a later frame:
  // MainRoutine calls UplinkInit every iteration until s_inited is set.
  if (!uplink_local_send_cmd(DSPICO_CMD_USB_COMMAND_LOCAL_STACK))
  {
    return;
  }

  s_sampling = true; // auto-start; host can pause with 0x02
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

  // 1. Flush staged host replies (PING); retry on the next poll if the
  //    card lock is momentarily unavailable
  if (s_tx_stage_len > 0)
  {
    if (uplink_local_write_block(s_tx_stage, s_tx_stage_len))
    {
      s_tx_stage_len = 0;
    }
  }

  // 2. Sample + stream: one 512-byte card transaction per full block
  //    (9 frames); the firmware pumps the bytes to the host at USB rate,
  //    decoupled from the game frame timing.
  if (s_sampling)
  {
    uplink_sampler_tick();
    if (uplink_sampler_block_full())
    {
      uint8_t stage[512];
      uint32_t len = uplink_sampler_flush_block(stage);
      if (len > 0)
      {
        if (uplink_local_write_block(stage, len))
        {
          uplink_frames_sent += len / UPLINK_FRAME_LEN;
        }
        else
        {
          uplink_frames_dropped += len / UPLINK_FRAME_LEN;
        }
      }
    }
  }

  // 3. Firmware status + host RX, every ~15 frames (~250 ms)
  {
    static uint32_t s_poll_cnt = 0;
    if (++s_poll_cnt >= UPLINK_LOCAL_POLL_EVERY)
    {
      s_poll_cnt = 0;
      if (uplink_local_poll_status())
      {
        if (s_local.rx_pending > 0)
        {
          uint32_t n = uplink_local_read_block(s_rx_buf, UPLINK_LOCAL_RX_EP);
          uplink_cmd_feed(s_rx_buf, n);
        }
      }
    }
  }
}
