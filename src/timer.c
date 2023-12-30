// Implements the timer component for the speedrun info HUD

#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"
#include "hud.h"
#include "speedrun_hud.h"

#define CHAR_WIDTH 6
#define DEFAULT_COL 10
#define SPLIT_SHOW_FRAMES 600
#define SPLIT_COLOR_TAG "[CS:G]"

uint8_t hundredths_lookup[60] = {0, 1, 3, 5, 6, 8, 10, 11, 13, 15, 16, 18, 20, 21, 23, 25, 26, 28, 30, 31, 33, 35, 36, 38, 40, 41, 43, 45, 46, 48, 50, 51, 53, 55, 56, 58, 60, 61, 63, 65, 66, 68, 70, 71, 73, 75, 76, 78, 80, 81, 83, 85, 86, 88, 90, 91, 93, 95, 96, 98};

bool file_timer = true;
struct play_time start_time;

uint8_t last_frame;
uint8_t curr_frame;

struct split {
  char string[HUD_LEN];
  int x_offset;
  int remaining_frames;
};

struct split current_split;

bool prev_held_timer = false;

// Return true if the play time has not advanced and we are not in the main menu
bool IsLagging() {
  return (curr_frame == last_frame) && !OverlayIsLoaded(OGROUP_OVERLAY_1);
}

// Return difference a - b in frames in play_time structs
int IGTDifferenceFrames(struct play_time* a, struct play_time* b) {
  return (a->seconds - b->seconds) * 60 + (a->frames - b->frames);
}

// Return difference a - b in play_time structs
struct play_time* IGTDifference(struct play_time* a, struct play_time* b) {
  struct play_time* diff = MemAlloc(sizeof(struct play_time), 0);
  int seconds = a->seconds - b->seconds;
  int frames = a->frames - b->frames;
  if (frames < 0) {
    frames += 60;
    seconds -= 1;
  }
  diff->seconds = seconds;
  diff->frames = frames;
  return diff;
}

__attribute__((used)) void ResetSplitRemainingFrames(void) {
  current_split.remaining_frames = SPLIT_SHOW_FRAMES;
}

__attribute__((used)) void HijackCalcChecksumAndSplit(undefined* save_info, int size) {
  current_split.remaining_frames = 0;
  CalcChecksum(save_info, size);
}

// Start+L for reset toggle, Select+L to show split time
void HandleTimerInput(void) {
  struct held_buttons held_buttons;
  GetHeldButtons(0, &held_buttons);
  if (held_buttons.start && held_buttons.l) {
    if (!prev_held_timer) {
      prev_held_timer = true;
      if (file_timer) {
        struct play_time* igt = (struct play_time*) &PLAY_TIME_SECONDS;
        memcpy(&start_time, igt, sizeof(struct play_time));
        file_timer = false;
      } else {
        memset(&start_time, 0, sizeof(struct play_time));
        file_timer = true;
      }
    }
  } else if (held_buttons.select && held_buttons.l) {
    if (!prev_held_timer) {
      prev_held_timer = true;
      ResetSplitRemainingFrames();
    }
  } else {
    prev_held_timer = false;
  }
}

void UpdateTimer(void) {
  struct play_time* igt = (struct play_time*) &PLAY_TIME_SECONDS;
  // Optimization to avoid all these costly operations during lag
  last_frame = curr_frame;
  curr_frame = igt->frames;
  if (IsLagging()) {
    return;
  }

  struct play_time* run_igt = IGTDifference(igt, &start_time);
  
  uint8_t run_hundreths = hundredths_lookup[run_igt->frames];
  uint64_t seconds_divided = _s32_div_f(run_igt->seconds, 60);
  uint8_t run_seconds = seconds_divided >> 32;
  uint32_t run_minutes = seconds_divided & 0xFFFF;
  uint64_t minutes_divided = _s32_div_f(run_minutes, 60);
  run_minutes = minutes_divided >> 32;
  uint16_t run_hours = minutes_divided & 0xFFFF;

  // Adjust x_offset based on the length of the text
  int x_offset = DEFAULT_COL;
    if (run_hours < 10) {
      x_offset += 3 * CHAR_WIDTH;
    } else if (run_hours < 100) {
      x_offset += 2 * CHAR_WIDTH;
    } else if (run_hours < 1000) {
      x_offset += CHAR_WIDTH;
    }

  char time_string[HUD_LEN];
  snprintf(time_string, HUD_LEN, "%01d:%02d:%02d.%02d", run_hours, run_minutes, run_seconds, run_hundreths);

  if (current_split.remaining_frames == SPLIT_SHOW_FRAMES) {
    snprintf(current_split.string, HUD_LEN, "%s%s", SPLIT_COLOR_TAG, time_string);
    current_split.x_offset = x_offset;
  }

  if (current_split.remaining_frames > 0) {
    UpdateHUDString(SPEEDRUN_HUD_TIMER, current_split.string, current_split.x_offset);
    current_split.remaining_frames--;
  } else {
    UpdateHUDString(SPEEDRUN_HUD_TIMER, time_string, x_offset);
  }

  MemFree(run_igt);
}
