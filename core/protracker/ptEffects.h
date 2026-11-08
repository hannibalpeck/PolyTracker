#pragma once
#include <stdint.h>

// The ProTracker effect commands as they are offered in the Fx list.
// command: 0..15, paramHigh: fixed high nibble of the parameter for the E-commands (-1 = free).
namespace protracker
{
struct EffectInfo
{
    uint8_t command;
    int8_t paramHigh;
    const char* name;
    const char* description;
};

static constexpr int kEffectCount = 29;

static constexpr EffectInfo kEffects[kEffectCount] = {
    {0x0, -1, "0xy Arpeggio", "Plays the note, +x and +y semitones in turn"},
    {0x1, -1, "1xx Slide up", "Raises the pitch xx every tick"},
    {0x2, -1, "2xx Slide down", "Lowers the pitch xx every tick"},
    {0x3, -1, "3xx Tone portamento", "Slides to the note, speed xx"},
    {0x4, -1, "4xy Vibrato", "x speed, y depth"},
    {0x5, -1, "5xy Porta+Vol slide", "Continues 3xx, volume slide x up / y down"},
    {0x6, -1, "6xy Vibrato+Vol slide", "Continues 4xy, volume slide x up / y down"},
    {0x7, -1, "7xy Tremolo", "x speed, y depth"},
    {0x9, -1, "9xx Sample offset", "Starts the sample at xx * 256 bytes"},
    {0xA, -1, "Axy Volume slide", "x up / y down every tick"},
    {0xB, -1, "Bxx Position jump", "Jumps to song position xx"},
    {0xC, -1, "Cxx Set volume", "Volume 00..40"},
    {0xD, -1, "Dxx Pattern break", "Next pattern, row xx (decimal as hex digits)"},
    {0xE, 0x0, "E0x Filter", "x=0 filter on, x=1 off"},
    {0xE, 0x1, "E1x Fine slide up", "Raises the pitch x once per row"},
    {0xE, 0x2, "E2x Fine slide down", "Lowers the pitch x once per row"},
    {0xE, 0x3, "E3x Glissando", "x=1 tone portamento in semitones"},
    {0xE, 0x4, "E4x Vibrato waveform", "0 sine, 1 ramp, 2 square (+4 no retrigger)"},
    {0xE, 0x5, "E5x Set finetune", "x=0..F, 8..F are -8..-1"},
    {0xE, 0x6, "E6x Pattern loop", "x=0 sets the start, x>0 loops x times"},
    {0xE, 0x7, "E7x Tremolo waveform", "0 sine, 1 ramp, 2 square (+4 no retrigger)"},
    {0xE, 0x9, "E9x Retrigger", "Retriggers the note every x ticks"},
    {0xE, 0xA, "EAx Fine vol up", "Raises the volume x once per row"},
    {0xE, 0xB, "EBx Fine vol down", "Lowers the volume x once per row"},
    {0xE, 0xC, "ECx Note cut", "Cuts the note after x ticks"},
    {0xE, 0xD, "EDx Note delay", "Delays the note x ticks"},
    {0xE, 0xE, "EEx Pattern delay", "Repeats the row x times"},
    {0xE, 0xF, "EFx Invert loop", "Funk repeat, speed x"},
    {0xF, -1, "Fxx Speed / Tempo", "01..1F speed (ticks), 20..FF tempo (BPM)"},
};

// index in kEffects for a command / parameter pair (the free entry when the E sub-command is unknown)
static inline int effectIndex(uint8_t command, uint8_t param)
{
    for (int i = 0; i < kEffectCount; i++)
    {
        if (kEffects[i].command != command)
            continue;
        if (kEffects[i].paramHigh < 0 || kEffects[i].paramHigh == (param >> 4))
            return i;
    }
    return 0;
}
} // namespace protracker
