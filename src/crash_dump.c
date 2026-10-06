#include <pmdsky.h>
#include "custom_headers.h"
#include "eeprom.h"
#include "crash_dump.h"

// First hook to fire wins; later calls (e.g. a FatalError that cascades into
// OS_Panic) must not overwrite the record.
static volatile uint32_t crash_dump_active = 0;

void CrashDumpWrite(uint32_t hook_id, const uint32_t *regs, uint32_t sp_base) {
  if (crash_dump_active) {
    return;
  }
  crash_dump_active = 1;

  // The card driver's synchronous write spin-waits while yielding and the
  // EEPROM programming is performed by the card thread, so the dump relies on
  // normal thread semantics. Skip it when the crash happened in IRQ/FIQ mode;
  // the stub still continues into the original halt path.
  uint32_t mode = regs[CRASH_DUMP_REG_CPSR] & 0x1C0;
  if (mode == 0x110 || mode == 0x120) {
    return;
  }

  uint32_t header[CRASH_DUMP_HEADER_SIZE / 4];
  for (uint32_t i = 0; i < CRASH_DUMP_HEADER_SIZE / 4; i++) {
    header[i] = 0;
  }
  header[CRASH_DUMP_OFF_MAGIC / 4] = CRASH_DUMP_MAGIC;
  header[CRASH_DUMP_OFF_VERSION / 4] = CRASH_DUMP_VERSION;
  header[CRASH_DUMP_OFF_HOOK_ID / 4] = hook_id;
  header[CRASH_DUMP_OFF_TICK / 4] = OS_GetTickLo();
  header[CRASH_DUMP_OFF_PC / 4] = regs[CRASH_DUMP_REG_PC];
  header[CRASH_DUMP_OFF_LR / 4] = regs[CRASH_DUMP_REG_LR];
  header[CRASH_DUMP_OFF_SP / 4] = regs[CRASH_DUMP_REG_SP];
  header[CRASH_DUMP_OFF_CPSR / 4] = regs[CRASH_DUMP_REG_CPSR];
  for (uint32_t i = 0; i < 13; i++) {
    header[(CRASH_DUMP_OFF_R0 / 4) + i] = regs[CRASH_DUMP_REG_R0 + i];
  }
  uint32_t checksum = 0;
  for (uint32_t i = 0; i < CRASH_DUMP_OFF_CHECKSUM / 4; i++) {
    checksum += header[i];
  }
  header[CRASH_DUMP_OFF_CHECKSUM / 4] = checksum;
  header[CRASH_DUMP_OFF_ARG0 / 4] = regs[CRASH_DUMP_REG_R0];
  header[CRASH_DUMP_OFF_ARG1 / 4] = regs[CRASH_DUMP_REG_R0 + 1];
  header[CRASH_DUMP_OFF_ARG2 / 4] = regs[CRASH_DUMP_REG_R0 + 2];
  header[CRASH_DUMP_OFF_ARG3 / 4] = regs[CRASH_DUMP_REG_R0 + 3];

  // The FatalError format string (the "official" crash reason) is the original
  // r1 (ABI: r0 = &{file,line} prog_pos, r1 = fmt, r2.. = variadic), which the
  // hook stub already saved into regs[CRASH_DUMP_REG_R1]. OS_Panic takes no
  // arguments, so a message only exists on the FatalError path. Copy it only
  // when the pointer falls in the cartridge region, where all the game's
  // string literals live - reads there are always safe, and a corrupted
  // pointer outside the range is rejected.
  if (hook_id == CRASH_DUMP_HOOK_FATAL_ERROR) {
    uint32_t fmt = regs[CRASH_DUMP_REG_R0 + 1];
    if (fmt >= 0x02000000 && fmt < 0x08000000) {
      const uint8_t *src = (const uint8_t *)fmt;
      uint8_t *dst = (uint8_t *)header;
      uint32_t len = 0;
      while (len < CRASH_DUMP_MSG_MAX && src[len] != 0) {
        dst[CRASH_DUMP_OFF_MSG + len] = src[len];
        len++;
      }
      header[CRASH_DUMP_OFF_MSG_LEN / 4] = len;
    }
  }

  // Note: the scheduler is deliberately left enabled. The synchronous card
  // write (Cardi_RequestStreamCommand -> Cardi_RequestStreamCommandCore)
  // spin-waits with OSi_RescheduleThread and the actual EEPROM programming
  // runs on the card thread, so disabling the scheduler here would hang the
  // write before a single byte is written. The lock is reentrant for the
  // owning thread, so a crash while the mod's own save code holds it is safe.
  // The original hook path halts the game regardless, so the state left here
  // does not matter afterwards.
  int lock_id = GetEepromLockId();
  Card_LockBackup(lock_id);
  Card_WriteAndVerifyEeprom(CRASH_DUMP_EEPROM_BASE, header, CRASH_DUMP_HEADER_SIZE);
  for (uint32_t i = 0; i < CRASH_DUMP_STACK_SIZE / CRASH_DUMP_STACK_CHUNK_SIZE; i++) {
    uint32_t chunk[CRASH_DUMP_STACK_CHUNK_SIZE / 4];
    const uint32_t *src = (const uint32_t *)(sp_base + i * CRASH_DUMP_STACK_CHUNK_SIZE);
    for (uint32_t j = 0; j < CRASH_DUMP_STACK_CHUNK_SIZE / 4; j++) {
      chunk[j] = src[j];
    }
    Card_WriteAndVerifyEeprom(CRASH_DUMP_EEPROM_BASE + CRASH_DUMP_STACK_OFFSET + i * CRASH_DUMP_STACK_CHUNK_SIZE,
                              chunk, CRASH_DUMP_STACK_CHUNK_SIZE);
  }
  Card_UnlockBackup(lock_id);
}