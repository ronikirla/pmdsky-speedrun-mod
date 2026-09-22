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
#include "eeprom.h"
#include "soft_reset.h"
#include "uplink.h"

#define STACK_SIZE_4KB 1024 * 4
#define STACK_SIZE_2KB 1024 * 2

#define VBLANK_ROUTINE_THREAD_PRIO 10
#define MAIN_ROUTINE_THREAD_PRIO 30

void VCount0Routine(void*);
void MainRoutine(void*);

struct thread vblank_routine_thread;
struct thread main_routine_thread;

uint64_t vblank_routine_thread_stack[STACK_SIZE_2KB / sizeof(uint64_t)];
uint64_t main_routine_thread_stack[STACK_SIZE_4KB / sizeof(uint64_t)];

__attribute__((used)) void InitThreads(void) {
  OS_CreateThread(&vblank_routine_thread, VCount0Routine, NULL,
                  vblank_routine_thread_stack + STACK_SIZE_2KB / sizeof(uint64_t),
                  STACK_SIZE_2KB, VBLANK_ROUTINE_THREAD_PRIO);
  OS_CreateThread(&main_routine_thread, MainRoutine, NULL,
                  main_routine_thread_stack + STACK_SIZE_4KB / sizeof(uint64_t),
                  STACK_SIZE_4KB, MAIN_ROUTINE_THREAD_PRIO);
  // Bring up the DSpico USB uplink (card lock is held briefly per
  // transaction; the game's own card I/O is never disturbed)
  UplinkInit();
}

__attribute__((used)) void WakeupThreads(void) {
  OS_WakeupThreadDirect(&vblank_routine_thread);
  OS_WakeupThreadDirect(&main_routine_thread);
}

// High priority routine to perform every frame on VCount 0.
// Currently just keeps track of the FPS and idle time.
// Remember thread safety! When writing to a shared resource,
// see what could happen in other threads
void VCount0Routine(void*) {
  while(true) {
    CalculateFPS();
    UpdateAPSIdleTime();
    OS_SleepThread(NULL);
  }
}

// Low priority routine to minimize the performance impact of the
// mod by only running it while we would be sleeping
void MainRoutine(void*) {
  while (true) {
    HandleSoftReset();
    HandleHUDToggle();
    HandleSpeedToggle();
    HandleTimerInput();
    HandleAPSInput();
    UpdateTimer();
    UpdateFPS();
    UpdateAPS();
    UpdateInputDisplay();
    UpdateHUDSlots();
    SaveIGT(true);
    // Stream the memory samples over USB (runs only while the mod thread
    // would otherwise be idle; pauses while the game holds the card lock)
    UplinkPoll();
    OS_SleepThread(NULL);
  }
}