#pragma once
#include <pmdsky.h>

struct play_time_no_padding {
  uint32_t seconds;
  uint8_t frames;
};

struct eeprom_timer {
  uint8_t index;
  struct play_time_no_padding redundant_timers[2];
};