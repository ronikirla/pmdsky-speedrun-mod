// This file contains optimizations to make the game run faster

#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"
#include "optimizations.h"
#include "aps.h"
#include "fps.h"
#include "eeprom.h"

enum optimization_mode optimization_mode = OPTIMIZATION_MODE_DEFAULT; // Shared resource, but only gets written to in main menu
bool prev_held_opt = false;

// In the main menu, let the player choose the level of optimization used during gameplay
void HandleSpeedToggle(void)
{
  if (IsLagging() || !OverlayIsLoaded(OGROUP_OVERLAY_1))
  {
    return;
  }

  struct held_buttons held_buttons;
  GetHeldButtons(0, (void *)&held_buttons);

  // 0 = None, -1 = Left (Decrement), 1 = Right (Increment)
  int direction = 0;
  if (held_buttons.start)
  {
    if (held_buttons.left)
      direction = -1;
    else if (held_buttons.right)
      direction = 1;
  }

  if (direction != 0)
  {
    if (prev_held_opt)
    {
      return;
    }

    prev_held_opt = true;
    optimization_mode = (optimization_mode + direction + 4) % 4;

    SaveConfigurations();

    return;
  }

  prev_held_opt = false;
}

enum optimization_mode GetOptimizationMode()
{
  return optimization_mode;
}

char *GetOptimizationModeString(void)
{
  if (GetCurrentAPSSplit()->remaining_frames > 0)
    return "";
  char *optimization_mode_strings[] = {"Throttle mode", "Normal mode", "Fast mode", "RNG viewer mode"};
  return optimization_mode_strings[optimization_mode];
}

bool skipped_g3x_sleep = false;
__attribute__((used)) bool CustomWaitTillVBlank(void)
{
  switch (optimization_mode)
  {
  case OPTIMIZATION_MODE_FAST:
  case OPTIMIZATION_MODE_RNG_VIEWER:
    // Check if blanking, exit early if so. This lets the game draw the graphics and finish the
    // frame in time even if it took slightly over 75% of the frame time to do all the other processing.
    // However, we don't want to do this on consecutive frames, because this results in weird bugs.
    if (GX_IsVBlank() && !skipped_g3x_sleep && OverlayIsLoaded(OGROUP_OVERLAY_29))
    {
      skipped_g3x_sleep = true;
      return 1;
    }
    skipped_g3x_sleep = false;
  default:
    // Original decompiled code
    bool uVar1;
    bool *puVar2;
    bool auStack_10[8];
    extern bool *DAT_02003aac;

    CallsChangeThreadPriority(auStack_10, 0xb);
    puVar2 = DAT_02003aac;
    if (DAT_02003aac[10] == '\0')
    {
      DAT_02003aac[9] = 1;
      OS_SleepThread((void *)0x0);
      DAT_02003aac[9] = 0;
    }
    else
    {
      DAT_02003aac[10] = 0;
      puVar2[7] = 1;
      OS_SleepThread((void *)0x0);
    }
    uVar1 = *DAT_02003aac;
    FUN_0200265c(auStack_10);
    return uVar1;
  }
}

// Unless optimizations are explicitly disabled, skip cart reads during AI calcs.
// Note that even throttle mode gets optimized here, so we can manually add a
// platform-independent amount of lag
__attribute__((used)) void SkipAICardRead(int string_id, struct entity *entity)
{
  switch (optimization_mode)
  {
  case OPTIMIZATION_MODE_THROTTLE:
    OS_Sleep(1);
    return;
  case OPTIMIZATION_MODE_DEFAULT:
    SubstitutePlaceholderStringTags(0, entity, 0);
    return;
  default:
    return;
  }
}

__attribute__((used)) void SubstitutePlaceholderStringTagsAndLogMessageByIdWithPopupCheckUser(
    struct entity *entity, int message_id)
{
  // If we optimized the card read out, we need to still perform it when it's actually necessary
  switch (optimization_mode)
  {
  case OPTIMIZATION_MODE_THROTTLE:
  case OPTIMIZATION_MODE_FAST:
  case OPTIMIZATION_MODE_RNG_VIEWER:
    SubstitutePlaceholderStringTags(0, entity, 0);
  default:
  }
  LogMessageByIdWithPopupCheckUser(entity, message_id);
}