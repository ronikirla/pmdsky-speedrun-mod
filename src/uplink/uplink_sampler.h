// ARM9 memory sampler for the uplink (protocol v2).
//
// The sample table is NOT built into the mod: the PC reader uploads it on
// every connect (see docs/uplink-protocol.md). Each entry is either a
// direct address or a pointer path (base + up to 4 offset/deref steps).
// Every frame the sampler evaluates every entry and streams one
// variable-length frame:
//
//   [0..1]   magic 'P' 'M'
//   [2..5]   seq           frame index (wraps at 2^32)
//   [6..9]   game_frame    PLAY_TIME (seconds*60 + frames) at sample time
//   [10..11] sample_count  entry count N (reader self-synchronizes)
//   [12..13] epoch         bumped on every CONFIG_COMMIT
//   [14..]   samples[N]    little-endian u32 (narrow reads zero-extended;
//                          failed pointer checks sample as 0)
//   [L-2..L-1] checksum    byte sum of everything before it (u16 wrap)
//
// Frame length L = 16 + 4*N + 2. The caller streams the frame out in
// 512-byte chunks (uplink.c owns the card transactions).
#pragma once

// Same type-separation rule as dspico_card.h: standard types only, no
// <pmdsky.h>.
#include <stdbool.h>
#include <stdint.h>

// Max entries in the uploaded table. The mod region is hard-capped
// (linker.ld: 0x8010 bytes total) and nearly full; the uplink objects are
// compiled -Os (see the Makefile) and this value is the largest that
// links with margin (measured: ~0x60 bytes spare at 120 entries).
#define UPLINK_MAX_SAMPLES 120
#define UPLINK_MAX_STEPS 4

// Host -> device command ids (parser lives in uplink.c; the entry command
// is parsed here).
#define UPLINK_CMD_START 0x01
#define UPLINK_CMD_STOP 0x02
#define UPLINK_CMD_PING 0x04
#define UPLINK_CMD_CONFIG_BEGIN 0x10
#define UPLINK_CMD_CONFIG_ENTRY 0x11
#define UPLINK_CMD_CONFIG_COMMIT 0x12
#define UPLINK_CMD_CONFIG_ABORT 0x13

#define UPLINK_CONFIG_VERSION 1

// Frame header length (magic..epoch) and per-sample size.
#define UPLINK_FRAME_HEADER_LEN 16

// Validation results for the config commands; uplink.c maps them to the
// "ERR <REASON>" reply lines.
enum uplink_cfg_err
{
  UPLINK_CFG_OK = 0,
  UPLINK_CFG_ERR_VER,
  UPLINK_CFG_ERR_COUNT,
  UPLINK_CFG_ERR_FULL,
  UPLINK_CFG_ERR_IDX,
  UPLINK_CFG_ERR_WIDTH,
  UPLINK_CFG_ERR_STEPS,
  UPLINK_CFG_ERR_OFFSET,
  UPLINK_CFG_ERR_BASE,
  UPLINK_CFG_ERR_STATE,
};

// Linkable counters (crash-dump SYS block + PING reply)
extern uint32_t uplink_frames_sent;
extern uint32_t uplink_frames_dropped;
extern uint32_t uplink_card_lock_skips;
extern uint32_t uplink_card_lock_waits;    // lock waits >= 2 ms (contention with game I/O)
extern uint32_t uplink_card_busy_timeouts; // a card transaction exceeded DSPICO_BUSY_SPINS (DSpico firmware stuck)

// Reset all sampler state (called once at boot)
void uplink_sampler_init(void);

//--------------------------------------------------------------------+
// Config upload (host-driven; see docs/uplink-protocol.md)
//--------------------------------------------------------------------+

// Validate and start a new configuration: version must be
// UPLINK_CONFIG_VERSION and 1 <= count <= UPLINK_MAX_SAMPLES. Clears the
// staging area; the previously committed table keeps streaming until
// COMMIT (uplink.c gates streaming during config).
enum uplink_cfg_err uplink_sampler_configure_begin(uint8_t version, uint16_t count);

// Parse + validate + compile one CONFIG_ENTRY command (raw bytes, starting
// with the command id) and append it to the staging area. len must be the
// full command length (8 + 3*steps).
enum uplink_cfg_err uplink_sampler_configure_entry(const uint8_t *cmd, uint16_t len);

// Promote the staging area to the live table and bump the epoch.
void uplink_sampler_configure_commit(void);

// Discard the staging area AND the live table (streaming stops; the host
// re-uploads after an abort).
void uplink_sampler_configure_abort(void);

// Entries applied so far in the current configuration (for OK ENTRIES n=)
uint16_t uplink_sampler_staging_count(void);
// Declared count from CONFIG_BEGIN
uint16_t uplink_sampler_declared_count(void);
// Committed entry count (0 = nothing to stream)
uint16_t uplink_sampler_entry_count(void);
// Epoch value (for OK STREAMING epoch=)
uint16_t uplink_sampler_epoch(void);

//--------------------------------------------------------------------+
// Frame streaming (pull model; uplink.c ships each chunk)
//--------------------------------------------------------------------+

// Begin building one frame. Returns false when there is nothing to stream
// (no committed table) or a previous frame is still unfinished.
bool uplink_sampler_frame_start(void);

// Write the next chunk of the current frame into dst (up to dst_len
// bytes). Returns the number of bytes written; 0 when the frame is
// complete. The seq counter increments when the last byte is emitted (an
// aborted frame therefore consumes no seq and leaves no gap on the wire).
uint32_t uplink_sampler_frame_fill(uint8_t *dst, uint32_t dst_len);

// Abandon the current frame (e.g. the card died mid-frame). No seq is
// consumed.
void uplink_sampler_frame_abort(void);

// Current seq (next frame's index; reported in the PING reply)
uint32_t uplink_sampler_get_seq(void);
