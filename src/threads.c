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
#define VBLANK_ROUTINE_THREAD_PRIO 10
#define MAIN_ROUTINE_THREAD_PRIO 30

void VCount0Routine(void *);
void MainRoutine(void *);

struct thread vblank_routine_thread;
struct thread main_routine_thread;

//  1KB is enough for the vblank routine: its whole call graph (CalculateFPS /
//  UpdateAPSIdleTime) uses only small HUD_LEN char buffers plus static arrays.
//  The 2KB saved here keeps the code+data+bss region within its linker length.
uint64_t vblank_routine_thread_stack[STACK_SIZE_1KB / sizeof(uint64_t)];
// 2KB: the USB uplink rewrite (protocol v2) removed its ~1.1KB of stack
// buffers (two 512-byte card-phase buffers nested under UplinkPoll), which
// used to be the deepest stack user on this thread. The remaining deepest
// path is UpdateInputDisplay (two 512-byte HUD string buffers in one frame).
// Headroom is measured, not guessed: the stack is painted at init and the
// high-water mark is scanned periodically (mod_main_stack_used, reported in
// the uplink PING reply). If a session ever reports usage above ~1.5KB,
// revert this to STACK_SIZE_4KB and shrink UPLINK_MAX_SAMPLES instead.
uint64_t main_routine_thread_stack[STACK_SIZE_2KB / sizeof(uint64_t)];

// Stack paint/high-water instrumentation (uplink PING reports the value).
// The pattern byte is chosen so touched bytes almost never match it.
#define MAIN_STACK_PAINT_BYTE 0xC5u
// High-water: stack bytes used by the main routine (0 = never scanned).
// `unsigned int` matches pmdsky.h's uint32_t at this definition site; the
// uplink reads it via an equally-spelled extern (no header is shared).
volatile unsigned int mod_main_stack_used = 0;

static void paint_main_stack(void) {
    uint8_t *p = (uint8_t *)main_routine_thread_stack;
    uint32_t n = sizeof(main_routine_thread_stack);
    for (uint32_t i = 0; i < n; i++) {
        p[i] = MAIN_STACK_PAINT_BYTE;
    }
}

// Scan the painted stack for the lowest touched byte (stack grows down,
// so untouched bytes form the bottom prefix). Runs on the vblank thread
// while the main routine may be running — read-only, so a torn view is
// harmless for a diagnostic.
static void scan_main_stack(void) {
    uint8_t *p = (uint8_t *)main_routine_thread_stack;
    uint32_t n = sizeof(main_routine_thread_stack);
    for (uint32_t i = 0; i < n; i++) {
        if (p[i] != MAIN_STACK_PAINT_BYTE) {
            uint32_t used = n - i;
            if (used > mod_main_stack_used) {
                mod_main_stack_used = used;
            }
            return;
        }
    }
    // Whole stack touched: overflow territory. Report the full size.
    mod_main_stack_used = n;
}

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

__attribute__((used)) void InitThreads(void) {
    OS_CreateThread(&vblank_routine_thread, VCount0Routine, NULL,
                    vblank_routine_thread_stack + STACK_SIZE_1KB / sizeof(uint64_t),
                    STACK_SIZE_1KB, VBLANK_ROUTINE_THREAD_PRIO);
    OS_CreateThread(&main_routine_thread, MainRoutine, NULL,
                    main_routine_thread_stack + STACK_SIZE_2KB / sizeof(uint64_t),
                    STACK_SIZE_2KB, MAIN_ROUTINE_THREAD_PRIO);
    // Paint the main routine's stack AFTER OS_CreateThread: the SDK wipes
    // the stack region at thread creation (MI_CpuClear32 in
    // OS_CreateThread), so a pre-creation paint never survived and the
    // periodic scan always read the wipe — latching stack=2048 forever.
    // Safe ordering: OS_WakeupThreadDirect below only marks the threads
    // runnable; the scheduler runs them at the next yield, after this
    // function (same priority, not preempted by lower-priority threads)
    // has finished painting.
    paint_main_stack();
    // One-time startup wake per thread (the SDK idiom, see CARDi_InitCommon):
    // OS_CreateThread leaves the thread waiting but not linked into any queue,
    // so OS_WakeupThread(&mod_wake_queue) could never start it. This direct wake
    // is safe here - freshly created threads have queue == NULL and empty link
    // fields, so no stale queue node can result. Every later wake goes through
    // the queue (see WakeupThreads).
    OS_WakeupThreadDirect(&vblank_routine_thread);
    OS_WakeupThreadDirect(&main_routine_thread);
}

__attribute__((used)) void WakeupThreads(void) {
    mod_wake_count++;
    // Wake the whole queue: waiters are unlinked cleanly (see mod_wake_queue).
    OS_WakeupThread(&mod_wake_queue);
}

// High priority routine to perform every frame on VCount 0.
// Currently just keeps track of the FPS and idle time.
// Remember thread safety! When writing to a shared resource,
// see what could happen in other threads
void VCount0Routine(void *) {
    uint32_t scan_divider = 0;
    while (true) {
        CalculateFPS();
        UpdateAPSIdleTime();
        // Stack high-water scan every 16 frames (~4 Hz): cheap (a few hundred
        // byte compares) and catches every deep path that lasts >16 ms.
        if (++scan_divider >= 16) {
            scan_divider = 0;
            scan_main_stack();
        }
        CheckIfShouldCrashDump();
        OS_SleepThread(&mod_wake_queue);
    }
}

// Low priority routine to minimize the performance impact of the
// mod by only running it while we would be sleeping
void MainRoutine(void *) {
    while (true) {
        UplinkInit();
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
        UplinkPoll();
        OS_SleepThread(&mod_wake_queue);
    }
}