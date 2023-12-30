#pragma once

#define HUD_LEN 1024

enum hud_slot {
  HUD_SLOT_NULL = -1,
  HUD_SLOT_TOP_LEFT = 0,
  HUD_SLOT_TOP_RIGHT = 1,
  HUD_SLOT_BOTTOM = 2
};

// Refreshes the window with the current strings behind the string pointers
void UpdateHUD(enum hud_slot slot);

// Set the pointer and the position of a specified string in a specified HUD slot
void AssignHUDSlot(enum hud_slot slot, int string_idx, char* string_pointer, uint8_t* x_offset);

// Set the string pointers of every HUD slot to null
void ClearHUDSlots(void);