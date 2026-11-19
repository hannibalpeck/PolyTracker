#include "SynthCapture.h"

void SynthCapture::start(int16_t* dest, uint32_t frames, float outRate)
{
    state = kIdle;
    buffer = dest;
    capacity = frames;
    written = 0;
    step = outRate / AUDIO_SAMPLE_RATE_EXACT;
    phase = 0.0f;
    acc = 0.0f;
    accCount = 0.0f;
    peakValue = 0;
    state = kRunning; // armed last, the audio interrupt only looks at the rest when it is set
}

void SynthCapture::update()
{
    audio_block_t* left = receiveReadOnly(0);
    audio_block_t* right = receiveReadOnly(1);

    if (state == kRunning && buffer != nullptr)
    {
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES && written < capacity; i++)
        {
            const int32_t l = left ? left->data[i] : 0;
            const int32_t r = right ? right->data[i] : 0;
            acc += (float)(l + r) * 0.5f;
            accCount += 1.0f;
            phase += step;
            if (phase >= 1.0f)
            {
                phase -= 1.0f;
                const int16_t v = (int16_t)(acc / accCount);
                acc = 0.0f;
                accCount = 0.0f;
                buffer[written++] = v;
                const int16_t a = v < 0 ? (int16_t)-v : v;
                if (a > peakValue)
                    peakValue = a;
            }
        }
        if (written >= capacity)
            state = kDone;
    }

    if (left)
        release(left);
    if (right)
        release(right);
}
