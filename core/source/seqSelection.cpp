#include "seqSelection.h"
#include "mtSequencer.h"

// Notes arriving over MIDI are routed to the sequencer; with no pattern grid cursor they use
// step 0 / track 0.
void sendSelection()
{
    sequencer.setSelection(0, 0, 0, 0);
}

uint8_t isMultiSelection()
{
    return 0;
}

uint8_t isEditMode()
{
    return 0;
}

int16_t getActualTrack()
{
    return 0;
}

// The performance screen no longer exists, so there is nothing to turn off.
void cbkTurnOffPerformanceMode(void)
{
}
