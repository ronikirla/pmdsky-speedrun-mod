#include <pmdsky.h>
#include "eeprom.h"
#include "custom_headers.h"
#include "fixed_rng.h"
#include "speedrun_hud.h"
#include "optimizations.h"
#include "timer.h"
#include "aps.h"

static struct eeprom_timer eeprom_timer;
static struct eeprom_configurations eeprom_configurations;
#define EEPROM_MAGIC_ADDRESS 0xc7f0
#define EEPROM_MAGIC 0x67
#define EEPROM_TIMER_BASE_ADDRESS 0xb65c
#define EEPROM_CONFIGURATIONS_BASE_ADDRESS 0xb670
#define EEPROM_RNG_SEED_BASE_ADDRESS 0xb690

static bool igt_loaded = false;
static int eeprom_lock_id = -3;

// Serialize every mod backup (EEPROM) section on one shared lock owner. All
// section call sites share one CARD lock id, and CARDi_LockResource is
// reentrant BY ID across threads, so two threads could be inside
// Card_WriteAndVerifyEeprom at once and clobber the shared cardi_arg buffer
// (p->cmd) / the p->cur_th wakeup slot -> lost wakeup -> a thread sleeps
// forever holding lock_ref. One OS mutex around each whole
// Card_LockBackup..Card_UnlockBackup section makes that impossible.
// OS_LockMutex is per-thread reentrant (os_mutex::count), so nested sections
// on the same thread (LoadIGTAndConfigurations -> SaveIGT/SaveConfigurations)
// keep working. A BSS-zeroed os_mutex is already a valid empty mutex
// (OS_InitMutex writes exactly NULL/NULL/0), no init call needed.
static struct os_mutex eeprom_mutex;

// Shared CARD backup lock id, allocated once. Returns -1 when allocation
// failed. The -3 sentinel can NEVER leave this function: OS_LOCK_ID_ERROR
// (-3) is what OS_GetLockID returns when its pool is exhausted, and it
// collides with CARDi's "lock free" sentinel - Card_LockBackup(-3) would be
// treated as a reentrant entry on a free lock and Card_UnlockBackup would hit
// the "not locking" panic path. Valid ARM9 ids are 0x40..0x6F.
int GetEepromLockId(void) {
  if (eeprom_lock_id == -3) {
    int id = OS_GetLockID();
    if (id >= 0x40 && id <= 0x6F) {
      eeprom_lock_id = id;
    }
  }
  return (eeprom_lock_id >= 0x40 && eeprom_lock_id <= 0x6F) ? eeprom_lock_id : -1;
}

// Enter/leave a serialized backup section (see eeprom_mutex above). Pair
// every successful EepromLock with exactly one EepromUnlock.
bool EepromLock(void) {
  OS_LockMutex(&eeprom_mutex);
  if (GetEepromLockId() < 0) {
    OS_UnlockMutex(&eeprom_mutex);
    return false;
  }
  Card_LockBackup(eeprom_lock_id);
  return true;
}

void EepromUnlock(void) {
  Card_UnlockBackup(eeprom_lock_id);
  OS_UnlockMutex(&eeprom_mutex);
}

// Bounded-wait variant for the crash dump: spend at most ~timeout_ms waiting
// for the section (the dump must never block behind a wedged mod section -
// that is exactly when the dump is needed), then take the CARD lock and
// proceed regardless. Returns false only when no valid CARD lock id exists
// (caller must then skip its writes). If the wait timed out we proceed
// WITHOUT the section mutex (best-effort exclusion); EepromUnlock is still
// correct then, because OS_UnlockMutex is owner-checked.
bool EepromTryLock(uint32_t timeout_ms) {
  if (!OS_TryLockMutex(&eeprom_mutex)) {
    for (uint32_t waited = 0; waited < timeout_ms; waited++) {
      OS_Sleep(1);
      if (OS_TryLockMutex(&eeprom_mutex)) {
        break;
      }
    }
  }
  if (GetEepromLockId() < 0) {
    OS_UnlockMutex(&eeprom_mutex);
    return false;
  }
  Card_LockBackup(eeprom_lock_id);
  return true;
}

void SaveIGT(bool o30_check) {
  if (!igt_loaded || (o30_check && OverlayIsLoaded(OGROUP_OVERLAY_30))) {
    return;
  }
  if (!EepromLock()) {
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

  // Write IGT
  Card_WriteAndVerifyEeprom(EEPROM_TIMER_BASE_ADDRESS + eeprom_offset, &eeprom_timer.redundant_timers[new_index], 5);
  // Write index
  Card_WriteAndVerifyEeprom(EEPROM_TIMER_BASE_ADDRESS, &eeprom_timer.index, 1);
  EepromUnlock();
}

void SaveRNGSeedForSoftReset(void) {
  char rng_seed_save[RNG_INPUT_LEN + 1];
  memcpy(rng_seed_save, base_rng_text, RNG_INPUT_LEN + 1);

  if (!EepromLock()) {
    return;
  }
  Card_WriteAndVerifyEeprom(EEPROM_RNG_SEED_BASE_ADDRESS, rng_seed_save, RNG_INPUT_LEN + 1);
  EepromUnlock();
}

void SaveConfigurations(void) {
  if (!EepromLock()) {
    return;
  }
  eeprom_configurations.SRAM_hud_display_mode = hud_display_mode;
  eeprom_configurations.SRAM_optimization_mode = optimization_mode;
  eeprom_configurations.SRAM_file_timer = file_timer;
  eeprom_configurations.SRAM_start_time = start_time;
  eeprom_configurations.SRAM_show_idle_seconds = GetShowIdleSeconds();

  Card_WriteAndVerifyEeprom(EEPROM_CONFIGURATIONS_BASE_ADDRESS, &eeprom_configurations, sizeof(eeprom_configurations));
  EepromUnlock();
}

void LoadIGTAndConfigurations(void) {
  if (!EepromLock()) {
    return;
  }
  uint8_t magic;
  Card_ReadEeprom(EEPROM_MAGIC_ADDRESS, &magic, sizeof(uint8_t));
  if (magic != EEPROM_MAGIC) {
    SaveIGT(true);
    SaveConfigurations();
    magic = EEPROM_MAGIC;
    Card_WriteAndVerifyEeprom(EEPROM_MAGIC_ADDRESS, &magic, sizeof(uint8_t));
    goto CLEANUP;
  }

  // Read index
  Card_ReadEeprom(EEPROM_TIMER_BASE_ADDRESS, &eeprom_timer.index, 1);

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
  if (eeprom_configurations.SRAM_show_idle_seconds != 0xFF) {
    SetShowIdleSeconds(eeprom_configurations.SRAM_show_idle_seconds);
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
  // Clear the seed from EEPROM so hard resets don't restore it
  char empty_seed[RNG_INPUT_LEN + 1];
  memset(empty_seed, 0xFF, RNG_INPUT_LEN + 1);
  Card_WriteAndVerifyEeprom(EEPROM_RNG_SEED_BASE_ADDRESS, empty_seed, RNG_INPUT_LEN + 1);
  
  EepromUnlock();
  igt_loaded = true;
}

__attribute__((used)) int HijackNoteLoadBaseAndLoadIGT(void) {
  igt_loaded = false;
  int res = NoteLoadBase();
  LoadIGTAndConfigurations();
  return res;
}