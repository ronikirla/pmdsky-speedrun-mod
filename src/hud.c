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

#define WINDOW_LIST_LEN 20

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

// Return number of open windows
int GetNofOpenWindows(void) {
  int result = 0;
  for (int i = 0; i < WINDOW_LIST_LEN; i++) {
    if (WINDOW_LIST.windows[i].valid) {
      result++;
    }
  }
  return result;
}

bool too_many_windows;

// Close the HUD or recreate it depending on if there is enough space for its windows
void HandleTooManyWindows(int open) {
  // To be safe, close HUD at 19 windows. Doing it at 20 caused some crashes for some reason.
  // Presumably the game assumes that there is room in the window list or something even before
  // allocating the window.
  if (open >= WINDOW_LIST_LEN - 1) {
    too_many_windows = true;
    for (int i = 0; i < HUD_SLOTS; i++) {
      CloseHUD((enum hud_slot) i);
    }
  // We need to have 1 less window open before we open the HUD again because otherwise
  // closing the HUD would cause enough slots to be free only for the HUD to be opened
  // right back up again 
  } else if (open + HUD_SLOTS <= WINDOW_LIST_LEN - 2) {
    too_many_windows = false;
  }
}

__attribute__((naked)) void HijackNewWindowScreenCheckAndCheckOpenWindows(void) {
  asm("stmdb sp!,{r0-r12,lr}");
  int open = GetNofOpenWindows();
  HandleTooManyWindows(open);
  asm("ldmia sp!,{r0-r12,lr}");
  asm("mov r4,#0x0");
  asm("bx lr");
}

__attribute__((naked)) void HijackDeleteWindowAndCheckOpenWindows(void) {
  asm("strb r0,[r4,#0xb6]");
  asm("stmdb sp!,{r0-r12,lr}");
  int open = GetNofOpenWindows();
  HandleTooManyWindows(open);
  asm("ldmia sp!,{r0-r12,lr}");
  asm("bx lr");
}

// There's some duplicate code for re-creating the keyboard when saying
// "no" to confirming the team name in the guild. This code however doesn't
// handle the top screen controls chart creation properly. It happens to
// work in the vanilla game, but causes corruption with extra windows open.
__attribute__((naked)) void HijackTeamNamePromptConfirm(void) {
  asm("stmdb sp!,{r0-r12,lr}");
  CloseHUD(HUD_SLOT_TOP_LEFT);
  CloseHUD(HUD_SLOT_TOP_RIGHT);
  asm("ldmia sp!,{r0-r12,lr}");
  asm("cmp r0,#0x0");
  asm("bx lr");
}

// HUD creation/closing handling. Hook into SetBrightness so we can easily tell when the screen is faded.
// The reason we need to do this is because leaving any windows open over a fade will cause memory corruption.
__attribute__((used)) void CustomSetBrightnessExit(enum screen screen, int brightness) {
  void(* hud_func)(enum hud_slot);
  // Faded as in fully black or white
  bool faded = (brightness >= 0xFF || brightness <= -0xFF);
  struct held_buttons held_buttons;
  GetHeldButtons(0, &held_buttons);
  // Workaround to allow buffering CancelRecoverCommon (aka. dinner skip) during a fade
  bool start_held_during_fade = screen == SCREEN_MAIN && held_buttons.start && brightness != 0;
  start_held_during_nonblocking_fade = start_held_during_nonblocking_fade && OverlayIsLoaded(OGROUP_OVERLAY_11);
  bool input_buffer_workaround = start_held_during_fade || start_held_during_nonblocking_fade || simple_menu_open;
  if (faded || input_buffer_workaround || too_many_windows) {
    hud_func = &CloseHUD;
  } else {
    // Very important, because the window system of the game is held together
    // by duct tape and completely corrupts everything if you try to create a
    // text box while any menu is open. Disable this check in the main menu,
    // quiz and sky jukebox because those are constantly in menu state and
    // the corruption is not a concern since it can only happen in a 
    // nonblocking fade state that is only used in ground mode.
    if (menu_open && screen == SCREEN_MAIN &&
        !OverlayIsLoaded(OGROUP_OVERLAY_1) &&
        !OverlayIsLoaded(OGROUP_OVERLAY_13) &&
        !OverlayIsLoaded(OGROUP_OVERLAY_9)) {
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