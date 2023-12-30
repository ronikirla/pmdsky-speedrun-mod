// Implements the input display component for the speedrun info HUD.

#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"
#include "hud.h"
#include "speedrun_hud.h"

#define NUM_BUTTONS 12
#define STR_TAG_LEN 10

#define ICON_START  "[M:B0]"
#define ICON_SELECT "[M:B1]"
#define ICON_A      "[M:B2]"
#define ICON_B      "[M:B3]"
#define ICON_X      "[M:B4]"
#define ICON_Y      "[M:B5]"
#define ICON_L      "[M:B6]"
#define ICON_R      "[M:B7]"
#define ICON_UP     "[M:B12]"
#define ICON_DOWN   "[M:B13]"
#define ICON_LEFT   "[M:B14]"
#define ICON_RIGHT  "[M:B15]"

#define DIR_WIDTH           12
#define NORMAL_WIDTH        11

#define BASE_OFFSET 50

#define OFFSET_LEFT   0
#define OFFSET_RIGHT  OFFSET_LEFT
#define OFFSET_UP     DIR_WIDTH
#define OFFSET_DOWN   OFFSET_UP
#define OFFSET_A      2 * DIR_WIDTH
#define OFFSET_B      OFFSET_A +     NORMAL_WIDTH
#define OFFSET_X      OFFSET_A + 2 * NORMAL_WIDTH
#define OFFSET_Y      OFFSET_A + 3 * NORMAL_WIDTH
#define OFFSET_L      OFFSET_A + 4 * NORMAL_WIDTH
#define OFFSET_R      OFFSET_A + 5 * NORMAL_WIDTH
#define OFFSET_START  OFFSET_A + 6 * NORMAL_WIDTH
#define OFFSET_SELECT OFFSET_START

struct input_display_button {
  bool held;
  char tag[STR_TAG_LEN];
  uint8_t x_offset;
};

void UpdateInputDisplay(void) {
  struct held_buttons held_buttons;
  GetHeldButtons(0, &held_buttons);
  struct input_display_button buttons[NUM_BUTTONS] = {
    {
      .held = held_buttons.left,
      .tag = ICON_LEFT,
      .x_offset = OFFSET_LEFT
    },
    {
      .held = held_buttons.right,
      .tag = ICON_RIGHT,
      .x_offset = OFFSET_RIGHT
    },
    {
      .held = held_buttons.up,
      .tag = ICON_UP,
      .x_offset = OFFSET_UP
    },
    {
      .held = held_buttons.down,
      .tag = ICON_DOWN,
      .x_offset = OFFSET_DOWN
    },
    {
      .held = held_buttons.a,
      .tag = ICON_A,
      .x_offset = OFFSET_A
    },
    {
      .held = held_buttons.b,
      .tag = ICON_B,
      .x_offset = OFFSET_B
    },
    {
      .held = held_buttons.x,
      .tag = ICON_X,
      .x_offset = OFFSET_X
    },
    {
      .held = held_buttons.y,
      .tag = ICON_Y,
      .x_offset = OFFSET_Y
    },
    {
      .held = held_buttons.l,
      .tag = ICON_L,
      .x_offset = OFFSET_L
    },
    {
      .held = held_buttons.r,
      .tag = ICON_R,
      .x_offset = OFFSET_R
    },
    {
      .held = held_buttons.start,
      .tag = ICON_START,
      .x_offset = OFFSET_START
    },
    {
      .held = held_buttons.select,
      .tag = ICON_SELECT,
      .x_offset = OFFSET_SELECT
    }
  };

  char input_display_string[HUD_LEN] = "";
  for (int i = 0; i < NUM_BUTTONS; i++) {
    struct input_display_button button = buttons[i];
    if (button.held) {
      char button_string[HUD_LEN];
      snprintf(button_string, HUD_LEN, "[CLUM_SET:%d]%s", BASE_OFFSET + button.x_offset, button.tag);
      strcat(input_display_string, button_string);
    }
  }
  UpdateHUDString(SPEEDRUN_HUD_INPUT_DISPLAY, input_display_string, 0);
}