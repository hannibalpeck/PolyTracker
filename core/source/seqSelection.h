#pragma once
// Small helpers that used to live in the stock pattern editor screen. The ProTracker firmware has
// no sequencer grid cursor, so they report "no cursor / no multi-selection".

#include <stdint.h>

void sendSelection();
uint8_t isMultiSelection();
uint8_t isEditMode();
int16_t getActualTrack();
void cbkTurnOffPerformanceMode(void);
