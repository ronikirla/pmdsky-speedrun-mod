#pragma once

enum optimization_mode
{
  OPTIMIZATION_MODE_THROTTLE_DS = 0,
  OPTIMIZATION_MODE_THROTTLE_EMU = 1,
  OPTIMIZATION_MODE_DEFAULT = 2,
  OPTIMIZATION_MODE_FAST = 3,
  OPTIMIZATION_MODE_RNG_VIEWER = 4, // Spaghetti, not really an optimization mode but eh

  // sentinel value, always keep last
  OPTIMIZATION_MODE_COUNT
};

extern enum optimization_mode optimization_mode;

char *GetOptimizationModeString(void);

enum optimization_mode GetOptimizationMode();

void HandleSpeedToggle(void);