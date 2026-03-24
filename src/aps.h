#pragma once
#include "hud.h"

void UpdateAPS(void);
void ResetAPS(void);
void ResetAPSRemainingFrames(void);

struct aps_split {
  char string[HUD_LEN];
  int remaining_frames; // Shared resource
};

struct aps_split* GetCurrentAPSSplit();