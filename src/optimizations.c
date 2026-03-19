// This file contains optimizations to make the game run faster

#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"
#include "timer.h"
#include "optimizations.h"

enum optimization_mode optimization_mode = OPTIMIZATION_MODE_DEFAULT;
bool prev_held_opt = false;

__attribute__((used)) void SubstitutePlaceholderStringTagsAndLogMessageByIdWithPopupCheckUser(
  struct entity* entity, int message_id
) {
  SubstitutePlaceholderStringTags(0, entity, 0);
  LogMessageByIdWithPopupCheckUser(entity, message_id);
}

// In the main menu, let the player choose the level of optimization used during gameplay
void HandleSpeedToggle(void) {
  if (IsLagging() || !OverlayIsLoaded(OGROUP_OVERLAY_1)) {
    return;
  }
  struct held_buttons held_buttons;
  GetHeldButtons(0, (undefined*) &held_buttons);
  if (held_buttons.start) {
    if (held_buttons.left) {
      if (prev_held_opt) {
        return;
      }
      prev_held_opt = true;
      optimization_mode = (optimization_mode + 3 - 1) % 3;
      return;
    } else if (held_buttons.right) {
      if (prev_held_opt) {
        return;
      }
      prev_held_opt = true;
      optimization_mode = (optimization_mode + 1) % 3;
      return;
    }
  }
  prev_held_opt = false;
}

enum optimization_mode GetOptimizationMode() {
  return optimization_mode;
}

char* GetOptimizationModeString(void) {
  char* optimization_mode_strings[] = {"Throttle mode", "Normal mode", "Fast mode"};
  return optimization_mode_strings[optimization_mode];
}