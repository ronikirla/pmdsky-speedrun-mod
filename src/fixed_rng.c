// Implements a set-seed RNG system where most RNG elements encountered during a playthrough
// are pre-determined based on a user-inputted seed. This can be used to synchronize the RNG
// between multiple runners in a race for a more even playing field.

#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"
#include "fixed_rng.h"
#include "speedrun_hud.h"

#define INPUT_LEN 10

// Fixed RNG base variables
bool fixed_rng = false;
char base_rng_text[INPUT_LEN + 1];
uint32_t base_rng_seed;
bool lock_rng_advances = false;

// Keyboard menu variables
char empty_result[] = "\1";

bool IsFixedRNG() {
  return fixed_rng;
}

// Fixed RNG state variables (saved to EEPROM)
struct fixed_rng_state fixed_rng_state = {
  .calls_per_scenario = 0,
  .scenario_prev = -1,
  .level_prev = -1,
};

// Reset the rng to a state derived from a base seed and the current save progress
// This can be called by other functions to give deterministic results for
// subsequent rng calls. This way each playthrough on the same seed
// gets the same RNG but the same RNG should not repeat in the same playthrough.
void ResetRngSeed() {
  if (!fixed_rng) {
    return;
  }
  
  int scenario;
  int level;
  if (GetGameMode() == GAME_MODE_SPECIAL_EPISODE) {
    scenario = LoadScriptVariableValueAtIndex(NULL, VAR_SCENARIO_SIDE, 0);
    level = LoadScriptVariableValueAtIndex(NULL, VAR_SCENARIO_SIDE, 1);
  } else {
    scenario = LoadScriptVariableValueAtIndex(NULL, VAR_SCENARIO_MAIN, 0);
    level = LoadScriptVariableValueAtIndex(NULL, VAR_SCENARIO_MAIN, 1);
  }
  
  // We want to advance the RNG if the player does not progress in the game,
  // but we want it to sync up again once they do progress.
  if (fixed_rng_state.scenario_prev != scenario || fixed_rng_state.level_prev != level) {
    fixed_rng_state.calls_per_scenario = 0;
  } else {
    fixed_rng_state.calls_per_scenario++;
  }
  //Debug_Print0("scenario: %x\n", scenario);
  //Debug_Print0("level: %x\n", level);
  //Debug_Print0("call per scenario: %x\n", fixed_rng_state.calls_per_scenario);
  fixed_rng_state.scenario_prev = scenario;
  fixed_rng_state.level_prev = level;

  // Save to EEPROM whenever the state changes
  int lock_id = OS_GetLockID();
  Card_LockBackup(lock_id);
  Card_WriteAndVerifyEeprom(EEPROM_RNG_STATE_BASE_ADDRESS, &fixed_rng_state, sizeof(fixed_rng_state));
  Card_UnlockBackup(lock_id);

  uint16_t seed = base_rng_seed ^ scenario ^ (level << 5) ^ (fixed_rng_state.calls_per_scenario << 12);
  //Debug_Print0("seed: %x\n", seed);
  SetRngSeed(seed);
  Rand16Bit();
}

// Hijacking functions that can be jumped into from ASM.

__attribute__((used)) void HijackSetDungeonRngPreseedAndResetRngSeed() {
  ResetRngSeed();
  // We can use the full 32 bits of the base seed here because dungeon RNG is 32-bit
  int32_t preseed = (base_rng_seed & 0xFFFF0000) | Rand16Bit();
  //Debug_Print0("preseed: %x\n", preseed);
  SetDungeonRngPreseed(preseed);
}

__attribute__((used)) void HijackGenerateKecleonItems1AndLockRngAdvances(enum kecleon_shop_version kecleon_shop_version) {
  lock_rng_advances = true;
  ResetRngSeed();
  GenerateKecleonItems1(kecleon_shop_version);
}

__attribute__((used)) void HijackGenerateKecleonItems2AndUnlockRngAdvances(enum kecleon_shop_version kecleon_shop_version) {
  GenerateKecleonItems2(kecleon_shop_version);
  lock_rng_advances = false;
}

__attribute__((used)) void HijackGenerateCroagunkItemsAndUnlockRngAdvances(enum kecleon_shop_version kecleon_shop_version) {
  GenerateCroagunkItems();
  lock_rng_advances = false;
}

__attribute__((used)) uint32_t HijackRandIntAndResetRngSeed(int high) {
  ResetRngSeed();
  return RandInt(high);
}

__attribute__((used)) uint32_t HijackRandRangeAndResetRngSeed(int x, int y) {
  ResetRngSeed();
  return RandRange(x, y);
}

// Enable the team name change in the main menu. This is used to set the RNG seed into a user-inputted value.
__attribute__((used)) bool HijackIsAdventureLogNotEmptyAndAddRenameTeam() {
  AddSubMenuOption(8, true);
  return IsAdventureLogNotEmpty();
}

// Update the seed text displayed in the HUD
void UpdateSeedDisplay(char* buffer, bool empty) {
  char string[INPUT_LEN + 3] = ""; // "S: " = 3 characters
  if (!empty) {
    strcat(string, "S: ");
    strcat(string, buffer);
  }
  UpdateHUDString(SPEEDRUN_HUD_SEED, string, 0);
}

// Set the rng seed to the value passed in the buffer
__attribute__((used)) void SetFixedRNGSeed(char* buffer) {
  //Debug_Print0("base seed: %x", base_rng_seed);
  bool empty = strncmp(buffer, empty_result, INPUT_LEN) == 0;
  UpdateSeedDisplay(buffer, empty);
  // If the keyboard was left empty, disable fixed RNG
  if (empty) {
    fixed_rng = false;
    return;
  }
  fixed_rng = true;
  strncpy(base_rng_text, buffer, INPUT_LEN);
  base_rng_seed = 0;
  // Possible number of inputs of char[10] far exceeds 2^32. Overflows act as modulo.
  for (uint16_t i = 0; i < INPUT_LEN; i++) {
    base_rng_seed += (buffer[i]) * (i + 1) * 256;
  }
  //Debug_Print0("base seed: %x\n", base_rng_seed);
}

// Replace the provided default value in the keyboard with a Rand16Bit() call result
// or the previously inputted seed
__attribute__((used)) int ShowKeyboardWithRandomDefaultValue(int message_id, char* buffer, int param_3, char* empty_fallback) {
  static char keyboard_default[INPUT_LEN + 1];

  if (fixed_rng) {
    strncpy(keyboard_default, base_rng_text, INPUT_LEN);
  } else {
    snprintf(keyboard_default, INPUT_LEN, "%d", Rand16Bit());
  }
  return ShowKeyboard(message_id, keyboard_default, param_3, empty_result);
}

__attribute__((used)) void Rand16BitIfAdvancesNotLocked(void) {
  if (!lock_rng_advances) {
    Rand16Bit();
  }
}
