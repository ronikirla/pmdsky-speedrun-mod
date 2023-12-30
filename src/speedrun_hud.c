// HUD implementation that contains useful information for speedrunners, such as a live file timer

#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"
#include "speedrun_hud.h"
#include "hud.h"
#include "fixed_rng.h"

#define TIMER_SLOT_MINIMAL HUD_SLOT_TOP_RIGHT
#define TIMER_SLOT_MINIMAL_STRING_IDX 0
#define SEED_SLOT_MINIMAL HUD_SLOT_TOP_LEFT
#define SEED_SLOT_MINIMAL_STRING_IDX 0

#define TIMER_SLOT_MAXIMAL HUD_SLOT_BOTTOM
#define TIMER_SLOT_MAXIMAL_STRING_IDX 0
#define FPS_SLOT_MAXIMAL HUD_SLOT_BOTTOM
#define FPS_SLOT_MAXIMAL_STRING_IDX 1
#define INPUT_DISPLAY_SLOT_MAXIMAL HUD_SLOT_BOTTOM
#define INPUT_DISPLAY_SLOT_MAXIMAL_STRING_IDX 2
#define SEED_SLOT_MAXIMAL HUD_SLOT_TOP_RIGHT
#define SEED_SLOT_MAXIMAL_STRING_IDX 0

#define TIMER_BOTTOM_X_OFFSET 175

enum hud_display_mode {
  HUD_DISPLAY_NONE = 0,
  HUD_DISPLAY_MINIMAL = 1,
  HUD_DISPLAY_MAXIMAL = 2
};

enum hud_display_mode hud_display_mode = HUD_DISPLAY_NONE;

struct speedrun_hud_string {
  char string[HUD_LEN];
  uint8_t base_x_offset; // This can be set in here to different values depending on slot
  uint8_t x_offset;      // This gets passed from the HUD component
};

struct speedrun_hud_strings {
  struct speedrun_hud_string timer;
  struct speedrun_hud_string seed;
  struct speedrun_hud_string fps;
  struct speedrun_hud_string input_display;
};
struct speedrun_hud_strings speedrun_hud_strings;

bool prev_held_srh = false;

bool hud_slot_needs_update[HUD_SLOTS];

// Update the specified speedrun HUD string
void UpdateHUDString(enum speedrun_hud_string_type shst, char* str, uint8_t x_offset) {
  struct speedrun_hud_string* timer = &speedrun_hud_strings.timer;
  struct speedrun_hud_string* seed = &speedrun_hud_strings.seed;
  struct speedrun_hud_string* fps = &speedrun_hud_strings.fps;
  struct speedrun_hud_string* input_display = &speedrun_hud_strings.input_display;
  char* loc;
  enum hud_slot slot_minimal;
  enum hud_slot slot_maximal;
  switch(shst) {
    case SPEEDRUN_HUD_TIMER:
      loc = timer->string;
      timer->x_offset = x_offset + timer->base_x_offset;
      slot_minimal = TIMER_SLOT_MINIMAL;
      slot_maximal = TIMER_SLOT_MAXIMAL;
      break;
    case SPEEDRUN_HUD_SEED:
      loc = seed->string;
      seed->x_offset = x_offset + seed->base_x_offset;
      slot_minimal = SEED_SLOT_MINIMAL;
      slot_maximal = SEED_SLOT_MAXIMAL;
      break;
    case SPEEDRUN_HUD_FPS:
      loc = fps->string;
      fps->x_offset = x_offset + fps->base_x_offset;
      slot_minimal = HUD_SLOT_NULL;
      slot_maximal = FPS_SLOT_MAXIMAL;
      break;
    case SPEEDRUN_HUD_INPUT_DISPLAY:
      loc = input_display->string;
      input_display->x_offset = x_offset + input_display->base_x_offset;
      slot_minimal = HUD_SLOT_NULL;
      slot_maximal = INPUT_DISPLAY_SLOT_MAXIMAL;
      break;
    default:
      return;
  }
  strncpy(loc, str, HUD_LEN);
  // Update the HUD slot that corresponds to the currently active mode
  switch(hud_display_mode) {
    case HUD_DISPLAY_NONE:
      break;
    case HUD_DISPLAY_MINIMAL:
      if (slot_minimal != HUD_SLOT_NULL) {
        hud_slot_needs_update[slot_minimal] = true;
      }
      break;
    case HUD_DISPLAY_MAXIMAL:
      if (slot_maximal != HUD_SLOT_NULL) {
        hud_slot_needs_update[slot_maximal] = true;
      }
      break;
    default:
      break;
  }
}

// Update the HUD slots at the end of every frame
void UpdateHUDSlots(void) {
  for (int i = 0; i < HUD_SLOTS; i++) {
    if (hud_slot_needs_update[i]) {
      hud_slot_needs_update[i] = false;
      UpdateHUD((enum hud_slot) i);
    }
  }
}

// Assign the string pointers to all the necessary HUD slots depending on the current mode
void AssignHUDSlots(void) {
  struct speedrun_hud_string* timer = &speedrun_hud_strings.timer;
  struct speedrun_hud_string* seed = &speedrun_hud_strings.seed;
  struct speedrun_hud_string* fps = &speedrun_hud_strings.fps;
  struct speedrun_hud_string* input_display = &speedrun_hud_strings.input_display;
  switch(hud_display_mode) {
    case HUD_DISPLAY_NONE:
      ClearHUDSlots();
      break;
    case HUD_DISPLAY_MINIMAL:
      if (timer->base_x_offset == TIMER_BOTTOM_X_OFFSET) {
        timer->base_x_offset = 0;
        timer->x_offset -= TIMER_BOTTOM_X_OFFSET;
      }
      ClearHUDSlots();
      AssignHUDSlot(TIMER_SLOT_MINIMAL,
                    TIMER_SLOT_MINIMAL_STRING_IDX,
                    timer->string,
                    &timer->x_offset);
      AssignHUDSlot(SEED_SLOT_MINIMAL,
                    SEED_SLOT_MINIMAL_STRING_IDX,
                    seed->string,
                    &seed->x_offset);
      break;
    case HUD_DISPLAY_MAXIMAL:
      if (timer->base_x_offset == 0) {
        timer->base_x_offset = TIMER_BOTTOM_X_OFFSET;
        timer->x_offset += TIMER_BOTTOM_X_OFFSET;
      }
      ClearHUDSlots();
      AssignHUDSlot(TIMER_SLOT_MAXIMAL,
                    TIMER_SLOT_MAXIMAL_STRING_IDX,
                    timer->string,
                    &timer->x_offset);
      AssignHUDSlot(SEED_SLOT_MAXIMAL,
                    SEED_SLOT_MAXIMAL_STRING_IDX,
                    seed->string,
                    &seed->x_offset);
      AssignHUDSlot(FPS_SLOT_MAXIMAL,
                    FPS_SLOT_MAXIMAL_STRING_IDX,
                    fps->string,
                    &fps->x_offset);
      AssignHUDSlot(INPUT_DISPLAY_SLOT_MAXIMAL,
                    INPUT_DISPLAY_SLOT_MAXIMAL_STRING_IDX,
                    input_display->string,
                    &input_display->x_offset);
      break;
  }
}

// Read inputs to switch between HUD modes
void HandleHUDToggle(void) {
  struct held_buttons held_buttons;
  GetHeldButtons(0, &held_buttons);
  if (held_buttons.start && held_buttons.select) {
    if (!prev_held_srh) {
      prev_held_srh = true;
      switch(hud_display_mode) {
        case HUD_DISPLAY_NONE:
          hud_display_mode = HUD_DISPLAY_MINIMAL;
          break;
        case HUD_DISPLAY_MINIMAL:
          hud_display_mode = HUD_DISPLAY_MAXIMAL;
          break;
        case HUD_DISPLAY_MAXIMAL:
          hud_display_mode = HUD_DISPLAY_NONE;
          break;
        default:
          break;
      }
      AssignHUDSlots();
    }
  } else {
    prev_held_srh = false;
  }

  // Disallow turning off HUD when fixed RNG is on
  if (IsFixedRNG() && hud_display_mode == HUD_DISPLAY_NONE) {
    hud_display_mode = HUD_DISPLAY_MINIMAL;
    AssignHUDSlots();
  }
}