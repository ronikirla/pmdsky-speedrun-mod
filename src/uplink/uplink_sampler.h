// ARM9 memory sampler for the uplink.
//
// Each tick reads UPLINK_SAMPLE_COUNT 32-bit values from a runtime-
// retargetable table and appends a 54-byte frame:
//   [0..1]   magic 'P' 'M'
//   [2..5]   seq          uplink frame index (wraps at 2^32)
//   [6..9]   game_frame   PLAY_TIME (seconds*60 + frames) at sample time
//   [10..49] samples[10]  little-endian u32
//   [50..51] checksum     sum of bytes [0..49]
//   [52..53] padding      zero (kept so 9 frames fill 486 bytes)
//
// Frames are staged in a 486-byte block (9 frames per 512-byte DSpico card
// transaction); the uplink flushes a full block over CDC.
#pragma once

// Same type-separation rule as dspico_card.h: standard types only, no
// <pmdsky.h> (this header is included by TinyUSB-using translation units).
#include <stdbool.h>
#include <stdint.h>

#define UPLINK_SAMPLE_COUNT   10
#define UPLINK_FRAME_LEN      54
#define UPLINK_FRAMES_PER_BLOCK 9

struct uplink_frame {
  uint8_t magic[2];
  uint32_t seq;
  uint32_t game_frame;
  uint32_t samples[UPLINK_SAMPLE_COUNT];
  uint16_t checksum;
} __attribute__((packed));

// Linkable counters (also used as self-samples by the default table)
extern uint32_t uplink_frames_sent;
extern uint32_t uplink_frames_dropped;
extern uint32_t uplink_card_lock_skips;
extern uint32_t uplink_card_lock_waits; // lock waits >= 2 ms (contention with game I/O)
extern uint32_t uplink_card_busy_timeouts; // wait_busy > 20 ms (DSpico firmware stuck)

// Set up the default sample table and reset counters
void uplink_sampler_init(void);

// Read the sample table and append one frame to the staging block
void uplink_sampler_tick(void);

// Copy the staged block into dst (up to UPLINK_FRAME_LEN * FRAMES_PER_BLOCK),
// reset the staging area, and return the number of valid bytes
uint32_t uplink_sampler_flush_block(uint8_t* dst);

// Discard the staged block (e.g. when the USB TX FIFO cannot accept it)
void uplink_sampler_discard_block(void);

// Runtime re-targeting: addr == 0 disables the slot
bool uplink_sampler_set_addr(uint8_t index, uint32_t addr);
uint32_t uplink_sampler_get_addr(uint8_t index);

// True once the staging block holds a full set of frames
bool uplink_sampler_block_full(void);
uint32_t uplink_sampler_get_seq(void);
