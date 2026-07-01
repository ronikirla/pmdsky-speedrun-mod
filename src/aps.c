// Implements the APS component for the speedrun info HUD.
// APS is defined as number of actions the player makes dividied by
// the number of seconds where the player is idle.

#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"
#include "timer.h"
#include "speedrun_hud.h"
#include "aps.h"
#include "fps.h"
#include "eeprom.h"

#define OFFSET 38

#define IDLE_COLOR_TAG "[CS:A]"
#define PAUSE_SKIP_COLOR_TAG "[CS:B]"

struct aps {
  uint32_t idle_time;
  uint32_t actions; // Shared resource
  enum action prev_action; // Shared resource
  bool running_prevent_aps_count;
  bool message_log_pause; // Shared resource
};

struct aps aps;

// Double-tap Start to show idle seconds instead of APS ratio
bool show_idle_seconds = false;

#define DOUBLE_TAP_WINDOW 30 // Frames within which a second tap counts as double-tap

struct aps_split current_aps_split;

void ResetAPS(void) {
  aps.idle_time = 1;
  aps.actions = 0;
  current_aps_split.remaining_frames = 0;
}

void ResetAPSRemainingFrames(void) {
  current_aps_split.remaining_frames = SPLIT_SHOW_FRAMES;
}

struct aps_split* GetCurrentAPSSplit(void) {
  return &current_aps_split;
}

bool GetShowIdleSeconds(void) {
  return show_idle_seconds;
}

void SetShowIdleSeconds(bool val) {
  show_idle_seconds = val;
}

// Count the action when SetLeaderAction() returns, i.e, when the player has inputted an action
__attribute__((used)) void HijackSetLeaderActionAndCountAction(void) {
  SetLeaderAction();
  enum action curr_action = GetLeaderAction()->val;
  // Don't count instances where the action is caused by buffering or being locked to a dash
  bool separate_input_action = curr_action == ACTION_PASS_TURN
                            || (curr_action == ACTION_WALK && !DUNGEON_PTR_MASTER->leader_running)
                            || aps.running_prevent_aps_count;
  if (aps.prev_action != curr_action || !separate_input_action) {
    aps.actions++;
    current_aps_split.remaining_frames = 0;
  }
  aps.prev_action = curr_action;
}

__attribute__((used)) bool HijackShouldLeaderKeepRunningAndPreventCount(void) {
  aps.running_prevent_aps_count = ShouldLeaderKeepRunning();
  return aps.running_prevent_aps_count;
}

// Detect missed pause skips, copy pasted decomp with mostly unknown variables
__attribute__((used)) void CustomMessageLogPauseLoop(undefined4 param_1, undefined4 message_log_struct, undefined2* message_log_inputs) {
  int i = 0;
  aps.message_log_pause = true;
  while( true ) {
    if (0xef < i) {
      break;
    }
    if (*(short *)(message_log_struct + 0x90) < 0xb4) {
      break;
    }
    if ((*message_log_inputs & 3) == 3) {
      break;
    }
    if ((message_log_inputs[1] & 0xf0) != 0) break;
    AdvanceFrame(param_1);
    i = i + 1;
  }
  aps.message_log_pause = false;
}

void HandleAPSInput(void) {
  static bool prev_held_start_aps = false;
  static int32_t frames_since_last_start_press = -999; // Initialize so first press can't double-tap
  
  struct held_buttons held_buttons;
  GetHeldButtons(0, (void*) &held_buttons);

  // Detect start button press edge (pressed this frame, not pressed last frame)
  bool start_pressed_edge = held_buttons.start && !prev_held_start_aps;
  prev_held_start_aps = held_buttons.start;

  if (start_pressed_edge) {
    if (frames_since_last_start_press >= 0 && frames_since_last_start_press < DOUBLE_TAP_WINDOW) {
      // Double-tap detected, toggle idle seconds display
      show_idle_seconds = !show_idle_seconds;
      frames_since_last_start_press = -999; // Reset after double-tap
      SaveConfigurations();
    } else {
      // Single press, start counting frames for potential double-tap
      frames_since_last_start_press = 0;
    }
  }

  // Increment frame counter if waiting for potential double-tap
  if (frames_since_last_start_press >= 0) {
    frames_since_last_start_press++;
  }
}

void UpdateAPS(void) {
  static bool was_in_dungeon = false;

  if (!DUNGEON_PTR_MASTER && !OverlayIsLoaded(OGROUP_OVERLAY_1)) {
    return;
  }

  int remaining_frames = current_aps_split.remaining_frames;
  if (remaining_frames > 0) {
    // Technically race condition if remaining_frames gets set to 0 in between
    // but in reality this just turns it negative, which is also less than or equal to 0
    current_aps_split.remaining_frames--;
  }

  bool in_dungeon = OverlayIsLoaded(OGROUP_OVERLAY_29);

  // Detect dungeon exit: back up values and reset immediately
  if (was_in_dungeon && !in_dungeon) {
    current_aps_split.idle_time = aps.idle_time;
    current_aps_split.actions = aps.actions;
    aps.idle_time = 0;
    aps.actions = 0;
  }
  was_in_dungeon = in_dungeon;

  // If not in dungeon and no split remaining, clear and exit
  if (!in_dungeon && remaining_frames <= 0) {
    UpdateHUDString(SPEEDRUN_HUD_APS, "", OFFSET);
    return;
  }

  // Determine which values to display
  uint32_t display_idle_time;
  uint32_t display_actions;

  if (in_dungeon) {
    display_idle_time = aps.idle_time;
    display_actions = aps.actions;
  } else {
    // During split display, use backed up values
    display_idle_time = current_aps_split.idle_time;
    display_actions = current_aps_split.actions;
  }

  // Scuffed ram search for whether menu is open
  int* menu_open_aps = (int*) 0x20afad0;

  char aps_color[HUD_LEN] = "";

  if (in_dungeon) {
    if (aps.message_log_pause) {
      aps.idle_time++;
      strncat(aps_color, PAUSE_SKIP_COLOR_TAG, HUD_LEN);
    } else if (DUNGEON_PTR_MASTER->no_action_in_progress && *menu_open_aps == 0 && GetLeaderAction()->val == ACTION_NOTHING) {
      aps.prev_action = ACTION_NOTHING;
      aps.idle_time++;
      strncat(aps_color, IDLE_COLOR_TAG, HUD_LEN);
    }
  } else {
    // During split display outside dungeon, use split color
    strncat(aps_color, SPLIT_COLOR_TAG, HUD_LEN);
  }

  char aps_string[HUD_LEN];

  if (show_idle_seconds) {
    // Show idle seconds with one decimal place
    uint64_t seconds_divided = _u32_div_f(display_idle_time, 60);
    uint32_t idle_seconds = seconds_divided & 0xFFFF;
    uint32_t idle_frames_remainder = seconds_divided >> 32;
    uint64_t decimal_divided = _u32_div_f(idle_frames_remainder * 10, 60);
    uint32_t idle_decimal = decimal_divided & 0xFFFF;

    snprintf(aps_string, HUD_LEN, "%s%d.%01d s", aps_color, idle_seconds, idle_decimal);
  } else {
    // Show normal APS ratio
    uint64_t aps_divided = _u32_div_f(display_actions * 60, display_idle_time);
    uint32_t aps_quotient = aps_divided & 0xFFFF;
    uint32_t aps_remainder = aps_divided >> 32;
    uint64_t remainder_divided = _u32_div_f(aps_remainder * 10, display_idle_time);
    uint32_t aps_decimal = remainder_divided & 0xFFFF;

    snprintf(aps_string, HUD_LEN, "%s%d.%01d aps", aps_color, aps_quotient, aps_decimal);
  }

  UpdateHUDString(SPEEDRUN_HUD_APS, aps_string, OFFSET);
}
