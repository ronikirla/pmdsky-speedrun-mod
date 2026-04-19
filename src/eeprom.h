#pragma once
#include <pmdsky.h>
#include "speedrun_hud.h"
#include "optimizations.h"

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
};

void SaveIGT(void);
void LoadIGT(void);
void SaveConfigurations(void);
void LoadIGTAndConfigurations(void);