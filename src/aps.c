// Implements the FPS component for the speedrun info HUD.
// FPS is defined as frames where the IGT advances divided by frames where the RNG advances.

#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"
#include "timer.h"
#include "speedrun_hud.h"

#define OFFSET 38

uint32_t idle_time = 0;
uint32_t actions = 0;

// Count the action when SetLeaderAction() returns, i.e, when the player has inputted an action
__attribute__((used)) void HijackSetLeaderActionAndCountAction(void) {
  SetLeaderAction();
  actions++;
} 

void UpdateAPS(void) {
  if (IsLagging()) {
    return;
  }
  if (!OverlayIsLoaded(OGROUP_OVERLAY_29)) {
    idle_time = 0;
    actions = 0;
    return;
  }
  if (DUNGEON_PTR_MASTER->no_action_in_progress) {
    idle_time++;
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