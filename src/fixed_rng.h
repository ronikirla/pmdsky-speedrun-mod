#pragma once

#define RNG_INPUT_LEN 10

bool IsFixedRNG(void);
void SetFixedRNGSeed(char* buffer);

extern char base_rng_text[RNG_INPUT_LEN + 1];
