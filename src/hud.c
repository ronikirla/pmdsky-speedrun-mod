// A generic HUD component that can be used to display text in pre-determined "slots"

#include <pmdsky.h>
#include <cot.h>
#include "hud.h"
#include "custom_headers.h"

#define WINDOW_WIDTH_TOP 10
#define WINDOW_OFFSET_TOP_LEFT 6
#define WINDOW_OFFSET_TOP_RIGHT 22

#define WINDOW_WIDTH_BOTTOM 32
#define WINDOW_OFFSET_BOTTOM 22
#define TEXT_OFFSET_BOTTOM 7
#define SLOT_SEPARATION_BOTTOM 100

#define WINDOW_HEIGHT 2

#define MAX_STRINGS 8

#define MENU_CLOSE_HUD_CREATE_DELAY 15

struct hud_window_status {
  int window_id;
  char* strings[MAX_STRINGS];
  uint8_t* x_offsets[MAX_STRINGS];
  struct window_params params;
};

struct hud_window_status hud_status[] = {
  {
    .window_id = -1,
    .strings = {NULL},
    .x_offsets = {NULL},
    .params = {
      .x_offset = WINDOW_OFFSET_TOP_LEFT,
      .y_offset = 0,
      .width = WINDOW_WIDTH_TOP,
      .height = WINDOW_HEIGHT,
      .screen = SCREEN_SUB,
      .box_type = BOX_TYPE_INVISIBLE
    }
  },
  {
    .window_id = -1,
    .strings = {NULL},
    .x_offsets = {NULL},
    .params = {
      .x_offset = WINDOW_OFFSET_TOP_RIGHT,
      .y_offset = 0,
      .width = WINDOW_WIDTH_TOP,
      .height = WINDOW_HEIGHT,
      .screen = SCREEN_SUB,
      .box_type = BOX_TYPE_INVISIBLE
    }
  },
  {
    .window_id = -1,
    .strings = {NULL},
    .x_offsets = {NULL},
    .params = {
      .x_offset = 0,
      .y_offset = WINDOW_OFFSET_BOTTOM,
      .width = WINDOW_WIDTH_BOTTOM,
      .height = WINDOW_HEIGHT,
      .screen = SCREEN_MAIN,
      .box_type = BOX_TYPE_INVISIBLE
    }
  }
};

// Leave this empty since we want to implement our own handling and not worry about the
// game's own callback system
void HUDCallback(int window_id) {};
// Refreshes the window with the current strings behind the string pointers
void UpdateHUD(enum hud_slot slot) {
  int window_id = hud_status[slot].window_id;
  if (slot == HUD_SLOT_NULL || window_id == -1) {
    return;
  }
  ClearWindow(window_id);
  for (int i = 0; i < MAX_STRINGS; i++) {
    char* string = hud_status[slot].strings[i];
    if (string) {
      DrawTextInWindow(window_id,
                       *hud_status[slot].x_offsets[i],
                       (slot == HUD_SLOT_BOTTOM) * TEXT_OFFSET_BOTTOM,
                       string); 
    }
    UpdateWindow(window_id);
  }
}

// Set a string and x-offset pointers of a specified HUD slot
void AssignHUDSlot(enum hud_slot slot, int string_idx, char* string_pointer, uint8_t* x_offset) {
  hud_status[slot].strings[string_idx] = string_pointer;
  hud_status[slot].x_offsets[string_idx] = x_offset;
  UpdateHUD(slot);
}

// Set the string pointers of every HUD slot to null
void ClearHUDSlots(void) {
  for (int i = 0; i < HUD_SLOTS; i++) {
    enum hud_slot slot = (enum hud_slot) i;
    for (int j = 0; j < MAX_STRINGS; j++) {
      AssignHUDSlot(slot, j, NULL, NULL);
    }
  }
}

// Create the text boxes of the HUD
void CreateHUD(enum hud_slot slot) {
  if (hud_status[slot].window_id != -1) {
    return;
  }
  hud_status[slot].window_id = CreateTextBox(&hud_status[slot].params, &HUDCallback);
  UpdateHUD(slot);
}

// Close the text boxes of the HUD
void CloseHUD(enum hud_slot slot) {
  if (hud_status[slot].window_id == -1) {
    return;
  }
  CloseTextBox(hud_status[slot].window_id);
  hud_status[slot].window_id = -1;
}

bool simple_menu_open;

// Having any windows open while a simple menu is open
// causes the prompts to not be able to be buffered. Therefore, we want to be able to close
// the HUD to prevent that from happening.
__attribute__((naked)) int HijackCreateSimpleMenuAndCloseHUD(void) {
  asm("stmdb sp!,{r0-r12,lr}");
  simple_menu_open = true;
  asm("ldmia sp!,{r0-r12,lr}");
  asm("mov r6,r3");
  asm("bx lr");
}

__attribute__((naked)) int HijackCloseSimpleMenuAndCreateHUD(void) {
  asm("stmdb sp!,{r0-r12}");
  simple_menu_open = false;
  asm("ldmia sp!,{r0-r12}");
  asm("ldmia sp!,{r3,r4,r5,pc}");
}

bool start_held_during_nonblocking_fade;

// Workaround to allow buffering CancelRecoverCommon (aka. dinner skip) during a fade
__attribute__((naked)) void HijackSetBrightnessNonblockingEntry(int brightness) {
  asm("stmdb sp!,{r0-r12,lr}");
  struct held_buttons held_buttons;
  GetHeldButtons(0, &held_buttons);
  start_held_during_nonblocking_fade = brightness != 0 && held_buttons.start;
  asm("ldmia sp!,{r0-r12,lr}");
  asm("mov r4,r0");
  asm("bx lr");
}

bool menu_open;

__attribute__((naked)) void HijackOpenMenuRoutine(void) {
  asm("stmdb sp!,{r0-r12,lr}");
  menu_open = true;
  asm("ldmia sp!,{r0-r12,lr}");
  asm("add r4,r12,#0x14");
  asm("bx lr");
}

__attribute__((naked)) void HijackCloseMenuRoutine(void) {
  asm("stmdb sp!,{r0-r12,lr}");
  menu_open = false;
  asm("ldmia sp!,{r0-r12,lr}");
  asm("ldr r0,[r12,#0x0]");
  asm("bx lr");
}

__attribute__((used)) void HijackUnloadMenuStateCall(void) {
  menu_open = false;
  UnloadMenuState();
}

// HUD creation/closing handling for all modes except dungeon mode. Hook into SetBrightness
// so we can easily tell when the screen is faded.
__attribute__((used)) void CustomSetBrightnessExit(enum screen screen, int brightness) {
  void(* hud_func)(enum hud_slot);
  // Faded as in fully black or white
  bool faded = (brightness >= 0xFF || brightness <= -0xFF);
  struct held_buttons held_buttons;
  GetHeldButtons(0, &held_buttons);
  // Workaround to allow buffering CancelRecoverCommon (aka. dinner skip) during a fade
  bool start_held_during_fade = screen == SCREEN_MAIN && held_buttons.start && brightness != 0;
  bool input_buffer_workaround = start_held_during_fade || start_held_during_nonblocking_fade || simple_menu_open;
  if (faded || input_buffer_workaround) {
    hud_func = &CloseHUD;
  } else {
    // Very important, because the window system of the game is held together
    // by duct tape and completely corrupts everything if you try to create a
    // text box while any menu is open. Disable this check in the quiz
    if (menu_open && screen == SCREEN_MAIN && !OverlayIsLoaded(OGROUP_OVERLAY_13)) {
      return;
    }
    hud_func = &CreateHUD;
  }
  switch(screen) {
    case SCREEN_MAIN:
      (*hud_func)(HUD_SLOT_BOTTOM);
      break;
    case SCREEN_SUB:
      (*hud_func)(HUD_SLOT_TOP_LEFT);
      (*hud_func)(HUD_SLOT_TOP_RIGHT);
      break;
    default:
      break;
  }
}

// These are used in dungeon mode for top screen handling
__attribute__((used)) void HijackTopScreenInitFuncAndDrawTopScreenText(void(* fun)()) {
  (*fun)();
  CreateHUD(HUD_SLOT_TOP_LEFT);
  CreateHUD(HUD_SLOT_TOP_RIGHT);
}

__attribute__((used)) void HijackFreeTopScreenAndClearTopScreenText() {
  CloseHUD(HUD_SLOT_TOP_LEFT);
  CloseHUD(HUD_SLOT_TOP_RIGHT);
  FreeTopScreen();
}

// Rewrite the second half of HandleFadesDungeon to include HUD handling for the bottom screen
__attribute__((used)) void UpdateDungeonFadeStruct(struct dungeon_fade* fstruct, enum screen screen) {
  int result;
  switch(fstruct->fade_type) {
    case DUNGEON_FADE_0x0:
      return;
    case DUNGEON_FADE_NONE:
      return;
    case DUNGEON_FADE_IN:
      result = fstruct->delta_brightness + fstruct->delta_delta_brightness;
      fstruct->delta_brightness = result;
      if (-1 < result) {
        fstruct->delta_brightness = 0;
        fstruct->fade_type = DUNGEON_FADE_NONE;
      } else if (screen == SCREEN_MAIN) {
        CreateHUD(HUD_SLOT_BOTTOM);
      }
      return;
    case DUNGEON_FADE_OUT:
      result = fstruct->delta_brightness - fstruct->delta_delta_brightness;
      fstruct->delta_brightness = result;
      if (result < -0xffff) {
        fstruct->delta_brightness = 0xffff0000;
        fstruct->fade_type = DUNGEON_FADE_NONE;
        if (screen == SCREEN_MAIN) {
          CloseHUD(HUD_SLOT_BOTTOM);
        }
      }
      return;
    // Idk when these cases happen (perhaps fade to white somewhere), just to be safe closing the HUD
    case DUNGEON_FADE_0x4:
      result = fstruct->delta_brightness - fstruct->delta_delta_brightness;
      fstruct->delta_brightness = result;
      if (result < 1) {
        fstruct->delta_brightness = 0;
        fstruct->fade_type = DUNGEON_FADE_NONE;
        if (screen == SCREEN_MAIN) {
          CloseHUD(HUD_SLOT_BOTTOM);
        }
      }
      return;
    case DUNGEON_FADE_0x5:
      result = fstruct->delta_brightness + fstruct->delta_delta_brightness;
      fstruct->delta_brightness = result;
      if (0xffff < result) {
        fstruct->delta_brightness = 0x10000;
        fstruct->fade_type = DUNGEON_FADE_NONE;
        if (screen == SCREEN_MAIN) {
          CloseHUD(HUD_SLOT_BOTTOM);
        }
      }
      return;
    default:
      return;
  }
}