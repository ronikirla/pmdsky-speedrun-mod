// This file contains optimizations to make the game run faster

#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"
#include "timer.h"
#include "optimizations.h"
#include "aps.h"

enum optimization_mode optimization_mode = OPTIMIZATION_MODE_DEFAULT; // Shared resource, but only gets written to in main menu
bool prev_held_opt = false;

// In the main menu, let the player choose the level of optimization used during gameplay
void HandleSpeedToggle(void) {
  if (IsLagging() || !OverlayIsLoaded(OGROUP_OVERLAY_1)) {
    return;
  }

  struct held_buttons held_buttons;
  GetHeldButtons(0, (void*) &held_buttons);

  // 0 = None, -1 = Left (Decrement), 1 = Right (Increment)
  int direction = 0;
  if (held_buttons.start) {
    if (held_buttons.left)      direction = -1;
    else if (held_buttons.right) direction = 1;
  }

  if (direction != 0) {
    if (prev_held_opt) {
      return; 
    }

    prev_held_opt = true;
    optimization_mode = (optimization_mode + direction + 4) % 4;
    
    return;
  }

  prev_held_opt = false;
}

enum optimization_mode GetOptimizationMode() {
  return optimization_mode;
}

char* GetOptimizationModeString(void) {
  if (GetCurrentAPSSplit()->remaining_frames > 0)
    return "";
  char* optimization_mode_strings[] = {"Throttle mode", "Normal mode", "Fast mode", "RNG viewer mode"};
  return optimization_mode_strings[optimization_mode];
}

// If optimizations are enabled, skip waiting for VCount 0
__attribute__((used)) void SkipVCount0Wait(void* param_1) {
  if (optimization_mode == OPTIMIZATION_MODE_FAST || optimization_mode == OPTIMIZATION_MODE_RNG_VIEWER)
    return;
  ReceiveMessageWithHighPrio(param_1);
}

// Unless optimizations are explicitly disabled, skip cart reads during AI calcs.
// Note that even throttle mode gets optimized here, so we can manually add a
// platform-independent amount of lag
__attribute__((used)) void SkipAICardRead(int string_id, struct entity* entity) {
  switch (optimization_mode) {
    case OPTIMIZATION_MODE_THROTTLE:
      OS_Sleep(1);
      return;
    case OPTIMIZATION_MODE_DEFAULT:
      SubstitutePlaceholderStringTags(0, entity, 0);
      return;
    case OPTIMIZATION_MODE_FAST:
      return;
    default:
  }
}

__attribute__((used)) void SubstitutePlaceholderStringTagsAndLogMessageByIdWithPopupCheckUser(
  struct entity* entity, int message_id
) {
  // If we optimized the card read out, we need to still perform it when it's actually necessary
  if (optimization_mode != OPTIMIZATION_MODE_DEFAULT) {
    SubstitutePlaceholderStringTags(0, entity, 0);
  }
  LogMessageByIdWithPopupCheckUser(entity, message_id);
}