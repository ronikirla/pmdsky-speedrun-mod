#pragma once

#include "hud.h"

enum speedrun_hud_string_type {
  SPEEDRUN_HUD_TIMER,
  SPEEDRUN_HUD_SEED,
  SPEEDRUN_HUD_FPS,
  SPEEDRUN_HUD_APS,
  SPEEDRUN_HUD_INPUT_DISPLAY
};

// Update the specified speedrun HUD string
void UpdateHUDString(enum speedrun_hud_string_type shst, char* str, uint8_t x_offset);

// Update the HUD slots at the end of every frame
void UpdateHUDSlots(void);

// Read inputs to switch between HUD modes
void HandleHUDToggle(void);