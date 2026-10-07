#pragma once
#include <pmdsky.h>

// Crash dump: when FatalError or OS_Panic halts the game, the hook stub in
// patches/patch.asm captures the registers and calls CrashDumpWrite, which
// writes a self-validating record to the backup EEPROM for offline
// inspection. The watchdog thread (CrashDumpWatchdogRoutine) writes the same
// record on the L+R+X+Y button combo, for hangs that never reach a hook.
// The record is left untouched on clean runs (all 0xFF).
//
// Since version 3 the record covers *every* thread: a fixed 0x100 header
// describing the current (crashing/triggering) thread, followed by packed
// variable-length thread records walked from THREAD_INFO_STRUCT's thread
// list. The FatalError format string (the "official" crash message) is still
// kept in the header.
//
// Record layout at CRASH_DUMP_EEPROM_BASE (all little-endian):
//
// Header (0x100 bytes):
//   0x000  u32  magic       ('CRSH')
//   0x004  u32  version     (3)
//   0x008  u32  hook_id     (CRASH_DUMP_HOOK_*)
//   0x00C  u32  tick_lo     (OS_GetTickLo)
//   0x010  u32  pc          (current thread: hooked instruction / trigger site)
//   0x014  u32  lr
//   0x018  u32  sp          (sp_base: snapshot base of thread record 0)
//   0x01C  u32  cpsr
//   0x020  u32  r0..r12     (13 words, 0x020..0x050; 0 for the manual trigger)
//   0x054  u32  checksum    (sum of the words at 0x000..0x0FF except 0x054
//                            and 0x0FC)
//   0x058  u32  arg0..arg3  (original r0-r3 at hook entry; 0 for the manual
//                            trigger)
//   0x068  u32  msg_len     (byte count of msg, excluding the NUL; 0 if none)
//   0x06C  u32  thread_count     (threads intended for the dump)
//   0x070  u32  threads_written  (thread records actually written)
//   0x074  u32  crashing_index   (index of the current thread's record; 0)
//   0x078  u32  trigger_buttons (raw held_buttons bitfield for the manual
//                                trigger, 0 for the hooks)
//   0x07C  char msg[0x80]   (FatalError format string, NUL-terminated, at most
//                            CRASH_DUMP_MSG_MAX bytes; empty otherwise)
//   0x0FC  u32  complete    (0 while writing; the very last EEPROM write sets
//                            1 - a 0 means the dump was interrupted)
//
// Thread records (packed from 0x100 on, variable length):
//   +0x00  u32  magic        ('THRD')
//   +0x04  u32  thread_id    (thread::thread_id; 0xFFFFFFFF when unknown)
//   +0x08  u32  priority     (thread::sorting_order; 0xFFFFFFFF when unknown)
//   +0x0C  u32  pc           (current thread: crash/trigger pc; other threads:
//                            os_context::function_address_plus_4 - 4)
//   +0x10  u32  lr           (os_context::exit_function)
//   +0x14  u32  sp           (sp the snapshot starts at)
//   +0x18  u32  stack_start  (low end of the thread's stack area)
//   +0x1C  u32  stack_end    (high end, exclusive)
//   +0x20  u32  state        (enum os_thread_state; 0xFFFFFFFF when unknown)
//   +0x24  u32  snapshot_bytes (multiple of 4; the snapshot follows)
//   +0x28  u32  flags        (CRASH_DUMP_THREAD_FLAG_*)
//   +0x2C  u8   snapshot[snapshot_bytes]  (stack words from sp upward)
//
// Thread record 0 is always the current thread (the crashing thread, or the
// watchdog for a manual trigger); the other records follow the game's thread
// list order (highest priority first). Each record's snapshot gets a fair
// share of the space that is still left (see StageThreadRecord), capped at
// CRASH_DUMP_MAX_SNAPSHOT_BYTES, so traces get shallower but always fit when
// the game has many threads.
//
// The area is free: the mod's RNG state ends at 0xb6a6 and the next mod-owned
// EEPROM region (the magic byte) is at 0xc7f0, so 0xb6b0..0xc6af is unused.

#define CRASH_DUMP_EEPROM_BASE 0xb6b0
#define CRASH_DUMP_SIZE 0x1000
#define CRASH_DUMP_MAGIC 0x48535243 // 'CRSH'
#define CRASH_DUMP_VERSION 3

#define CRASH_DUMP_HOOK_FATAL_ERROR 1
#define CRASH_DUMP_HOOK_OS_PANIC 2
#define CRASH_DUMP_HOOK_MANUAL 3

#define CRASH_DUMP_HEADER_SIZE 0x100
// Max message bytes copied (excluding the NUL, which always fits).
#define CRASH_DUMP_MSG_MAX 0x7F
#define CRASH_DUMP_WRITE_CHUNK_SIZE 0x100
// The record is staged in RAM one piece at a time (a crash handler must not
// hold 0x1000 bytes of stack), so each thread's snapshot is capped at the
// staging buffer size minus the record header.
#define CRASH_DUMP_RECORD_BUFFER_SIZE 0x400
#define CRASH_DUMP_MAX_SNAPSHOT_BYTES (CRASH_DUMP_RECORD_BUFFER_SIZE - CRASH_DUMP_THREAD_RECORD_HEADER_SIZE)

#define CRASH_DUMP_OFF_MAGIC 0x000
#define CRASH_DUMP_OFF_VERSION 0x004
#define CRASH_DUMP_OFF_HOOK_ID 0x008
#define CRASH_DUMP_OFF_TICK 0x00C
#define CRASH_DUMP_OFF_PC 0x010
#define CRASH_DUMP_OFF_LR 0x014
#define CRASH_DUMP_OFF_SP 0x018
#define CRASH_DUMP_OFF_CPSR 0x01C
#define CRASH_DUMP_OFF_R0 0x020 // r0..r12 at 0x020..0x050
#define CRASH_DUMP_OFF_CHECKSUM 0x054
#define CRASH_DUMP_OFF_ARG0 0x058 // arg0..arg3 at 0x058..0x064
#define CRASH_DUMP_OFF_ARG1 0x05C
#define CRASH_DUMP_OFF_ARG2 0x060
#define CRASH_DUMP_OFF_ARG3 0x064
#define CRASH_DUMP_OFF_MSG_LEN 0x068
#define CRASH_DUMP_OFF_THREAD_COUNT 0x06C
#define CRASH_DUMP_OFF_THREADS_WRITTEN 0x070
#define CRASH_DUMP_OFF_CRASHING_INDEX 0x074
#define CRASH_DUMP_OFF_TRIGGER_BUTTONS 0x078
#define CRASH_DUMP_OFF_MSG 0x07C // msg at 0x07C..0x0FB
#define CRASH_DUMP_OFF_COMPLETE 0x0FC

#define CRASH_DUMP_THREAD_RECORD_MAGIC 0x44524854 // 'THRD'
#define CRASH_DUMP_THREAD_RECORD_HEADER_SIZE 0x2C

#define CRASH_DUMP_THREAD_OFF_MAGIC 0x00
#define CRASH_DUMP_THREAD_OFF_THREAD_ID 0x04
#define CRASH_DUMP_THREAD_OFF_PRIORITY 0x08
#define CRASH_DUMP_THREAD_OFF_PC 0x0C
#define CRASH_DUMP_THREAD_OFF_LR 0x10
#define CRASH_DUMP_THREAD_OFF_SP 0x14
#define CRASH_DUMP_THREAD_OFF_STACK_START 0x18
#define CRASH_DUMP_THREAD_OFF_STACK_END 0x1C
#define CRASH_DUMP_THREAD_OFF_STATE 0x20
#define CRASH_DUMP_THREAD_OFF_SNAPSHOT_BYTES 0x24
#define CRASH_DUMP_THREAD_OFF_FLAGS 0x28
#define CRASH_DUMP_THREAD_OFF_SNAPSHOT 0x2C

// The snapshot starts at a sp that is inside the thread's stack bounds.
#define CRASH_DUMP_THREAD_FLAG_SP_VALID 1
// This record is the current thread (record 0).
#define CRASH_DUMP_THREAD_FLAG_CURRENT 2

// Layout of the regs array passed in from the hook stubs:
//   regs[0] = pc, regs[1] = lr, regs[2] = sp, regs[3] = cpsr,
//   regs[4..16] = r0..r12
// The manual trigger only fills pc/lr/sp/cpsr (the watchdog's registers are
// not useful for diagnosing other threads).
#define CRASH_DUMP_REG_PC 0
#define CRASH_DUMP_REG_LR 1
#define CRASH_DUMP_REG_SP 2
#define CRASH_DUMP_REG_CPSR 3
#define CRASH_DUMP_REG_R0 4
#define CRASH_DUMP_REG_COUNT 17

// sp_base points at the stack location of the original first instruction's
// push result (the top of the stack at crash time). It is the snapshot base
// of thread record 0.
void CrashDumpWrite(uint32_t hook_id, const uint32_t *regs, uint32_t sp_base);

// Capture the live thread states and write the dump record (hook_id
// CRASH_DUMP_HOOK_MANUAL). Called by the watchdog on L+R+X+Y.
void CrashDumpTrigger(void);

// Highest-priority mod thread: polls the button combo every frame and turns
// hangs (infinite loops) into a diagnosable crash dump. Runs until the first
// dump is written (crash_dump_active then blocks any further trigger).
void CrashDumpWatchdogRoutine(void *arg);