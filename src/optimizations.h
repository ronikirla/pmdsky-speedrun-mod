#pragma once

enum optimization_mode {
  OPTIMIZATION_MODE_THROTTLE = 0,
  OPTIMIZATION_MODE_DEFAULT = 1,
  OPTIMIZATION_MODE_FAST = 2,
  OPTIMIZATION_MODE_RNG_VIEWER = 3 // Spaghetti, not really an optimization mode but eh
};

char* GetOptimizationModeString(void);

enum optimization_mode GetOptimizationMode();

void HandleSpeedToggle(void);