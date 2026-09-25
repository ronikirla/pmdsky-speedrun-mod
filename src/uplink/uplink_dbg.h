// PMDSky uplink debug console.
//
// On-screen log for the DSpico USB uplink, rendered through the game's
// windowing system. The console owns no window of its own: while enabled,
// its 18 newest log lines are drawn into the HUD's existing top-right
// window (HUD_SLOT_TOP_RIGHT, 18x18 chars on the top screen - the slot
// that normally shows the timer or seed) with the same per-frame
// ClearWindow / DrawTextInWindow / UpdateWindow calls the HUD itself
// uses, so no window is ever created or destroyed for the console.
//
// Toggling:
//   - Runtime:  L+R+A (edge) shows/hides the console. While hidden, the
//     slot shows its normal content again.
//   - Compile:  UPLINK_DBG_ENABLED (default 1). Set to 0 (or build with
//     -DUPLINK_DBG_ENABLED=0) and every API below becomes a no-op macro.
//
// Removing it completely:
//   1. delete src/uplink/uplink_dbg.c and this header,
//   2. remove the #include "uplink_dbg.h" lines and the uplink_dbg_* call
//      sites (all single-line, all greppable),
//   3. remove uplink_dbg.o from UPLINK_OBJS in the Makefile and from the
//      section lists in linker.ld,
//   4. remove the HUD_GetWindowId() accessor in src/hud.c / src/hud.h.
//
// Note: this header must not include <stdint.h>/<stdbool.h> - it is
// included from both the pmdsky-header world (threads.c, where uint32_t
// and bool are pmdsky typedefs) and the standard-header world (the
// uplink TUs). Both worlds provide uint32_t and bool before this header
// is used.
#pragma once

#ifndef UPLINK_DBG_ENABLED
#define UPLINK_DBG_ENABLED 1
#endif

#if UPLINK_DBG_ENABLED

// One-time setup; called from UplinkInit().
void uplink_dbg_init(void);

// Called from the main routine thread every frame (once before and once
// after UplinkPoll): handles the L+R+A toggle and, while enabled, redraws
// the HUD's top-right slot with the six newest log lines.
void uplink_dbg_poll(void);

// Append one line to the log. Supported conversions: %% %s %c %d %u %x
// with an optional "0N" zero-pad width (no libc on the NDS). Lines are
// rate-limited (~20/s) and identical repeats are suppressed with a
// periodic "xN same" marker. Only the 18 newest lines are visible (18
// characters each - the top-right HUD slot's width).
void uplink_dbg_log(const char* fmt, ...);

// Append one line bypassing both the ~50 ms rate limit and the repeat
// suppression. Used for fast diagnostic bursts (a USB enumeration runs in
// a few frames, so the rate limit would drop most of the lines). Only the
// 18 newest lines remain visible - the enumeration tail we want.
void uplink_dbg_log_raw(const char* fmt, ...);

// Programmatic equivalent of the L+R+A toggle.
void uplink_dbg_set_enabled(bool enabled);
bool uplink_dbg_is_enabled(void);

// Coarse millisecond clock, frame-driven (~16 ms per game frame; advanced
// by uplink_dbg_ms_tick() from the VCount 0 routine). Wraps every ~49
// days; fine for rate limiting and short elapsed-time deltas.
uint32_t uplink_dbg_ms(void);

// Advance the frame clock; called once per frame from the VCount 0 routine.
void uplink_dbg_ms_tick(uint32_t ms);

#else // UPLINK_DBG_ENABLED == 0

#define uplink_dbg_init() do {} while (0)
#define uplink_dbg_poll() do {} while (0)
#define uplink_dbg_log(...) do {} while (0)
#define uplink_dbg_log_raw(...) do {} while (0)
#define uplink_dbg_set_enabled(enabled) do {} while (0)
#define uplink_dbg_is_enabled() 0
#define uplink_dbg_ms() 0
#define uplink_dbg_ms_tick(ms) do {} while (0)

#endif