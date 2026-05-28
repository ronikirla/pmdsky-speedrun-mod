#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"
#include "timer.h"

bool is_saving = false;
bool just_finished_run = false;

__attribute__((used)) uint8_t PlayTimerTickAndWaitTillVBlank(void) {
  // Magic number for whether we are in the top menu of the main menu.
  // If not, then advance the play timer. Otherwise pause (normal behaviour)
  static int frames_paused = 0;
  static bool was_paused = false;
  static int resume_delay_remaining = 0;

  int* main_menu_magic = (int*) 0x22a3670;
  bool is_paused = (*main_menu_magic == 0x22a3e94) || (PLAY_TIME_SECONDS == 0);

  if (is_paused) {
    // Timer is paused - count frames
    frames_paused++;
    was_paused = true;
    resume_delay_remaining = 0;
  } else if (was_paused && resume_delay_remaining == 0) {
    // Transition from paused to running - calculate and set resume delay
    // Delay is max(10, 30 - frames_paused) to ensure at least 30 frames total
    int delay = 30 - frames_paused;
    if (delay < 10) delay = 10;
    resume_delay_remaining = delay;
    frames_paused = 0;
    // Don't advance timer yet - still in delay period
  } else if (resume_delay_remaining > 0) {
    // Still in resume delay period
    resume_delay_remaining--;
    if (resume_delay_remaining == 0) {
      // Delay expired - now actually resume
      was_paused = false;
      just_finished_run = false;
    }
    // Don't advance timer during delay
  } else if (!just_finished_run) {
    // Normal running state - advance the timer
    was_paused = false;
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
