// This file handles creating and dispatching the threads used to run the code of the mod

#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"
#include "speedrun_hud.h"
#include "timer.h"
#include "fps.h"
#include "aps.h"
#include "input_display.h"
#include "optimizations.h"

#define STACK_SIZE_4KB 1024 * 4
#define STACK_SIZE_1KB 1024

#define VBLANK_ROUTINE_THREAD_PRIO 10
#define DRAWING_THREAD_PRIO 30

void VBlankRoutine(void*);
void DrawingRoutine(void*);

struct thread vblank_routine_thread;
struct thread drawing_thread;

uint64_t vblank_routine_thread_stack[STACK_SIZE_4KB / sizeof(uint64_t)];
uint64_t drawing_thread_stack[STACK_SIZE_1KB / sizeof(uint64_t)];

__attribute__((used)) void InitThreads(void) {
  OS_CreateThread(&vblank_routine_thread, VBlankRoutine, NULL,
                  vblank_routine_thread_stack + STACK_SIZE_4KB / sizeof(uint64_t),
                  STACK_SIZE_4KB, VBLANK_ROUTINE_THREAD_PRIO);
  OS_CreateThread(&drawing_thread, DrawingRoutine, NULL,
                  drawing_thread_stack + STACK_SIZE_1KB / sizeof(uint64_t),
                  STACK_SIZE_1KB, DRAWING_THREAD_PRIO);
}

__attribute__((used)) void WakeupThreads(void) {
  OS_WakeupThreadDirect(&vblank_routine_thread);
  OS_WakeupThreadDirect(&drawing_thread);
}

// High priority routine to perform every frame on VBlank.
// Used to run the main logic of the mod
void VBlankRoutine(void*) {
  while(true) {
    HandleHUDToggle();
    HandleSpeedToggle();
    HandleTimerInput();
    UpdateTimer();
    UpdateFPS();
    UpdateAPS();
    UpdateInputDisplay();
    OS_SleepThread(NULL);
  }
}

// Low priority routine to perform drawing of the HUD.
// Minimize the performance impact of the mod by only running
// it while we would be sleeping
void DrawingRoutine(void*) {
  while (true) {
    UpdateHUDSlots();
    OS_SleepThread(NULL);
  }
}