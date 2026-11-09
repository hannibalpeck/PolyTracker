#include "mtEffectOctave.h"

mtEffectOctave sampleEditorOctave;

static uint32_t octaveLength(uint32_t length, int shift)
{
    if (shift > 0)
        return length >> shift;
    if (shift < 0)
        return length << (-shift);
    return length;
}

void mtEffectOctave::setParameter(float& value, int idx)
{
    if (idx != 0)
        return;

    int v = (int)(value >= 0.0f ? value + 0.5f : value - 0.5f);
    if (v > kMaxShift)
        v = kMaxShift;
    if (v < -kMaxShift)
        v = -kMaxShift;
    shift = v;
}

bool mtEffectOctave::startProcess()
{
    length = confirmed[0].selection.length;
    outLength = octaveLength(length, shift);
    produced = 0;
    state = outLength > 0;
    return state;
}

int32_t mtEffectOctave::processBlock()
{
    if (!state)
        return -1;

    uint32_t block = outLength - produced;
    if (block > 8192)
        block = 8192;

    for (int channel = 0; channel < kChannels; channel++)
    {
        const int16_t* src = confirmed[channel].selection.addr;
        int16_t* dst = processed[channel].selection.addr;

        for (uint32_t n = 0; n < block; n++)
        {
            const uint32_t j = produced + n;
            int32_t out;

            if (shift > 0)
            {
                const uint32_t f = 1u << shift;
                int32_t sum = 0;
                for (uint32_t t = 0; t < f; t++)
                    sum += src[j * f + t];
                out = sum / (int32_t)f;
            }
            else if (shift < 0)
            {
                const uint32_t f = 1u << (-shift);
                const uint32_t k = j / f;
                const int32_t r = (int32_t)(j % f);
                const int32_t s0 = src[k];
                const int32_t s1 = src[(k + 1 < length) ? k + 1 : k];
                out = s0 + ((s1 - s0) * r) / (int32_t)f;
            }
            else
            {
                out = src[j];
            }
            dst[j] = (int16_t)out;
        }
    }

    produced += block;

    if (produced >= outLength)
    {
        state = false;
        for (int channel = 0; channel < kChannels; channel++)
        {
            processed[channel].selection.length = outLength;
            processed[channel].area.length =
              confirmed[channel].area.length + (outLength - confirmed[channel].selection.length);
        }
    }

    return (int32_t)block;
}

bool mtEffectOctave::getProcessState()
{
    return state;
}

uint32_t mtEffectOctave::getExpectedProcessLength()
{
    return octaveLength(confirmed[0].selection.length, shift);
}
