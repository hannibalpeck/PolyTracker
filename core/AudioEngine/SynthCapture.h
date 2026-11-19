#ifndef AUDIOENGINE_SYNTHCAPTURE_H_
#define AUDIOENGINE_SYNTHCAPTURE_H_

#include "Audio.h"
#include <stdint.h>

// Listens to a stereo pair (left on input 0, right on input 1), mixes it to mono and stores it at a
// lower sample rate into a caller supplied buffer. It is used to render one synth note into a
// ProTracker sample. Costs nothing while idle.
class SynthCapture : public AudioStream
{
  public:
    SynthCapture() : AudioStream(2, inputQueueArray) {}

    // dest holds `frames` mono samples at outRate Hz
    void start(int16_t* dest, uint32_t frames, float outRate);
    void cancel() { state = kIdle; }
    bool isRunning() const { return state == kRunning; }
    bool isDone() const { return state == kDone; }
    uint32_t captured() const { return written; }
    int16_t peak() const { return peakValue; }

    void update() override;

  private:
    enum State : uint8_t
    {
        kIdle,
        kRunning,
        kDone
    };

    audio_block_t* inputQueueArray[2] = {};
    int16_t* buffer = nullptr;
    uint32_t capacity = 0;
    uint32_t written = 0;
    float step = 1.0f; // output samples per input sample
    float phase = 0.0f;
    float acc = 0.0f;
    float accCount = 0.0f;
    int16_t peakValue = 0;
    volatile State state = kIdle;
};

#endif
