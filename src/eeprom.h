#pragma once
#include <pmdsky.h>
#include "speedrun_hud.h"
#include "optimizations.h"
#include "aps.h"

struct play_time_no_padding
{
  uint32_t seconds;
  uint8_t frames;
};

struct eeprom_timer
{
  uint8_t index;
  struct play_time_no_padding redundant_timers[2];
};

struct eeprom_configurations
{
  enum hud_display_mode SRAM_hud_display_mode;
  enum optimization_mode SRAM_optimization_mode;
  bool SRAM_file_timer;
  struct play_time SRAM_start_time;
  bool SRAM_show_idle_seconds;
};

void SaveIGT(bool o30_check);
void LoadIGT(void);
void SaveConfigurations(void);
void LoadIGTAndConfigurations(void);
void SaveRNGSeedForSoftReset(void);
int GetEepromLockId(void);
// Serialized backup-section lock (implementation in src/eeprom.c).
// EepromLock()/EepromUnlock() wrap each Card_LockBackup..Card_UnlockBackup
// section; EepromLock returns false when no valid CARD lock id exists and the
// caller must skip its writes. Nesting on one thread is allowed (needed by
// LoadIGTAndConfigurations -> SaveIGT/SaveConfigurations). EepromTryLock is
// the bounded-wait variant for the crash dump (never blocks past the timeout).
bool EepromLock(void);
void EepromUnlock(void);
bool EepromTryLock(uint32_t timeout_ms);
