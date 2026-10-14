#include "uplink_sampler.h"

// Game RAM play-time globals used for the frame's game_frame field. They
// are re-declared here (instead of including pmdsky.h) with types that
// exactly match their definitions, because this translation unit must
// stay compatible with the standard fixed-width types (pmdsky.h
// self-defines conflicting typedefs and a bool macro).
//   PLAY_TIME_SECONDS / PLAY_TIME_FRAME_COUNTER: pmdsky data/ram.h
//     (EU 0x022ABFD4 / 0x022ABFD8, resolved via the linker script)
extern unsigned int PLAY_TIME_SECONDS;
extern unsigned char PLAY_TIME_FRAME_COUNTER;

// ARM9 main RAM window. Requested addresses and every pointer-path
// dereference must land here; anything else (IO, WRAM, palette memory,
// null pointers) samples as 0 instead of being dereferenced.
#define RAM_BASE 0x02000000u
#define RAM_END 0x02400000u

// One compiled table entry: base + up to UPLINK_MAX_STEPS steps. A step
// packs its u16 offset (<= 0x7FFF) with the dereference flag in bit 15.
// packed: the natural layout is 14 bytes but would pad to 16; the region
// budget is tight enough that 2 bytes/entry matter. All members stay
// naturally aligned inside the struct, so only the array stride becomes
// 14 (base reads turn into byte loads — a few per entry per frame).
struct __attribute__((packed)) uplink_entry
{
  uint32_t base;
  uint16_t steps[UPLINK_MAX_STEPS];
  uint8_t width; // 1, 2 or 4
  uint8_t nsteps;
};

//--------------------------------------------------------------------+
// State
//--------------------------------------------------------------------+

uint32_t uplink_frames_sent = 0;
uint32_t uplink_frames_dropped = 0;
uint32_t uplink_card_lock_skips = 0;
uint32_t uplink_card_lock_waits = 0;
uint32_t uplink_card_busy_timeouts = 0; // incremented by dspico_card.c's watchdog

static struct uplink_entry s_table[UPLINK_MAX_SAMPLES];
static uint16_t s_entry_count;    // committed (streaming) entries
static uint16_t s_staging_count;  // entries applied in the current config
static uint16_t s_declared_count; // count declared by CONFIG_BEGIN
static uint16_t s_epoch;
static uint32_t s_seq;

// Frame writer state (one frame at a time; pull model)
static bool s_frame_active;
static uint8_t s_frame_hdr[UPLINK_FRAME_HEADER_LEN]; // prebuilt at start
static uint16_t s_frame_pos;                         // byte position within the frame
// Running checksum over the emitted bytes. u32 ON PURPOSE: the on-device
// frames showed the low checksum byte correct but the high byte always 0
// (stored 0x00BB vs true sum 0x05BB) — an 8-bit-effective accumulator. A
// u32 accumulator with explicit masked emission cannot truncate.
static uint32_t s_frame_sum;
static uint32_t s_frame_sample; // sample currently being emitted
static uint16_t s_frame_loaded; // table index cached in s_frame_sample

// Store a little-endian u32 at p
static void put_le32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

//--------------------------------------------------------------------+
// Memory access helpers
//--------------------------------------------------------------------+

// True when [addr, addr+len) lies inside ARM9 main RAM. Null pointers
// fail automatically (0 < RAM_BASE).
static bool in_ram(uint32_t addr, uint32_t len)
{
  return addr >= RAM_BASE && addr < RAM_END && (RAM_END - addr) >= len;
}

// Assemble a little-endian value from single-byte reads. Safe for any
// address: ARM9 unaligned LDR/LDRH rotate instead of reading across the
// word boundary, so misaligned addresses must not use wide loads.
static uint32_t read_bytewise(uint32_t addr, uint8_t width)
{
  uint32_t v = 0;
  for (uint8_t b = 0; b < width; b++)
  {
    v |= (uint32_t)*(volatile uint8_t *)(addr + b) << (8 * b);
  }
  return v;
}

// Evaluate one entry for the current frame. Pointer paths are walked
// every frame; any failed in_ram check yields 0 (never a hard crash).
static uint32_t eval_entry(const struct uplink_entry *e)
{
  uint32_t cur = e->base;
  for (uint8_t i = 0; i < e->nsteps; i++)
  {
    cur += (uint32_t)(e->steps[i] & 0x7FFFu);
    if (e->steps[i] & 0x8000u)
    {
      if (!in_ram(cur, 4))
      {
        return 0;
      }
      cur = read_bytewise(cur, 4);
    }
  }
  if (!in_ram(cur, e->width))
  {
    return 0;
  }
  return read_bytewise(cur, e->width);
}

//--------------------------------------------------------------------+
// Config upload
//--------------------------------------------------------------------+

void uplink_sampler_init(void)
{
  uplink_frames_sent = 0;
  uplink_frames_dropped = 0;
  uplink_card_lock_skips = 0;
  uplink_card_lock_waits = 0;
  uplink_card_busy_timeouts = 0;
  s_entry_count = 0;
  s_staging_count = 0;
  s_declared_count = 0;
  s_epoch = 0;
  s_seq = 0;
  s_frame_active = false;
}

enum uplink_cfg_err uplink_sampler_configure_begin(uint8_t version, uint16_t count)
{
  if (version != UPLINK_CONFIG_VERSION)
  {
    return UPLINK_CFG_ERR_VER;
  }
  if (count == 0 || count > UPLINK_MAX_SAMPLES)
  {
    return UPLINK_CFG_ERR_COUNT;
  }
  s_staging_count = 0;
  s_declared_count = count;
  s_frame_active = false; // kill any half-built frame
  return UPLINK_CFG_OK;
}

enum uplink_cfg_err uplink_sampler_configure_entry(const uint8_t *cmd, uint16_t len)
{
  // cmd[0] == UPLINK_CMD_CONFIG_ENTRY (checked by the caller's parser)
  uint16_t idx = (uint16_t)(cmd[1] | ((uint16_t)cmd[2] << 8));
  uint32_t base = (uint32_t)cmd[3] | ((uint32_t)cmd[4] << 8) |
                  ((uint32_t)cmd[5] << 16) | ((uint32_t)cmd[6] << 24);
  uint8_t meta = cmd[7];
  uint8_t width_code = meta & 3u;
  uint8_t nsteps = (meta >> 2) & 7u;

  if (width_code > 2)
  {
    return UPLINK_CFG_ERR_WIDTH;
  }
  if (nsteps > UPLINK_MAX_STEPS)
  {
    return UPLINK_CFG_ERR_STEPS;
  }
  uint16_t need = (uint16_t)(8 + 3 * nsteps);
  if (len < need)
  {
    return UPLINK_CFG_ERR_STATE; // parser guarantees the full command
  }
  if (idx > s_staging_count)
  {
    return UPLINK_CFG_ERR_IDX; // gap: an earlier entry was lost
  }
  if (idx >= s_declared_count)
  {
    return UPLINK_CFG_ERR_FULL;
  }
  // Direct entries must point into RAM (typo feedback). Pointer-path
  // bases are pointer LOCATIONS and are accepted anywhere: the game only
  // ever reads at deref targets and the final address, and each of those
  // reads is RAM-checked per frame (a path based outside RAM, or on a
  // pointer that is null for part of the game, simply samples 0 until it
  // becomes valid — see eval_entry).
  if (nsteps == 0 && !in_ram(base, 1))
  {
    return UPLINK_CFG_ERR_BASE;
  }

  // idx == s_staging_count: append the next entry. idx < s_staging_count:
  // a RE-SENT entry (the reader retries a batch until OK ENTRIES
  // confirms it) — overwrite in place, do not advance the count. This
  // makes batch delivery retry-until-confirmed instead of one-shot.
  struct uplink_entry *e = &s_table[idx];
  for (uint8_t i = 0; i < nsteps; i++)
  {
    const uint8_t *step = cmd + 8 + 3 * i;
    uint16_t offset = (uint16_t)(step[0] | ((uint16_t)step[1] << 8));
    if (offset > 0x7FFFu)
    {
      return UPLINK_CFG_ERR_OFFSET;
    }
    // flags bit 0 = deref; remaining bits reserved (ignored)
    e->steps[i] = offset | (step[2] & 1u ? 0x8000u : 0u);
  }
  e->base = base;
  e->width = (uint8_t)(1u << width_code); // 1, 2 or 4
  e->nsteps = nsteps;
  if (idx == s_staging_count)
  {
    s_staging_count++;
  }
  return UPLINK_CFG_OK;
}

void uplink_sampler_configure_commit(void)
{
  s_entry_count = s_staging_count;
  s_epoch++;
  s_frame_active = false;
}

void uplink_sampler_configure_abort(void)
{
  s_staging_count = 0;
  s_declared_count = 0;
  s_entry_count = 0;
  s_frame_active = false;
}

uint16_t uplink_sampler_staging_count(void)
{
  return s_staging_count;
}

uint16_t uplink_sampler_declared_count(void)
{
  return s_declared_count;
}

uint16_t uplink_sampler_entry_count(void)
{
  return s_entry_count;
}

uint16_t uplink_sampler_epoch(void)
{
  return s_epoch;
}

//--------------------------------------------------------------------+
// Frame streaming
//--------------------------------------------------------------------+

bool uplink_sampler_frame_start(void)
{
  if (s_frame_active || s_entry_count == 0)
  {
    return false;
  }
  s_frame_active = true;
  // Header: magic, seq, game_frame, sample_count, epoch (little-endian)
  s_frame_hdr[0] = 'P';
  s_frame_hdr[1] = 'M';
  put_le32(s_frame_hdr + 2, s_seq);
  put_le32(s_frame_hdr + 6,
           (uint32_t)PLAY_TIME_SECONDS * 60u + PLAY_TIME_FRAME_COUNTER);
  s_frame_hdr[10] = (uint8_t)s_entry_count;
  s_frame_hdr[11] = (uint8_t)(s_entry_count >> 8);
  s_frame_hdr[12] = (uint8_t)s_epoch;
  s_frame_hdr[13] = (uint8_t)(s_epoch >> 8);
  s_frame_pos = 0;
  s_frame_sum = 0;
  s_frame_loaded = 0xFFFFu;
  return true;
}

// Emit the next byte of the current frame and fold it into the checksum.
static uint8_t frame_next_byte(void)
{
  uint8_t b;
  uint16_t pos = s_frame_pos;
  uint16_t samples_end = (uint16_t)(UPLINK_FRAME_HEADER_LEN + 4u * s_entry_count);
  if (pos < UPLINK_FRAME_HEADER_LEN)
  {
    b = s_frame_hdr[pos]; // header prebuilt at frame start
  }
  else if (pos < samples_end)
  {
    // Samples: evaluate one entry, emit its 4 bytes little-endian
    uint16_t off = (uint16_t)(pos - UPLINK_FRAME_HEADER_LEN);
    uint16_t idx = (uint16_t)(off >> 2);
    if (idx != s_frame_loaded)
    {
      s_frame_sample = eval_entry(&s_table[idx]);
      s_frame_loaded = idx;
    }
    b = (uint8_t)(s_frame_sample >> ((off & 3u) * 8u));
  }
  else if (pos == samples_end)
  {
    // b = 0x98;
    b = (uint8_t)(s_frame_sum & 0xFFu);
  }
  else
  {
    // b = 0x99;
    b = (uint8_t)((s_frame_sum >> 8) & 0xFFu);
  }

  // The checksum bytes themselves are not part of the checksum; folding
  // them would corrupt the high byte (emitted after the low one).
  if (pos < samples_end)
  {
    s_frame_sum += b;
  }
  s_frame_pos++;
  if (s_frame_pos >= (uint16_t)(samples_end + 3u))
  {
    // Frame complete: publish the seq (an aborted frame never gets here,
    // so it leaves no gap in the host's seq stream).
    s_frame_active = false;
    s_seq++;
  }
  return b;
}

uint32_t uplink_sampler_frame_fill(uint8_t *dst, uint32_t dst_len)
{
  if (!s_frame_active)
  {
    return 0;
  }
  uint32_t n = 0;
  while (n < dst_len && s_frame_active)
  {
    dst[n++] = frame_next_byte();
  }
  return n;
}

void uplink_sampler_frame_abort(void)
{
  s_frame_active = false;
}

uint32_t uplink_sampler_get_seq(void)
{
  return s_seq;
}
