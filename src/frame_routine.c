#include <pmdsky.h>
#include <cot.h>
#include "speedrun_hud.h"
#include "timer.h"
#include "fps.h"
#include "aps.h"
#include "input_display.h"

// This function gets called every frame on RNG advance, can be used
// to perform arbitrary frame-based routines
__attribute__((used)) void FrameRoutine(void) {
  HandleHUDToggle();
  HandleTimerInput();
  UpdateTimer();
  UpdateFPS();
  UpdateAPS();
  UpdateInputDisplay();
  UpdateHUDSlots();
}