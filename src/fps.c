// Implements the FPS component for the speedrun info HUD.
// FPS is defined as frames where the IGT advances divided by frames where the RNG advances.

#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"
#include "timer.h"
#include "hud.h"
#include "speedrun_hud.h"
#include "optimizations.h"

#define MONITORING_WINDOW 15
#define MULTIPLIER_TO_FPS 4

struct play_time prev_frames[MONITORING_WINDOW];
bool is_prev_frames_filled = false; // Shared resource
int idx = 0;
int fps; // Shared resource

void UpdateFPS(void) {
  if (IsLagging()) {
    return;
  }
  // This never gets changed back to false so it's thread safe
  if (is_prev_frames_filled) {
    char fps_string[HUD_LEN];
    // In the main menu, instead of printing FPS we print the optimization mode
    if (OverlayIsLoaded(OGROUP_OVERLAY_1)) {
      snprintf(fps_string, HUD_LEN, GetOptimizationModeString()); 
    } else {
      // fps: atomic access, thread safe
      snprintf(fps_string, HUD_LEN, "%d fps", fps); 
    }
    UpdateHUDString(SPEEDRUN_HUD_FPS, fps_string, 2);
  }
}

void CalculateFPS(void) {
  struct play_time* igt = (struct play_time*) &PLAY_TIME_SECONDS;
  if (is_prev_frames_filled) {
    fps = IGTDifferenceFrames(igt, &prev_frames[idx]) * MULTIPLIER_TO_FPS;
  } else {
    is_prev_frames_filled = (idx == MONITORING_WINDOW - 1);
  }
  memcpy(&prev_frames[idx], igt, sizeof(*igt));
  idx = (idx + 1) % MONITORING_WINDOW;
}