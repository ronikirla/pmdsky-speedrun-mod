#pragma once

#define SPLIT_COLOR_TAG "[CS:G]"
#define SPLIT_SHOW_FRAMES 600

// Return difference a - b in frames in play_time structs
int IGTDifferenceFrames(struct play_time* a, struct play_time* b);

// Return true if the play time has not advanced and we are not in the main menu
bool IsLagging();

void HandleTimerInput(void);

void UpdateTimer(void);