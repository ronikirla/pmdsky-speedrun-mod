// Implements the FPS component for the speedrun info HUD.
// FPS is defined as frames where the IGT advances divided by frames where the RNG advances.

#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"
#include "timer.h"
#include "speedrun_hud.h"

#define OFFSET 40

void UpdateAPS(void) {
  if (IsLagging()) {
    return;
  }
  UpdateHUDString(SPEEDRUN_HUD_APS, "69 aps", OFFSET);
}