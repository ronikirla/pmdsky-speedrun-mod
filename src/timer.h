#pragma once

// Return difference a - b in frames in play_time structs
int IGTDifferenceFrames(struct play_time* a, struct play_time* b);

void HandleTimerInput(void);

void UpdateTimer(void);