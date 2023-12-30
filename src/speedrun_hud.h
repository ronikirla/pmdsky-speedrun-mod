#pragma once

#include "hud.h"

enum speedrun_hud_string_type {
  SPEEDRUN_HUD_TIMER = 0,
  SPEEDRUN_HUD_SEED = 1,
  SPEEDRUN_HUD_FPS = 2,
  SPEEDRUN_HUD_INPUT_DISPLAY = 3
};

// Update the specified speedrun HUD string
void UpdateHUDString(enum speedrun_hud_string_type shst, char* str, uint8_t x_offset);

// Read inputs to switch between HUD modes
void HandleHUDToggle(void);