#ifndef EFFECTOR_MTEFFECTOCTAVE_H_
#define EFFECTOR_MTEFFECTOCTAVE_H_

#include "mtEffect.h"

// Moves the selection by whole octaves by resampling it: one octave up halves it (the points that
// are merged are averaged), one octave down doubles it (new points are interpolated).
class mtEffectOctave : public mtEffect
{
  public:
    static constexpr int kMaxShift = 3;

    virtual void setParameter(float& value, int idx) override;
    virtual bool startProcess() override;
    virtual int32_t processBlock() override;
    virtual bool getProcessState() override;
    virtual uint32_t getExpectedProcessLength() override;

  private:
    int shift = 0;
    uint32_t length = 0;
    uint32_t outLength = 0;
    uint32_t produced = 0;
    bool state = false;
};

extern mtEffectOctave sampleEditorOctave;

#endif /* EFFECTOR_MTEFFECTOCTAVE_H_ */
