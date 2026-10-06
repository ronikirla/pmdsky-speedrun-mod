#pragma once
#include <pmdsky.h>

// Crash dump: when FatalError or OS_Panic halts the game, the hook stub in
// patches/patch.asm captures the registers and calls CrashDumpWrite, which
// writes a self-validating record to the backup EEPROM for offline
// inspection. The record is left untouched on clean runs (all 0xFF).
//
// Record layout at CRASH_DUMP_EEPROM_BASE (all little-endian):
//   0x000  u32  magic       ('CRSH')
//   0x004  u32  version
//   0x008  u32  hook_id     (CRASH_DUMP_HOOK_*)
//   0x00C  u32  tick_lo     (OS_GetTickLo)
//   0x010  u32  pc          (address of the hooked instruction)
//   0x014  u32  lr
//   0x018  u32  sp          (stack pointer after the original first instruction)
//   0x01C  u32  cpsr
//   0x020  u32  r0
//   0x024  u32  r1
//   0x028  u32  r2
//   0x02C  u32  r3
//   0x030  u32  r4
//   0x034  u32  r5
//   0x038  u32  r6
//   0x03C  u32  r7
//   0x040  u32  r8
//   0x044  u32  r9
//   0x048  u32  r10
//   0x04C  u32  r11
//   0x050  u32  r12
//   0x054  u32  checksum    (sum of the words at 0x000..0x053)
//   0x058  u32  arg0        (original r0 at hook entry)
//   0x05C  u32  arg1        (original r1 at hook entry)
//   0x060  u32  arg2        (original r2 at hook entry)
//   0x064  u32  arg3        (original r3 at hook entry)
//   0x068  u32  msg_len     (byte count of msg, excluding the NUL; 0 if none)
//   0x06C  char msg[0x194]  (FatalError format string, NUL-terminated, at most
//                            CRASH_DUMP_MSG_MAX bytes; empty for OS_Panic)
//   0x200  stack snapshot (CRASH_DUMP_STACK_SIZE bytes from sp upward)
//
// The area is free: the mod's RNG state ends at 0xb6a6 and the next mod-owned
// EEPROM region (the magic byte) is at 0xc7f0, so 0xb6b0..0xc6af is unused.

#define CRASH_DUMP_EEPROM_BASE 0xb6b0
#define CRASH_DUMP_SIZE 0x1000
#define CRASH_DUMP_MAGIC 0x48535243 // 'CRSH'
#define CRASH_DUMP_VERSION 2

#define CRASH_DUMP_HOOK_FATAL_ERROR 1
#define CRASH_DUMP_HOOK_OS_PANIC 2

// The header covers 0x000..0x1FF: the fixed fields above plus the FatalError
// message, which fills the formerly-reserved 0x068..0x1FF space.
#define CRASH_DUMP_HEADER_SIZE 0x200
#define CRASH_DUMP_STACK_OFFSET 0x200
#define CRASH_DUMP_STACK_SIZE 0xE00
#define CRASH_DUMP_STACK_CHUNK_SIZE 0x100

#define CRASH_DUMP_OFF_MAGIC 0x000
#define CRASH_DUMP_OFF_VERSION 0x004
#define CRASH_DUMP_OFF_HOOK_ID 0x008
#define CRASH_DUMP_OFF_TICK 0x00C
#define CRASH_DUMP_OFF_PC 0x010
#define CRASH_DUMP_OFF_LR 0x014
#define CRASH_DUMP_OFF_SP 0x018
#define CRASH_DUMP_OFF_CPSR 0x01C
#define CRASH_DUMP_OFF_R0 0x020
#define CRASH_DUMP_OFF_R1 0x024
#define CRASH_DUMP_OFF_R2 0x028
#define CRASH_DUMP_OFF_R3 0x02C
#define CRASH_DUMP_OFF_R4 0x030
#define CRASH_DUMP_OFF_R5 0x034
#define CRASH_DUMP_OFF_R6 0x038
#define CRASH_DUMP_OFF_R7 0x03C
#define CRASH_DUMP_OFF_R8 0x040
#define CRASH_DUMP_OFF_R9 0x044
#define CRASH_DUMP_OFF_R10 0x048
#define CRASH_DUMP_OFF_R11 0x04C
#define CRASH_DUMP_OFF_R12 0x050
#define CRASH_DUMP_OFF_CHECKSUM 0x054
#define CRASH_DUMP_OFF_ARG0 0x058
#define CRASH_DUMP_OFF_ARG1 0x05C
#define CRASH_DUMP_OFF_ARG2 0x060
#define CRASH_DUMP_OFF_ARG3 0x064
#define CRASH_DUMP_OFF_MSG_LEN 0x068
#define CRASH_DUMP_OFF_MSG 0x06C
// Max message bytes copied (excluding the NUL, which always fits).
#define CRASH_DUMP_MSG_MAX (CRASH_DUMP_HEADER_SIZE - CRASH_DUMP_OFF_MSG - 1)

// Layout of the regs array passed in from the hook stubs:
//   regs[0] = pc, regs[1] = lr, regs[2] = sp, regs[3] = cpsr,
//   regs[4..16] = r0..r12
#define CRASH_DUMP_REG_PC 0
#define CRASH_DUMP_REG_LR 1
#define CRASH_DUMP_REG_SP 2
#define CRASH_DUMP_REG_CPSR 3
#define CRASH_DUMP_REG_R0 4
#define CRASH_DUMP_REG_COUNT 17

// sp_base points at the stack location of the original first instruction's
// push result (the top of the stack at crash time).
void CrashDumpWrite(uint32_t hook_id, const uint32_t *regs, uint32_t sp_base);