#pragma once

enum optimization_mode {
  OPTIMIZATION_MODE_THROTTLE = 0,
  OPTIMIZATION_MODE_DEFAULT = 1,
  OPTIMIZATION_MODE_FAST = 2
};

char* GetOptimizationModeString(void);

enum optimization_mode GetOptimizationMode();

void HandleSpeedToggle(void);