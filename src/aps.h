#pragma once
#include "hud.h"

void UpdateAPS(void);
void HandleAPSInput(void);
void ResetAPS(void);
void ResetAPSRemainingFrames(void);
bool GetShowIdleSeconds(void);
void SetShowIdleSeconds(bool val);

struct aps_split {
  char string[HUD_LEN];
  int remaining_frames; // Shared resource
  uint32_t idle_time;   // Backed up idle_time for split display
  uint32_t actions;     // Backed up actions for split display
};

struct aps_split* GetCurrentAPSSplit();