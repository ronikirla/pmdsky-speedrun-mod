#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"
#include "timer.h"

bool is_saving = false;
bool just_finished_run = false;

__attribute__((used)) uint8_t PlayTimerTickAndWaitTillVBlank(void) {
  // Magic number for whether we are in the top menu of the main menu.
  // If not, then advance the play timer. Otherwise pause (normal behaviour)
  int* main_menu_magic = (int*) 0x22a3670;
  if (*main_menu_magic != 0x22a3e94 && PLAY_TIME_SECONDS != 0) {
    // In main menu with custom timer at zero, advance start_time to keep displayed timer at 0
    struct play_time* igt = (struct play_time*) &PLAY_TIME_SECONDS;
    bool advance_start_time = false;
    if (IGTDifferenceFrames(igt, &start_time) == 0) {
      advance_start_time = true;
    }
    PlayTimerTick((struct play_time*) &PLAY_TIME_SECONDS);
    if (advance_start_time) {
      memcpy(&start_time, igt, sizeof(struct play_time));
    }
  }
  return WaitTillVBlank();
}

void CustomPlayTimerTick(struct play_time *param_1) {
  char bVar1;

  bVar1 = param_1->frames + 1;
  param_1->frames = bVar1;
  if (0x3b < bVar1) {
    param_1->frames = '\0';
    if (param_1->seconds < MAX_PLAY_TIME) {
      param_1->seconds = param_1->seconds + 1;
    }
    return;
  }
  return;
}

void AddTimePenalty(struct play_time *play_time, int additional_frames) {
  if (!just_finished_run) {
    int total_frames = play_time->frames + additional_frames;
    play_time->seconds += total_frames / 60;
    play_time->frames = (uint8_t)(total_frames % 60);
    return;
  }
}

__attribute__((used)) void *HijackNoteSaveBaseAndSetSaveVariable(void) {
  is_saving = true;
  void *res = MemAlloc(0xB65C, 5);
  return res;
}

__attribute__((used)) void HijackNoteSaveBaseAndUnsetSaveVariableAndAlsoAddTimePenaltyByTheWay(void *whatever_this_was) {
  is_saving = false;

  AddTimePenalty(&PLAY_TIME, 200);
  MemFree(whatever_this_was);
  return;
}

__attribute__((used)) void CheckIfShouldIncrementPlayTimer(void *param_1) {
  if (!is_saving && !just_finished_run) {
    CustomPlayTimerTick(param_1);
  }
  return;
}
