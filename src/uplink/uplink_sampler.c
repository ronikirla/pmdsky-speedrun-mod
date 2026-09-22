#include "uplink_sampler.h"
#include "dspico_card.h"

// Game RAM + mod globals used by the default sample table. They are
// re-declared here (instead of including pmdsky.h / timer.h /
// speedrun_hud.h) with types that exactly match their definitions, because
// this translation unit must stay compatible with the standard fixed-width
// types (pmdsky.h self-defines conflicting typedefs and a bool macro).
//   PLAY_TIME_SECONDS / PLAY_TIME_FRAME_COUNTER: pmdsky data/ram.h
//     (EU 0x022ABFD4 / 0x022ABFD8, resolved via the linker script)
//   start_time / file_timer: src/timer.c
//   hud_display_mode: src/speedrun_hud.c
extern unsigned int PLAY_TIME_SECONDS;
extern unsigned char PLAY_TIME_FRAME_COUNTER;

struct play_time { // mirror of pmdsky types/common/common.h (8 bytes)
  unsigned int seconds;
  unsigned char frames;
  unsigned char _padding[3];
};
extern struct play_time start_time;
extern unsigned char file_timer; // bool in timer.c (pmdsky uint8_t)

enum hud_display_mode { // mirror of src/speedrun_hud.h
  HUD_DISPLAY_NONE = 0,
  HUD_DISPLAY_MINIMAL = 1,
  HUD_DISPLAY_MAXIMAL = 2,
  HUD_DISPLAY_COUNT
};
extern enum hud_display_mode hud_display_mode;

uint32_t uplink_frames_sent = 0;
uint32_t uplink_frames_dropped = 0;
uint32_t uplink_card_lock_skips = 0;

static struct {
  uint32_t address; // 0 = disabled
} s_samples[UPLINK_SAMPLE_COUNT];

static uint32_t s_seq = 0;

static uint8_t s_block[UPLINK_FRAMES_PER_BLOCK * UPLINK_FRAME_LEN];
static uint32_t s_block_len = 0;

void uplink_sampler_init(void) {
  uplink_frames_sent = 0;
  uplink_frames_dropped = 0;
  uplink_card_lock_skips = 0;
  s_seq = 0;
  s_block_len = 0;

  // Default table: the speedrun-critical clocks plus uplink self-observability
  s_samples[0].address = (uint32_t)&PLAY_TIME_SECONDS;
  s_samples[1].address = (uint32_t)&PLAY_TIME_FRAME_COUNTER;
  s_samples[2].address = (uint32_t)&start_time;
  s_samples[3].address = (uint32_t)&file_timer;
  s_samples[4].address = (uint32_t)&hud_display_mode;
  s_samples[5].address = (uint32_t)&REG_MCCNT1; // card busy/latency visibility
  s_samples[6].address = (uint32_t)&REG_MCCNT0;
  s_samples[7].address = (uint32_t)&uplink_frames_sent;
  s_samples[8].address = (uint32_t)&uplink_frames_dropped;
  s_samples[9].address = (uint32_t)&uplink_card_lock_skips;
}

void uplink_sampler_tick(void) {
  if (s_block_len >= sizeof(s_block)) {
    uplink_frames_dropped += UPLINK_FRAMES_PER_BLOCK; // block not flushed in time
    s_block_len = 0;
  }

  struct uplink_frame* f = (struct uplink_frame*)(s_block + s_block_len);
  f->magic[0] = 'P';
  f->magic[1] = 'M';
  f->seq = s_seq++;
  f->game_frame = (uint32_t)PLAY_TIME_SECONDS * 60u + PLAY_TIME_FRAME_COUNTER;

  for (uint8_t i = 0; i < UPLINK_SAMPLE_COUNT; i++) {
    f->samples[i] = s_samples[i].address
        ? *(volatile uint32_t*)s_samples[i].address
        : 0;
  }

  uint8_t* p = (uint8_t*)f;
  uint16_t sum = 0;
  // Checksum covers everything before the checksum field itself. The two
  // bytes after it are zero padding (UPLINK_FRAME_LEN is 54, the packed
  // struct is 52; the padding keeps 9 frames at 486 bytes per block).
  for (uint32_t i = 0; i < UPLINK_FRAME_LEN - 4; i++) {
    sum += p[i];
  }
  f->checksum = sum;

  s_block_len += UPLINK_FRAME_LEN;
}

uint32_t uplink_sampler_flush_block(uint8_t* dst) {
  uint32_t len = s_block_len;
  if (len > 0) {
    // byte-wise copy: dst is a TinyUSB buffer, both sides are 4-aligned but
    // keep it simple and safe
    uint8_t* d = dst;
    uint8_t* s = s_block;
    for (uint32_t i = 0; i < len; i++) {
      d[i] = s[i];
    }
    s_block_len = 0;
  }
  return len;
}

void uplink_sampler_discard_block(void) {
  s_block_len = 0;
}

bool uplink_sampler_set_addr(uint8_t index, uint32_t addr) {
  if (index >= UPLINK_SAMPLE_COUNT) {
    return false;
  }
  s_samples[index].address = addr;
  return true;
}

uint32_t uplink_sampler_get_addr(uint8_t index) {
  if (index >= UPLINK_SAMPLE_COUNT) {
    return 0;
  }
  return s_samples[index].address;
}

bool uplink_sampler_block_full(void) {
  return s_block_len >= sizeof(s_block);
}

uint32_t uplink_sampler_get_seq(void) {
  return s_seq;
}
