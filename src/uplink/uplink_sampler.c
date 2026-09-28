#include "uplink_sampler.h"

// Game RAM + mod globals used by the default sample table. They are
// re-declared here (instead of including pmdsky.h) with types that exactly
// match their definitions, because this translation unit must stay
// compatible with the standard fixed-width types (pmdsky.h self-defines
// conflicting typedefs and a bool macro).
//   PLAY_TIME_SECONDS / PLAY_TIME_FRAME_COUNTER: pmdsky data/ram.h
//     (EU 0x022ABFD4 / 0x022ABFD8, resolved via the linker script)
extern unsigned int PLAY_TIME_SECONDS;
extern unsigned char PLAY_TIME_FRAME_COUNTER;

// Fixed EU (0x02xxxxxx) game-memory addresses for the default sample table.
// (The mod docs list the short forms, e.g. 0x2ABFD8 == 0x022ABFD8; the
// dungeon_ptr value 0x02354138 was cross-checked against the DUNGEON_PTR
// symbol in the debug headers.)
#define ADDR_SCENARIO_MAIN_FLAG_MAIN 0x022ABAA8u
#define ADDR_SCENARIO_MAIN_FLAG_SUB  0x022ABAA9u
#define ADDR_REQUEST_CLEAR_COUNT     0x022ABADBu
#define ADDR_REQUEST_CLEAR_COUNT_U16 0x022A40E4u
#define ADDR_MAGIC_NUMBER            0x022A3670u
#define ADDR_OVERLAY1_START          0x02329D40u
#define ADDR_DUNGEON_PTR             0x02354138u
#define ADDR_SCRIPT_ID_PART1         0x02325ACAu
#define ADDR_SCRIPT_ID_PART2         0x02325ACEu

// Offsets from dungeon_ptr (see pmdsky-debug/headers/.../dungeon_mode/dungeon.h)
#define DUNGEON_OFF_IS_CLEARING_FLOOR 0x006u
#define DUNGEON_OFF_CURRENT_DUNGEON_ID 0x748u
#define DUNGEON_OFF_CURRENT_FLOOR      0x749u

uint32_t uplink_frames_sent = 0;
uint32_t uplink_frames_dropped = 0;
uint32_t uplink_card_lock_skips = 0;
uint32_t uplink_card_lock_waits = 0;
uint32_t uplink_card_busy_timeouts = 0;

// One sample slot. `address` is either an absolute ARM9 address (direct
// read) or an offset from the current dungeon_ptr (via_dungeon_ptr slots).
// `width` is the read width (1, 2 or 4); narrower values are zero-extended
// into the u32 slot. address == 0 disables the slot.
static struct {
  uint32_t address;
  uint8_t width;
  uint8_t via_dungeon_ptr;
} s_samples[UPLINK_SAMPLE_COUNT];

static uint32_t s_seq = 0;

static uint8_t s_block[UPLINK_FRAMES_PER_BLOCK * UPLINK_FRAME_LEN];
static uint32_t s_block_len = 0;

static void slot_set(uint8_t index, uint32_t address, uint8_t width,
                     uint8_t via_dungeon_ptr) {
  s_samples[index].address = address;
  s_samples[index].width = width;
  s_samples[index].via_dungeon_ptr = via_dungeon_ptr;
}

// A dungeon pointer is only dereferenced while it stays inside ARM9 WRAM
// (0x02000000-0x023FFFFF) with margin for the largest offset below (0x749).
static uint32_t valid_dungeon_ptr(uint32_t p) {
  return (p >= 0x02000000u && p < 0x023F8000u) ? p : 0;
}

void uplink_sampler_init(void) {
  uplink_frames_sent = 0;
  uplink_frames_dropped = 0;
  uplink_card_lock_skips = 0;
  uplink_card_lock_waits = 0;
  uplink_card_busy_timeouts = 0;
  s_seq = 0;
  s_block_len = 0;

  // Default table (one value per slot, narrower values zero-extended):
  //  0 PLAY_TIME_SECONDS          u32 @ &PLAY_TIME_SECONDS (linker-resolved)
  //  1 PLAY_TIME_FRAME_COUNTER    u8  @ &PLAY_TIME_FRAME_COUNTER
  //  2 SCENARIO_MAIN_FLAG_MAIN    u8  @ 0x022ABAA8
  //  3 SCENARIO_MAIN_FLAG_SUB     u8  @ 0x022ABAA9
  //  4 REQUEST_CLEAR_COUNT        u8  @ 0x022ABADB
  //  5 REQUEST_CLEAR_COUNT_U16    u16 @ 0x022A40E4
  //  6 magic_number               u32 @ 0x022A3670
  //  7 overlay1_start             u16 @ 0x02329D40
  //  8 dungeon_ptr                u32 @ 0x02354138
  //  9 current_script_id_part1    u32 @ 0x02325ACA
  // 10 current_script_id_part2    u32 @ 0x02325ACE
  // 11 is_clearing_floor          u8  @ dungeon_ptr + 0x6
  // 12 current_dungeon_id         u8  @ dungeon_ptr + 0x748
  // 13 current_floor              u8  @ dungeon_ptr + 0x749
  slot_set(0, (uint32_t)&PLAY_TIME_SECONDS, 4, 0);
  slot_set(1, (uint32_t)&PLAY_TIME_FRAME_COUNTER, 1, 0);
  slot_set(2, ADDR_SCENARIO_MAIN_FLAG_MAIN, 1, 0);
  slot_set(3, ADDR_SCENARIO_MAIN_FLAG_SUB, 1, 0);
  slot_set(4, ADDR_REQUEST_CLEAR_COUNT, 1, 0);
  slot_set(5, ADDR_REQUEST_CLEAR_COUNT_U16, 2, 0);
  slot_set(6, ADDR_MAGIC_NUMBER, 4, 0);
  slot_set(7, ADDR_OVERLAY1_START, 2, 0);
  slot_set(8, ADDR_DUNGEON_PTR, 4, 0);
  slot_set(9, ADDR_SCRIPT_ID_PART1, 4, 0);
  slot_set(10, ADDR_SCRIPT_ID_PART2, 4, 0);
  slot_set(11, DUNGEON_OFF_IS_CLEARING_FLOOR, 1, 1);
  slot_set(12, DUNGEON_OFF_CURRENT_DUNGEON_ID, 1, 1);
  slot_set(13, DUNGEON_OFF_CURRENT_FLOOR, 1, 1);
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

  // Resolve the dungeon pointer once per frame; pointer-derived slots read
  // through it and zero out when it is not valid ARM9 RAM (menus, before
  // dungeon init). The dungeon_ptr slot reports the raw value either way.
  uint32_t dungeon = valid_dungeon_ptr(*(volatile uint32_t*)ADDR_DUNGEON_PTR);

  for (uint8_t i = 0; i < UPLINK_SAMPLE_COUNT; i++) {
    uint32_t addr = s_samples[i].address;
    if (!addr) {
      f->samples[i] = 0;
      continue;
    }
    if (s_samples[i].via_dungeon_ptr) {
      if (!dungeon) {
        f->samples[i] = 0;
        continue;
      }
      addr += dungeon;
    }

    uint8_t w = s_samples[i].width;
    if (w != 1 && w != 2) {
      w = 4;
    }
    if (addr & (w - 1)) {
      f->samples[i] = read_bytewise(addr, w);   // misaligned
    } else if (w == 1) {
      f->samples[i] = *(volatile uint8_t*)addr;
    } else if (w == 2) {
      f->samples[i] = *(volatile uint16_t*)addr;
    } else {
      f->samples[i] = *(volatile uint32_t*)addr;
    }

  }

  uint8_t* p = (uint8_t*)f;
  uint16_t sum = 0;
  // Checksum covers everything before the checksum field itself. The two
  // bytes after it are zero padding (UPLINK_FRAME_LEN is 70, the packed
  // struct is 68).
  for (uint32_t i = 0; i < UPLINK_FRAME_LEN - 4; i++) {
    sum += p[i];
  }
  f->checksum = sum;

  s_block_len += UPLINK_FRAME_LEN;
}

// Assemble a value from single-byte reads, little-endian. Safe for any
// address: ARM9 unaligned LDR/LDRH rotate instead of reading across the
// word boundary, so misaligned addresses must not use wide loads.
static uint32_t read_bytewise(uint32_t addr, uint8_t width) {
  uint32_t v = 0;
  for (uint8_t b = 0; b < width; b++) {
    v |= (uint32_t)*(volatile uint8_t*)(addr + b) << (8 * b);
  }
  return v;
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
  // Retargeting resets the slot to a plain width-4 direct read of addr
  // (any pointer-derived slot semantics are discarded); addr == 0 disables.
  slot_set(index, addr, 4, 0);
  return true;
}

uint32_t uplink_sampler_get_addr(uint8_t index) {
  if (index >= UPLINK_SAMPLE_COUNT) {
    return 0;
  }
  // Direct slots report the absolute address; pointer-derived slots report
  // their dungeon_ptr offset.
  return s_samples[index].address;
}

bool uplink_sampler_block_full(void) {
  return s_block_len >= sizeof(s_block);
}

uint32_t uplink_sampler_get_seq(void) {
  return s_seq;
}
