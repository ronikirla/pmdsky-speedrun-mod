// PMDSky uplink debug console implementation.
//
// Type rule (same as uplink_sampler.c / dspico_card.c): this translation
// unit stays on the standard fixed-width types and never includes
// <pmdsky.h>. The game functions it needs are re-declared below with
// signatures that match the pmdsky headers (they resolve against the ROM
// symbol table at link time). hud.h is a plain mod header (no pmdsky
// includes) and is used for the HUD slot API.
//
// Rendering model:
//   - The console owns no window. While enabled, its 18 newest log lines
//     are drawn into the HUD's existing top-right window (HUD_SLOT_TOP_
//     RIGHT, 16x6 chars on the top screen - the slot that normally shows
//     the timer or seed) with the same ClearWindow / DrawTextInWindow /
//     UpdateWindow calls that src/hud.c's UpdateHUD() performs every
//     frame from the main routine thread. Creating or destroying windows
//     from the uplink call chain corrupts the game's window system (see
//     the comment in CustomSetBrightnessExit in src/hud.c), so the
//     console never touches window lifecycle at all.
//   - uplink_dbg_poll() runs on the main routine thread (MainRoutine in
//     src/threads.c) after UpdateHUDSlots(), so while enabled the console
//     always wins the frame's final state of the slot; toggling off
//     redraws the slot once with its normal strings.
//   - Log lines are appended from the uplink call chain (UplinkPoll ->
//     dspico_dcd_poll / TinyUSB callbacks), which is the same thread, so
//     the line buffers need no locking.
//   - If the main thread ever hangs inside a card wait, the console simply
//     freezes; the last rendered line then points at the hang site.
//   - While enabled, the top-right slot shows the 18 newest log lines
//     instead of its usual content; the window itself is still created
//     and closed by the HUD's own fade-driven logic, so fades need no
//     special handling here.
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include "uplink_dbg.h"
#include "hud.h"

#if UPLINK_DBG_ENABLED

//--------------------------------------------------------------------+
// Game API mirrors (see the type rule in the header comment)
//--------------------------------------------------------------------+

// custom_headers.h (2 bytes, byte-identical layout)
struct uplink_dbg_held_buttons
{
  uint8_t a : 1;
  uint8_t b : 1;
  uint8_t select : 1;
  uint8_t start : 1;
  uint8_t right : 1;
  uint8_t left : 1;
  uint8_t up : 1;
  uint8_t down : 1;
  uint8_t r : 1;
  uint8_t l : 1;
  uint8_t x : 1;
  uint8_t y : 1;
  uint8_t _padding : 4;
};

// pmdsky functions (libs.h / arm9.h)
void UpdateWindow(int window_id);
void ClearWindow(int window_id);
void DrawTextInWindow(int window_id, int x, int y, char *string);
void GetHeldButtons(int player, void *out_buttons);

// src/hud.c (the HUD's own render guard: the game-thread CloseHUD() waits
// on it before destroying a window, so the console's render sets it too)
extern bool draw_in_progress;

//--------------------------------------------------------------------+
// State
//--------------------------------------------------------------------+

// The HUD slot the console renders into (src/hud.c widens and heightens
// the top-right slot specifically for this purpose).
#define DBG_SLOT HUD_SLOT_TOP_RIGHT
#define DBG_LINE_COUNT 12      // 12 of the slot's 18 visible lines (18 caused render corruption)
#define DBG_LINE_LEN 27        // 18 chars * 8px = the slot's 144px window width
#define DBG_LINE_PITCH 8       // line height in pixels (8px font, 48px slot)
#define DBG_MIN_LINE_GAP_MS 50 // rate limit for new log lines

// Frame-driven millisecond clock: the game's ARM9 timers are not
// free-running in this game, so no hardware counter can be trusted.
// uplink_dbg_ms_tick() is called once per game frame from the VCount 0
// routine (threads.c), so the estimate advances ~16 ms per frame and
// keeps advancing even if the main thread stalls in a card wait (which
// is what the wait_busy / lock-wait diagnostics below it need). If the
// vblank hook itself stops, the console is frozen anyway.
static uint32_t s_ms_est = 0;

uint32_t uplink_dbg_ms(void)
{
  return s_ms_est;
}

void uplink_dbg_ms_tick(uint32_t ms)
{
  s_ms_est += ms;
}

static bool s_enabled = false;
static bool s_prev_lra = false;

static char s_lines[DBG_LINE_COUNT][DBG_LINE_LEN + 1];
static char s_last_msg[DBG_LINE_LEN + 1];
static uint32_t s_repeat = 0;
static uint32_t s_last_line_ms = 0;
//--------------------------------------------------------------------+
// Minimal printf-style formatter (no libc on the NDS).
// Supported conversions: %% %s %c %d %u %x, with an optional "0N" pad.
//--------------------------------------------------------------------+

static uint32_t dbg_put_u32(char *dst, uint32_t cap, uint32_t v, int base, int width, bool zero_pad)
{
  char tmp[12];
  int n = 0;
  if (v == 0)
  {
    tmp[n++] = '0';
  }
  while (v > 0)
  {
    uint32_t d = v % (uint32_t)base;
    tmp[n++] = (char)(d < 10 ? ('0' + d) : ('a' + d - 10));
    v /= (uint32_t)base;
  }
  uint32_t pad = (uint32_t)n < (uint32_t)width ? (uint32_t)(width - n) : 0;
  uint32_t out = 0;
  for (uint32_t i = 0; i < pad && out + 1 < cap; i++)
  {
    dst[out++] = zero_pad ? '0' : ' ';
  }
  for (uint32_t i = 0; i < (uint32_t)n && out + 1 < cap; i++)
  {
    dst[out++] = tmp[n - 1 - i];
  }
  dst[out] = 0;
  return out;
}

static uint32_t dbg_format_v(char *dst, uint32_t cap, const char *fmt, va_list ap)
{
  uint32_t out = 0;
  if (cap == 0)
  {
    return 0;
  }
  for (const char *f = fmt; *f != 0 && out + 1 < cap; f++)
  {
    if (*f != '%')
    {
      dst[out++] = *f;
      continue;
    }
    f++;
    if (*f == '%')
    {
      dst[out++] = '%';
      continue;
    }
    bool zero_pad = false;
    int width = 0;
    if (*f == '0')
    {
      zero_pad = true;
      f++;
    }
    while (*f >= '0' && *f <= '9')
    {
      width = width * 10 + (*f - '0');
      f++;
    }
    switch (*f)
    {
    case 's':
    {
      const char *s = va_arg(ap, const char *);
      while (*s != 0 && out + 1 < cap)
      {
        dst[out++] = *s++;
      }
      break;
    }
    case 'c':
    {
      int c = va_arg(ap, int);
      dst[out++] = (char)c;
      break;
    }
    case 'd':
    case 'u':
    {
      uint32_t v = va_arg(ap, uint32_t);
      if (*f == 'd' && (int)v < 0)
      {
        if (out + 1 < cap)
        {
          dst[out++] = '-';
        }
        v = (uint32_t)(-(int)v);
      }
      out += dbg_put_u32(dst + out, cap - out, v, 10, width, zero_pad);
      break;
    }
    case 'x':
    {
      uint32_t v = va_arg(ap, uint32_t);
      out += dbg_put_u32(dst + out, cap - out, v, 16, width, zero_pad);
      break;
    }
    default:
      dst[out++] = '?';
      break;
    }
  }
  dst[out] = 0;
  return out;
}

// Variadic convenience wrapper for direct call sites
static uint32_t dbg_format(char *dst, uint32_t cap, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  uint32_t n = dbg_format_v(dst, cap, fmt, ap);
  va_end(ap);
  return n;
}

static void dbg_str_cpy(char *dst, const char *src)
{
  while (*src != 0)
  {
    *dst++ = *src++;
  }
  *dst = 0;
}

static bool dbg_str_eq(const char *a, const char *b)
{
  while (*a != 0 && *a == *b)
  {
    a++;
    b++;
  }
  return *a == 0 && *b == 0;
}
//--------------------------------------------------------------------+
// Rendering
//--------------------------------------------------------------------+

// Draw the 18 newest log lines into the HUD's top-right slot. Uses the
// same calls (and the same draw_in_progress guard) as UpdateHUD() in
// src/hud.c; runs on the main routine thread, after that function's own
// per-frame update, so the console wins the frame while enabled.
static void dbg_render(void)
{
  int window_id = HUD_GetWindowId(DBG_SLOT);
  if (window_id == -1)
  {
    // The slot's window is closed (fade, too many windows, ...); the HUD
    // re-creates it on its own schedule and the console picks it up then.
    return;
  }
  draw_in_progress = true;
  ClearWindow(window_id);
  for (int i = 0; i < DBG_LINE_COUNT; i++)
  {
    if (s_lines[i][0] != 0)
    {
      DrawTextInWindow(window_id, 0, i * DBG_LINE_PITCH, s_lines[i]);
    }
  }
  UpdateWindow(window_id);
  draw_in_progress = false;
}

//--------------------------------------------------------------------+
// Log lines
//--------------------------------------------------------------------+

// Shift the visible lines down and append the new one (the newest line
// is always the last slot, rendered on the bottom row of the window).
static void dbg_append_line(const char *msg)
{
  for (int i = 0; i < DBG_LINE_COUNT - 1; i++)
  {
    for (int j = 0; j <= DBG_LINE_LEN; j++)
    {
      s_lines[i][j] = s_lines[i + 1][j];
    }
  }
  // No timestamp: with only 16 visible characters it would crowd the
  // message itself.
  dbg_str_cpy(s_lines[DBG_LINE_COUNT - 1], msg);
  s_last_line_ms = uplink_dbg_ms();
}

void uplink_dbg_log(const char *fmt, ...)
{
  char msg[DBG_LINE_LEN + 1];
  va_list ap;
  va_start(ap, fmt);
  dbg_format_v(msg, sizeof(msg), fmt, ap);
  va_end(ap);

  // Flood suppression: identical consecutive messages collapse into a
  // periodic "xN same" marker (the marker does not reset the repeat
  // count).
  if (s_last_msg[0] != 0 && dbg_str_eq(msg, s_last_msg))
  {
    s_repeat++;
    if (s_repeat == 10 || (s_repeat >= 100 && s_repeat % 100 == 0) ||
        (s_repeat >= 1000 && s_repeat % 1000 == 0))
    {
      char marker[DBG_LINE_LEN + 1];
      dbg_format(marker, sizeof(marker), "x%u same", s_repeat);
      dbg_append_line(marker);
    }
    return;
  }

  // Rate limit: at most one new line every DBG_MIN_LINE_GAP_MS.
  uint32_t now = uplink_dbg_ms();
  if (s_last_line_ms != 0 && now - s_last_line_ms < DBG_MIN_LINE_GAP_MS)
  {
    return;
  }

  dbg_append_line(msg);
  dbg_str_cpy(s_last_msg, msg);
  s_repeat = 0;
}

// Non rate-limited, non repeat-suppressed append for fast diagnostic
// bursts (see uplink_dbg_log_raw in the header). Runs on the main routine
// thread, the same as uplink_dbg_log, so the line buffers need no locking.
void uplink_dbg_log_raw(const char *fmt, ...)
{
  char msg[DBG_LINE_LEN + 1];
  va_list ap;
  va_start(ap, fmt);
  dbg_format_v(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  dbg_append_line(msg);
}

//--------------------------------------------------------------------+
// Public API
//--------------------------------------------------------------------+

void uplink_dbg_init(void)
{
  s_enabled = false;
  s_prev_lra = false;
  s_repeat = 0;
  s_last_line_ms = 0;
  for (int i = 0; i < DBG_LINE_COUNT; i++)
  {
    s_lines[i][0] = 0;
  }
  s_last_msg[0] = 0;
  uplink_dbg_log("uplink dbg ready");
}

void uplink_dbg_set_enabled(bool enabled)
{
  if (enabled == s_enabled)
  {
    return;
  }
  s_enabled = enabled;
  if (enabled)
  {
    uplink_dbg_log("console on");
  }
}

bool uplink_dbg_is_enabled(void)
{
  return s_enabled;
}

void uplink_dbg_poll(void)
{
  // L+R+A edge toggles the console (same edge-detect pattern as the HUD).
  // While enabled, dbg_render() redraws the HUD's top-right slot with the
  // 18 newest log lines (this runs after UpdateHUDSlots() in
  // MainRoutine, so the console wins the frame); toggling off redraws the
  // slot once with its normal strings.
  struct uplink_dbg_held_buttons b;
  GetHeldButtons(0, (void *)&b);
  bool lra = b.l && b.r && b.a;
  if (lra && !s_prev_lra)
  {
    if (s_enabled)
    {
      uplink_dbg_set_enabled(false);
      UpdateHUD(HUD_SLOT_TOP_RIGHT);
    }
    else
    {
      uplink_dbg_set_enabled(true);
    }
  }
  s_prev_lra = lra;

  if (s_enabled)
  {
    dbg_render();
  }
}

#endif // UPLINK_DBG_ENABLED