#pragma once

#define RNG_INPUT_LEN 10

bool IsFixedRNG(void);
void SetFixedRNGSeed(char* buffer);

struct fixed_rng_state {
  uint32_t calls_per_scenario;
  int scenario_prev;
  int level_prev;
};

extern char base_rng_text[RNG_INPUT_LEN + 1];
extern struct fixed_rng_state fixed_rng_state;

#define EEPROM_RNG_STATE_BASE_ADDRESS 0xb69B
