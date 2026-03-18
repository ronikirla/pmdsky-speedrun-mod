// Implements the FPS component for the speedrun info HUD.
// FPS is defined as frames where the IGT advances divided by frames where the RNG advances.

#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"
#include "timer.h"
#include "hud.h"
#include "speedrun_hud.h"

#define MONITORING_WINDOW 15
#define MULTIPLIER_TO_FPS 4

struct play_time prev_frames[MONITORING_WINDOW];
bool is_prev_frames_filled = false;
int idx = 0;

void UpdateFPS(void) {
  struct play_time* igt = (struct play_time*) &PLAY_TIME_SECONDS;
  if (is_prev_frames_filled) {
    int fps = IGTDifferenceFrames(igt, &prev_frames[idx]) * MULTIPLIER_TO_FPS;
    char fps_string[HUD_LEN];
    // Don't print fps in main menu because it doesn't work when the timer isn't running
    if (OverlayIsLoaded(OGROUP_OVERLAY_1)) {
      snprintf(fps_string, HUD_LEN, ""); 
    } else {
      snprintf(fps_string, HUD_LEN, "%d fps", fps); 
    }
    UpdateHUDString(SPEEDRUN_HUD_FPS, fps_string, 2);
  } else {
    is_prev_frames_filled = (idx == MONITORING_WINDOW - 1);
  }

  memcpy(&prev_frames[idx], igt, sizeof(*igt));
  idx = (idx + 1) % MONITORING_WINDOW;
}