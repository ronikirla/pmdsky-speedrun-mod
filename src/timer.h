#pragma once

#define SPLIT_COLOR_TAG "[CS:G]"
#define SPLIT_SHOW_FRAMES 600

// Return difference a - b in frames in play_time structs
int IGTDifferenceFrames(struct play_time* a, struct play_time* b);

void HandleTimerInput(void);

bool IsLagging();

void UpdateTimer(void);