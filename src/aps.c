// Implements the APS component for the speedrun info HUD.
// APS is defined as number of actions the player makes dividied by
// the number of seconds where the player is idle.

#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"
#include "timer.h"
#include "speedrun_hud.h"
#include "aps.h"

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

void UpdateAPS(void) {
  if (IsLagging()) {
    return;
  }
  if (current_aps_split.remaining_frames > 0) {
    UpdateHUDString(SPEEDRUN_HUD_APS, current_aps_split.string, OFFSET);
    // Technically race condition if remaining_frames gets set to 0 in between
    // but in reality this just turns it negative, which is also less than or equal to 0
    current_aps_split.remaining_frames--;
  }
  if (!OverlayIsLoaded(OGROUP_OVERLAY_29)) {
    aps.idle_time = 0;
    aps.actions = 0;
    if (current_aps_split.remaining_frames <= 0) {
      UpdateHUDString(SPEEDRUN_HUD_APS, "", OFFSET);
    }
    return;
  }

  // Scuffed ram search for whether menu is open
  int* menu_open_aps = (int*) 0x20afad0;

  char aps_color[HUD_LEN] = "";
  // Note that we are making an assumption here that there is always enough time to run the routine while we are idle
  if (aps.message_log_pause) {
    aps.idle_time++;
    strncat(aps_color, PAUSE_SKIP_COLOR_TAG, HUD_LEN);
  } else if (DUNGEON_PTR_MASTER->no_action_in_progress && *menu_open_aps == 0 && GetLeaderAction()->val == ACTION_NOTHING) {
    aps.prev_action = ACTION_NOTHING;
    aps.idle_time++;
    strncat(aps_color, IDLE_COLOR_TAG, HUD_LEN);
  }

  uint64_t aps_divided = _u32_div_f(aps.actions * 60, aps.idle_time);
  uint32_t aps_quotient = aps_divided & 0xFFFF;
  uint32_t aps_remainder = aps_divided >> 32;
  uint64_t remainder_divided = _u32_div_f(aps_remainder * 10, aps.idle_time);
  uint32_t aps_decimal = remainder_divided & 0xFFFF;

  char aps_string[HUD_LEN];

  snprintf(aps_string, HUD_LEN, "%s%d.%01d aps", aps_color, aps_quotient, aps_decimal);

  if (current_aps_split.remaining_frames <= 0) {
    snprintf(current_aps_split.string, HUD_LEN, "%s%d.%01d aps", SPLIT_COLOR_TAG, aps_quotient, aps_decimal);
    UpdateHUDString(SPEEDRUN_HUD_APS, aps_string, OFFSET);
  }
}