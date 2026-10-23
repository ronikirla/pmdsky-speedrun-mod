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
struct __attribute__((packed)) uplink_entry {
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

static struct uplink_entry s_table[UPLINK_MAX_SAMPLES];
static uint16_t s_entry_count;    // committed (streaming) entries
static uint16_t s_staging_count;  // entries applied in the current config
static uint16_t s_declared_count; // count declared by CONFIG_BEGIN
static uint16_t s_epoch;
static uint32_t s_seq;

// Frame writer state. A frame is built atomically in ONE frame_fill call
// (with UPLINK_MAX_SAMPLES = 120 the longest frame is 498 bytes, so it
// always fits the caller's 512-byte scratch): no incremental emission, no
// per-byte position tracking, no completion constant — the checksum is
// written directly from the final sum. (The previous incremental writer's
// completion check fired one byte early on hardware and silently dropped
// the high checksum byte; building the whole frame removes that entire
// class of bug.) The flag only makes the caller's pull loop terminate
// after one frame.
static bool s_frame_active;

// Store a little-endian u32 at p
static void put_le32(uint8_t* p, uint32_t v) {
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
static bool in_ram(uint32_t addr, uint32_t len) {
  return addr >= RAM_BASE && addr < RAM_END && (RAM_END - addr) >= len;
}

// Assemble a little-endian value from single-byte reads. Safe for any
// address: ARM9 unaligned LDR/LDRH rotate instead of reading across the
// word boundary, so misaligned addresses must not use wide loads.
static uint32_t read_bytewise(uint32_t addr, uint8_t width) {
  uint32_t v = 0;
  for (uint8_t b = 0; b < width; b++) {
    v |= (uint32_t)*(volatile uint8_t*)(addr + b) << (8 * b);
  }
  return v;
}

// Evaluate one entry for the current frame. Pointer paths are walked
// every frame; any failed in_ram check yields 0 (never a hard crash).
static uint32_t eval_entry(const struct uplink_entry* e) {
  uint32_t cur = e->base;
  for (uint8_t i = 0; i < e->nsteps; i++) {
    cur += (uint32_t)(e->steps[i] & 0x7FFFu);
    if (e->steps[i] & 0x8000u) {
      if (!in_ram(cur, 4)) {
        return 0;
      }
      cur = read_bytewise(cur, 4);
    }
  }
  if (!in_ram(cur, e->width)) {
    return 0;
  }
  return read_bytewise(cur, e->width);
}

//--------------------------------------------------------------------+
// Config upload
//--------------------------------------------------------------------+

void uplink_sampler_init(void) {
  uplink_frames_sent = 0;
  uplink_frames_dropped = 0;
  s_entry_count = 0;
  s_staging_count = 0;
  s_declared_count = 0;
  s_epoch = 0;
  s_seq = 0;
  s_frame_active = false;
}

enum uplink_cfg_err uplink_sampler_configure_begin(uint8_t version, uint16_t count) {
  if (version != UPLINK_CONFIG_VERSION) {
    return UPLINK_CFG_ERR_VER;
  }
  if (count == 0 || count > UPLINK_MAX_SAMPLES) {
    return UPLINK_CFG_ERR_COUNT;
  }
  s_staging_count = 0;
  s_declared_count = count;
  s_frame_active = false; // kill any half-built frame
  return UPLINK_CFG_OK;
}

enum uplink_cfg_err uplink_sampler_configure_entry(const uint8_t* cmd, uint16_t len) {
  // cmd[0] == UPLINK_CMD_CONFIG_ENTRY (checked by the caller's parser)
  uint16_t idx = (uint16_t)(cmd[1] | ((uint16_t)cmd[2] << 8));
  uint32_t base = (uint32_t)cmd[3] | ((uint32_t)cmd[4] << 8) |
                  ((uint32_t)cmd[5] << 16) | ((uint32_t)cmd[6] << 24);
  uint8_t meta = cmd[7];
  uint8_t width_code = meta & 3u;
  uint8_t nsteps = (meta >> 2) & 7u;

  if (width_code > 2) {
    return UPLINK_CFG_ERR_WIDTH;
  }
  if (nsteps > UPLINK_MAX_STEPS) {
    return UPLINK_CFG_ERR_STEPS;
  }
  uint16_t need = (uint16_t)(8 + 3 * nsteps);
  if (len < need) {
    return UPLINK_CFG_ERR_STATE; // parser guarantees the full command
  }
  if (idx > s_staging_count) {
    return UPLINK_CFG_ERR_IDX; // gap: an earlier entry was lost
  }
  if (idx >= s_declared_count) {
    return UPLINK_CFG_ERR_FULL;
  }
  // Direct entries must point into RAM (typo feedback). Pointer-path
  // bases are pointer LOCATIONS and are accepted anywhere: the game only
  // ever reads at deref targets and the final address, and each of those
  // reads is RAM-checked per frame (a path based outside RAM, or on a
  // pointer that is null for part of the game, simply samples 0 until it
  // becomes valid — see eval_entry).
  if (nsteps == 0 && !in_ram(base, 1)) {
    return UPLINK_CFG_ERR_BASE;
  }

  // idx == s_staging_count: append the next entry. idx < s_staging_count:
  // a RE-SENT entry (the reader retries a batch until OK ENTRIES
  // confirms it) — overwrite in place, do not advance the count. This
  // makes batch delivery retry-until-confirmed instead of one-shot.
  struct uplink_entry* e = &s_table[idx];
  for (uint8_t i = 0; i < nsteps; i++) {
    const uint8_t* step = cmd + 8 + 3 * i;
    uint16_t offset = (uint16_t)(step[0] | ((uint16_t)step[1] << 8));
    if (offset > 0x7FFFu) {
      return UPLINK_CFG_ERR_OFFSET;
    }
    // flags bit 0 = deref; remaining bits reserved (ignored)
    e->steps[i] = offset | (step[2] & 1u ? 0x8000u : 0u);
  }
  e->base = base;
  e->width = (uint8_t)(1u << width_code); // 1, 2 or 4
  e->nsteps = nsteps;
  if (idx == s_staging_count) {
    s_staging_count++;
  }
  return UPLINK_CFG_OK;
}

void uplink_sampler_configure_commit(void) {
  s_entry_count = s_staging_count;
  s_epoch++;
  s_frame_active = false;
}

void uplink_sampler_configure_abort(void) {
  s_staging_count = 0;
  s_declared_count = 0;
  s_entry_count = 0;
  s_frame_active = false;
}

uint16_t uplink_sampler_staging_count(void) {
  return s_staging_count;
}

uint16_t uplink_sampler_declared_count(void) {
  return s_declared_count;
}

uint16_t uplink_sampler_entry_count(void) {
  return s_entry_count;
}

uint16_t uplink_sampler_epoch(void) {
  return s_epoch;
}

//--------------------------------------------------------------------+
// Frame streaming
//--------------------------------------------------------------------+

bool uplink_sampler_frame_start(void) {
  if (s_frame_active || s_entry_count == 0) {
    return false;
  }
  s_frame_active = true;
  return true;
}

// Build the complete frame into dst and return its length
// (UPLINK_FRAME_HEADER_LEN + 4*entry_count + 2). The next call returns 0
// (frame consumed), which terminates the caller's pull loop. The seq
// counter increments here — a frame dropped before this point consumes
// no seq and leaves no gap in the host's seq stream.
uint32_t uplink_sampler_frame_fill(uint8_t* dst, uint32_t dst_len) {
  if (!s_frame_active) {
    return 0;
  }
  uint16_t n = s_entry_count;
  uint16_t frame_len = (uint16_t)(UPLINK_FRAME_HEADER_LEN + 4u * n + 2u);
  if (dst_len < frame_len) {
    // Cannot happen with the 512-byte scratch (max frame 498 bytes);
    // drop the frame rather than ship a truncated one.
    s_frame_active = false;
    return 0;
  }

  // Header: magic, seq, game_frame, sample_count, epoch, reserved
  dst[0] = 'P';
  dst[1] = 'M';
  put_le32(dst + 2, s_seq);
  put_le32(dst + 6, (uint32_t)PLAY_TIME_SECONDS * 60u + PLAY_TIME_FRAME_COUNTER);
  dst[10] = (uint8_t)n;
  dst[11] = (uint8_t)(n >> 8);
  dst[12] = (uint8_t)s_epoch;
  dst[13] = (uint8_t)(s_epoch >> 8);
  dst[14] = 0;
  dst[15] = 0;

  // Samples (little-endian u32 each) folded into the checksum
  uint32_t sum = 0;
  for (uint16_t i = 0; i < n; i++) {
    uint32_t v = eval_entry(&s_table[i]);
    uint8_t* p = dst + UPLINK_FRAME_HEADER_LEN + 4u * i;
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
    sum += p[0];
    sum += p[1];
    sum += p[2];
    sum += p[3];
  }
  // Header bytes are part of the checksum too
  for (uint16_t i = 0; i < UPLINK_FRAME_HEADER_LEN; i++) {
    sum += dst[i];
  }

  // Checksum: byte sum of everything before it (u16 wrap); the checksum
  // bytes themselves are not part of it.
  dst[frame_len - 2] = (uint8_t)(sum & 0xFFu);
  dst[frame_len - 1] = (uint8_t)((sum >> 8) & 0xFFu);

  // Sacrificial trailing byte. The card->firmware->USB transport
  // consistently loses the LAST byte of each transaction's valid payload
  // (v1 absorbed this invisibly: its 70-byte frames ended with 2 padding
  // bytes after the checksum). Without a sacrificial byte the frame's
  // high checksum byte IS the last byte and gets replaced by 00 on the
  // wire (seen on device: stored=0x00AE vs expected=0x01AE). The reader
  // parses frames by magic + declared length, so the extra byte is
  // skipped as inter-frame garbage; the frame layout itself is unchanged.
  dst[frame_len] = dst[frame_len - 1];

  s_frame_active = false;
  s_seq++;
  return frame_len + 1u;
}

void uplink_sampler_frame_abort(void) {
  // Frames are built atomically; nothing to unwind.
  s_frame_active = false;
}

uint32_t uplink_sampler_get_seq(void) {
  return s_seq;
}
