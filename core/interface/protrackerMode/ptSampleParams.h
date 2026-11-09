#ifndef INTERFACE_PROTRACKERMODE_PTSAMPLEPARAMS_H_
#define INTERFACE_PROTRACKERMODE_PTSAMPLEPARAMS_H_

#ifdef PLATFORM_IMX

#include "core/modulesBase.h"
#include "display.h"
#include "elapsedMillis.h"

// The ProTracker sample parameters (Sample Play button): the fields of a .mod sample header --
// volume, finetune, length and the loop -- with the waveform and the loop markers. Samples are
// always played "1-Shot" (no loop) or "Forward Loop", which is all ProTracker has.
class cProTrackerSampleParams : public ModuleBase
{
  public:
    enum Param : uint8_t
    {
        kSample,
        kVolume,
        kFineTune,
        kLoop,
        kLoopStart,
        kLoopEnd,
        kLength,
        kOctave,
        kParamCount
    };

    cProTrackerSampleParams() {}
    ~cProTrackerSampleParams() {}

    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();
    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    void bindButtons();
    void select(int param);
    void change(int delta, bool big);
    void resetParam();
    void refreshAll();
    void refreshWave();
    void refreshPoints();
    void refreshTexts();
    void playNote(int note, bool on);
    void octave(int direction);
    void undoOctave();

    uint8_t selectedParam = kVolume;
    bool needRefreshWave = false;

    hControl label[8] = {};
    hControl bgLabel = -1;
    hControl frameControl = -1;
    hControl spectrum = -1;
    hControl points = -1;
    hControl playhead = -1;
    elapsedMillis playheadTimer;
    bool playheadShown = false;

    strFrameData frameData;
    strTrackerPoints pointsData;
};

extern cProTrackerSampleParams protrackerSampleParams;

#endif // PLATFORM_IMX

#endif /* INTERFACE_PROTRACKERMODE_PTSAMPLEPARAMS_H_ */
