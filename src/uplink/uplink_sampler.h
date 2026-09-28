// ARM9 memory sampler for the uplink.
//
// Each tick reads UPLINK_SAMPLE_COUNT values from a runtime-retargetable
// table and appends a 70-byte frame:
//   [0..1]   magic 'P' 'M'
//   [2..5]   seq          uplink frame index (wraps at 2^32)
//   [6..9]   game_frame   PLAY_TIME (seconds*60 + frames) at sample time
//   [10..65] samples[14]  little-endian u32 (narrower fields zero-extended)
//   [66..67] checksum     sum of bytes [0..65]
//   [68..69] padding      zero
//
// Frames are staged in a 490-byte block (7 frames per 512-byte DSpico card
// transaction); the uplink flushes a full block over CDC.
//
// Default table (one value per slot, narrower values zero-extended):
//   0  PLAY_TIME_SECONDS          u32
//   1  PLAY_TIME_FRAME_COUNTER    u8
//   2  SCENARIO_MAIN_FLAG_MAIN    u8
//   3  SCENARIO_MAIN_FLAG_SUB     u8
//   4  REQUEST_CLEAR_COUNT        u8
//   5  REQUEST_CLEAR_COUNT_U16    u16
//   6  magic_number               u32
//   7  overlay1_start             u16
//   8  dungeon_ptr                u32
//   9  current_script_id_part1    u32
//  10  current_script_id_part2    u32
//  11  is_clearing_floor          u8   (dungeon_ptr + 0x6)
//  12  current_dungeon_id         u8   (dungeon_ptr + 0x748)
//  13  current_floor              u8   (dungeon_ptr + 0x749)
// Slots 11-13 zero out while dungeon_ptr is not valid ARM9 RAM.
#pragma once

// Same type-separation rule as dspico_card.h: standard types only, no
// <pmdsky.h> (this header is included by TinyUSB-using translation units).
#include <stdbool.h>
#include <stdint.h>

#define UPLINK_SAMPLE_COUNT   14
#define UPLINK_FRAME_LEN      70
#define UPLINK_FRAMES_PER_BLOCK 1

struct uplink_frame {
  uint8_t magic[2];
  uint32_t seq;
  uint32_t game_frame;
  uint32_t samples[UPLINK_SAMPLE_COUNT];
  uint16_t checksum;
} __attribute__((packed));

// Linkable counters (reported in the PING reply)
extern uint32_t uplink_frames_sent;
extern uint32_t uplink_frames_dropped;
extern uint32_t uplink_card_lock_skips;
extern uint32_t uplink_card_lock_waits; // lock waits >= 2 ms (contention with game I/O)
extern uint32_t uplink_card_busy_timeouts; // wait_busy > 20 ms (DSpico firmware stuck)

// Set up the default sample table and reset counters
void uplink_sampler_init(void);

// Read the sample table and append one frame to the staging block
void uplink_sampler_tick(void);

static uint32_t read_bytewise(uint32_t addr, uint8_t width);

// Copy the staged block into dst (up to UPLINK_FRAME_LEN * FRAMES_PER_BLOCK),
// reset the staging area, and return the number of valid bytes
uint32_t uplink_sampler_flush_block(uint8_t* dst);

// Discard the staged block (e.g. when the USB TX FIFO cannot accept it)
void uplink_sampler_discard_block(void);

// Runtime re-targeting: addr == 0 disables the slot; the slot resets to a
// plain width-4 direct read (any pointer-derived semantics are discarded)
bool uplink_sampler_set_addr(uint8_t index, uint32_t addr);
// Configured address: absolute for direct slots, dungeon_ptr offset for
// pointer-derived slots, 0 when disabled
uint32_t uplink_sampler_get_addr(uint8_t index);

// True once the staging block holds a full set of frames
bool uplink_sampler_block_full(void);
uint32_t uplink_sampler_get_seq(void);
