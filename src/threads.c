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
#include "crash_dump.h"

#define STACK_SIZE_4KB 1024 * 4
#define STACK_SIZE_2KB 1024 * 2
#define STACK_SIZE_1KB 1024

// Lower number = higher priority. The watchdog must preempt a hung game
// thread, so it runs above the other mod threads (10 and 30).
#define WATCHDOG_THREAD_PRIO 5
#define VBLANK_ROUTINE_THREAD_PRIO 10
#define MAIN_ROUTINE_THREAD_PRIO 30

void VCount0Routine(void *);
void MainRoutine(void *);

struct thread watchdog_thread;
struct thread vblank_routine_thread;
struct thread main_routine_thread;

uint64_t watchdog_thread_stack[STACK_SIZE_1KB / sizeof(uint64_t)];
// 1KB is enough for the vblank routine: its whole call graph (CalculateFPS /
// UpdateAPSIdleTime) uses only small HUD_LEN char buffers plus static arrays.
// The 2KB saved here keeps the code+data+bss region within its linker length.
uint64_t vblank_routine_thread_stack[STACK_SIZE_1KB / sizeof(uint64_t)];
uint64_t main_routine_thread_stack[STACK_SIZE_4KB / sizeof(uint64_t)];

// Dedicated wake queue for the mod's threads. The VCount 0 hook wakes the
// queue as a whole (OS_WakeupThread) instead of poking each thread with
// OS_WakeupThreadDirect: the queue wake properly unlinks the sleepers and
// clears their thread::queue / thread::link fields, while a direct wake on a
// thread that happens to be sleeping on another queue (e.g. inside
// Card_LockRom / CARDi_WaitTask during UplinkPoll) leaves a stale linked node
// behind and corrupts that queue's list. A BSS-zeroed os_thread_queue is
// already a valid empty queue (head == tail == NULL), no init call needed.
struct os_thread_queue mod_wake_queue;
// Count of WakeupThreads() calls (one per VCount 0 since the threads were
// created): recorded in crash dump records for hang diagnosis.
uint32_t mod_wake_count = 0;

__attribute__((used)) void InitThreads(void)
{
  OS_CreateThread(&watchdog_thread, CrashDumpWatchdogRoutine, NULL,
                  watchdog_thread_stack + STACK_SIZE_1KB / sizeof(uint64_t),
                  STACK_SIZE_1KB, WATCHDOG_THREAD_PRIO);
  OS_CreateThread(&vblank_routine_thread, VCount0Routine, NULL,
                  vblank_routine_thread_stack + STACK_SIZE_1KB / sizeof(uint64_t),
                  STACK_SIZE_1KB, VBLANK_ROUTINE_THREAD_PRIO);
  OS_CreateThread(&main_routine_thread, MainRoutine, NULL,
                  main_routine_thread_stack + STACK_SIZE_4KB / sizeof(uint64_t),
                  STACK_SIZE_4KB, MAIN_ROUTINE_THREAD_PRIO);
  // One-time startup wake per thread (the SDK idiom, see CARDi_InitCommon):
  // OS_CreateThread leaves the thread waiting but not linked into any queue,
  // so OS_WakeupThread(&mod_wake_queue) could never start it. This direct wake
  // is safe here - freshly created threads have queue == NULL and empty link
  // fields, so no stale queue node can result. Every later wake goes through
  // the queue (see WakeupThreads).
  OS_WakeupThreadDirect(&watchdog_thread);
  OS_WakeupThreadDirect(&vblank_routine_thread);
  OS_WakeupThreadDirect(&main_routine_thread);
  // Bring up the DSpico USB uplink (card lock is held briefly per
  // transaction; the game's own card I/O is never disturbed)
}

__attribute__((used)) void WakeupThreads(void)
{
  mod_wake_count++;
  // Wake the whole queue: waiters are unlinked cleanly (see mod_wake_queue).
  OS_WakeupThread(&mod_wake_queue);
}

// High priority routine to perform every frame on VCount 0.
// Currently just keeps track of the FPS and idle time.
// Remember thread safety! When writing to a shared resource,
// see what could happen in other threads
void VCount0Routine(void *)
{
  while (true)
  {
    CalculateFPS();
    UpdateAPSIdleTime();
    OS_SleepThread(&mod_wake_queue);
  }
}

// Low priority routine to minimize the performance impact of the
// mod by only running it while we would be sleeping
void MainRoutine(void *)
{
  while (true)
  {
    // Skip all USB uplink traffic while overlay 30 (quicksave) is loaded:
    // the game is doing a burst of EEPROM writes then, and mod card
    // transactions must stay out of that window (occasional eeprom
    // corruption after a quicksave). UplinkInit/UplinkPoll are the only USB
    // senders in the mod, so gating them gates all USB data.
    bool quicksave_active = OverlayIsLoaded(OGROUP_OVERLAY_30);
    if (!quicksave_active)
    {
      // will only init if it isn't elready and if certain overlays are loaded
      UplinkInit();
    }
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
    if (!quicksave_active)
    {
      //  Stream the memory samples over USB (runs only while the mod thread
      //  would otherwise be idle; pauses while the game holds the card lock)
      UplinkPoll();
    }
    OS_SleepThread(&mod_wake_queue);
  }
}