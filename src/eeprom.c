#include <pmdsky.h>
#include "eeprom.h"
#include "custom_headers.h"
#include "fixed_rng.h"
#include "speedrun_hud.h"
#include "optimizations.h"
#include "timer.h"

struct eeprom_timer eeprom_timer;
struct eeprom_configurations eeprom_configurations;
#define EEPROM_TIMER_BASE_ADDRESS 0xb65c
#define EEPROM_CONFIGURATIONS_BASE_ADDRESS 0xb670
#define EEPROM_RNG_SEED_BASE_ADDRESS 0xb690

bool igt_loaded = false;
int eeprom_lock_id;

void SaveIGT(void) {
  if (!igt_loaded) {
    return;
  }
  int eeprom_offset = 0x0;
  int new_index = 0;
  if (eeprom_timer.index == 0) {
    new_index = 1;
    eeprom_offset = 0x6;
  }
  else {
    new_index = 0;
    eeprom_offset = 0x1;
  }

  eeprom_timer.index = new_index;
  eeprom_timer.redundant_timers[new_index].seconds = PLAY_TIME_SECONDS;
  eeprom_timer.redundant_timers[new_index].frames = PLAY_TIME_FRAME_COUNTER;

  Card_LockBackup(eeprom_lock_id);
  // Write IGT
  Card_WriteAndVerifyEeprom(EEPROM_TIMER_BASE_ADDRESS + eeprom_offset, &eeprom_timer.redundant_timers[new_index], 5);
  // Write index
  Card_WriteAndVerifyEeprom(EEPROM_TIMER_BASE_ADDRESS, &eeprom_timer.index, 1);
  Card_UnlockBackup(eeprom_lock_id);
}

void SaveRNGSeedForSoftReset(void) {
  char rng_seed_save[RNG_INPUT_LEN + 1];
  memcpy(rng_seed_save, base_rng_text, RNG_INPUT_LEN + 1);

  int lock_id = OS_GetLockID();
  Card_LockBackup(lock_id);
  Card_WriteAndVerifyEeprom(EEPROM_RNG_SEED_BASE_ADDRESS, rng_seed_save, RNG_INPUT_LEN + 1);
  Card_UnlockBackup(lock_id);
}

void SaveConfigurations(void) {
  eeprom_configurations.SRAM_hud_display_mode = hud_display_mode;
  eeprom_configurations.SRAM_optimization_mode = optimization_mode;
  eeprom_configurations.SRAM_file_timer = file_timer;
  eeprom_configurations.SRAM_start_time = start_time;

  eeprom_lock_id = OS_GetLockID();

  Card_LockBackup(eeprom_lock_id);
  Card_WriteAndVerifyEeprom(EEPROM_CONFIGURATIONS_BASE_ADDRESS, &eeprom_configurations, sizeof(eeprom_configurations));
  Card_UnlockBackup(eeprom_lock_id);
}

void LoadIGTAndConfigurations(void) {
  eeprom_lock_id = OS_GetLockID();

  Card_LockBackup(eeprom_lock_id);
  // Read index
  Card_ReadEeprom(EEPROM_TIMER_BASE_ADDRESS, &eeprom_timer.index, 1);
  if (eeprom_timer.index == 0xFF) {
    goto CLEANUP;
  }
  // Read Configurations
  Card_ReadEeprom(EEPROM_CONFIGURATIONS_BASE_ADDRESS, &eeprom_configurations, sizeof(eeprom_configurations));

  if (eeprom_configurations.SRAM_hud_display_mode < 0 || eeprom_configurations.SRAM_hud_display_mode >= HUD_DISPLAY_COUNT) {
    eeprom_configurations.SRAM_hud_display_mode = HUD_DISPLAY_NONE;
  }
  if (eeprom_configurations.SRAM_optimization_mode < 0 || eeprom_configurations.SRAM_optimization_mode >= OPTIMIZATION_MODE_COUNT) {
    eeprom_configurations.SRAM_optimization_mode = OPTIMIZATION_MODE_DEFAULT;
  }
  hud_display_mode = eeprom_configurations.SRAM_hud_display_mode;
  optimization_mode = eeprom_configurations.SRAM_optimization_mode;

  if (eeprom_configurations.SRAM_file_timer != 0xFF) {
    file_timer = eeprom_configurations.SRAM_file_timer;
    start_time = eeprom_configurations.SRAM_start_time;
  }
  
  // Load RNG seed from EEPROM and apply it, then clear from EEPROM (only survives soft reset)
  char rng_seed_loaded[RNG_INPUT_LEN + 1];
  Card_ReadEeprom(EEPROM_RNG_SEED_BASE_ADDRESS, rng_seed_loaded, RNG_INPUT_LEN + 1);
  // Check if there's a valid seed saved (not all 0xFF which means unwritten EEPROM)
  bool has_rng_seed = false;
  for (int i = 0; i < RNG_INPUT_LEN + 1; i++) {
    if (rng_seed_loaded[i] != (char)0xFF) {
      has_rng_seed = true;
      break;
    }
  }
  if (has_rng_seed) {
    SetFixedRNGSeed(rng_seed_loaded);
    // Clear the seed from EEPROM so hard resets don't restore it
    char empty_seed[RNG_INPUT_LEN + 1];
    memset(empty_seed, 0xFF, RNG_INPUT_LEN + 1);
    Card_WriteAndVerifyEeprom(EEPROM_RNG_SEED_BASE_ADDRESS, empty_seed, RNG_INPUT_LEN + 1);
  }

  // Need to update the HUD slot that corresponds to the currently active mode here too, or else it won't graphically show up
  AssignHUDSlots();

  // Do not load the time if there is no time in the save data (aka deleted save)
  if (PLAY_TIME_SECONDS == 0) {
    goto CLEANUP;
  }

  // Read IGT
  int eeprom_offset = 0x1 + eeprom_timer.index * 0x5;
  Card_ReadEeprom(EEPROM_TIMER_BASE_ADDRESS + eeprom_offset, &eeprom_timer.redundant_timers[eeprom_timer.index], 5);

  PLAY_TIME.seconds = eeprom_timer.redundant_timers[eeprom_timer.index].seconds;
  PLAY_TIME.frames = eeprom_timer.redundant_timers[eeprom_timer.index].frames;

  // Load fixed RNG state
  Card_ReadEeprom(EEPROM_RNG_STATE_BASE_ADDRESS, &fixed_rng_state, sizeof(fixed_rng_state));

CLEANUP:
  Card_UnlockBackup(eeprom_lock_id);
  igt_loaded = true;
}

__attribute__((used)) int HijackNoteLoadBaseAndLoadIGT(void) {
  igt_loaded = false;
  int res = NoteLoadBase();
  LoadIGTAndConfigurations();
  return res;
}