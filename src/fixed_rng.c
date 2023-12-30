// Implements a set-seed RNG system where most RNG elements encountered during a playthrough
// are pre-determined based on a user-inputted seed. This can be used to synchronize the RNG
// between multiple runners in a race for a more even playing field.

#include <pmdsky.h>
#include <cot.h>
#include "custom_headers.h"
#include "speedrun_hud.h"

#define INPUT_LEN 10

// Fixed RNG base variables
bool fixed_rng = false;
char base_rng_text[INPUT_LEN + 1];
uint32_t base_rng_seed;

// Fixed RNG state variables
uint32_t calls_per_scenario = 0;
int scenario_prev = -1;
int level_prev = -1;

// Keyboard menu variables
char keyboard_default[INPUT_LEN + 1];
char empty_result[] = "\1";

bool IsFixedRNG() {
  return fixed_rng;
}

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
  if (scenario_prev != scenario || level_prev != level) {
    calls_per_scenario = 0;
  } else {
    calls_per_scenario++;
  }
  DebugPrint0("scenario: %x\n", scenario);
  DebugPrint0("level: %x\n", level);
  DebugPrint0("call per scenario: %x\n", calls_per_scenario);
  scenario_prev = scenario;
  level_prev = level;

  uint16_t seed = base_rng_seed ^ scenario ^ (level << 5) ^ (calls_per_scenario << 12);
  DebugPrint0("seed: %x\n", seed);
  SetRngSeed(seed);
}

// Hijacking functions that can be jumped into from ASM.

__attribute__((used)) void HijackSetDungeonRngPreseedAndResetRngSeed() {
  ResetRngSeed();
  // We can use the full 32 bits of the base seed here because dungeon RNG is 32-bit
  int32_t preseed = (base_rng_seed & 0xFFFF0000) | Rand16Bit();
  DebugPrint0("preseed: %x\n", preseed);
  SetDungeonRngPreseed(preseed);
}

__attribute__((used)) void HijackGenerateKecleonItems1AndResetRngSeed(enum kecleon_shop_version kecleon_shop_version) {
  ResetRngSeed();
  GenerateKecleonItems1(kecleon_shop_version);
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
  DebugPrint0("base seed: %x", base_rng_seed);
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
  DebugPrint0("base seed: %x\n", base_rng_seed);
}

// Replace the provided default value in the keyboard with a Rand16Bit() call result
// or the previously inputted seed
__attribute__((used)) int ShowKeyboardWithRandomDefaultValue(int message_id, char* buffer, int param_3, char* empty_fallback) {
  if (fixed_rng) {
    strncpy(keyboard_default, base_rng_text, INPUT_LEN);
  } else {
    snprintf(keyboard_default, INPUT_LEN, "%d", Rand16Bit());
  }
  return ShowKeyboard(message_id, keyboard_default, param_3, empty_result);
}