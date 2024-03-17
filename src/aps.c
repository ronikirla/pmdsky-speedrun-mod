// Implements the APS component for the speedrun info HUD.
// APS is defined as number of actions the player makes dividied by
// the number of seconds where the player is idle.

#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"
#include "timer.h"
#include "speedrun_hud.h"

#define OFFSET 38

uint32_t idle_time = 0;
uint32_t actions = 0;
enum action prev_action = ACTION_NOTHING;
bool prevent_aps_count = false;

// Scuffed ram search for whether menu is open
int* menu_open_aps = 0x20afad0;

// Count the action when SetLeaderAction() returns, i.e, when the player has inputted an action
__attribute__((used)) void HijackSetLeaderActionAndCountAction(void) {
  SetLeaderAction();
  enum action curr_action = GetLeaderAction()->val;
  // Don't count instances where the action is caused by buffering or being locked to a dash
  bool separate_input_action = curr_action == ACTION_PASS_TURN
                         || curr_action == ACTION_WALK && !DUNGEON_PTR_MASTER->leader_running
                         || prevent_aps_count;
  if (prev_action != curr_action || !separate_input_action) {
    actions++;
  }
  prev_action = curr_action;
}

__attribute__((used)) bool HijackShouldLeaderKeepRunningAndPreventCount(void) {
  prevent_aps_count = ShouldLeaderKeepRunning();
  return prevent_aps_count;
}

void UpdateAPS(void) {
  if (IsLagging()) {
    return;
  }
  if (!OverlayIsLoaded(OGROUP_OVERLAY_29)) {
    idle_time = 0;
    actions = 0;
    UpdateHUDString(SPEEDRUN_HUD_APS, "", OFFSET);
    return;
  }
  if (GetLeaderAction()->val == ACTION_NOTHING && *menu_open_aps == 0) {
    idle_time++;
    prev_action = ACTION_NOTHING;
  }

  uint64_t aps_divided = _u32_div_f(actions * 60, idle_time);
  uint32_t aps_quotient = aps_divided & 0xFFFF;
  uint32_t aps_remainder = aps_divided >> 32;
  uint64_t remainder_divided = _u32_div_f(aps_remainder * 10, idle_time);
  uint32_t aps_decimal = remainder_divided & 0xFFFF;

  char aps_string[HUD_LEN];
  snprintf(aps_string, HUD_LEN, "%d.%01d aps", aps_quotient, aps_decimal);
  UpdateHUDString(SPEEDRUN_HUD_APS, aps_string, OFFSET);
}