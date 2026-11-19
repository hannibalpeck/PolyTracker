#ifndef INTERFACE_PROTRACKERMODE_PTCHIPTUNE_H_
#define INTERFACE_PROTRACKERMODE_PTCHIPTUNE_H_

#ifdef PLATFORM_IMX

#include "core/modulesBase.h"
#include "display.h"
#include "elapsedMillis.h"

// The Instrument Parameters button: the built-in chiptune synth with its six macros. Pressing Play
// renders one note of it, as 8-bit mono, into the current ProTracker sample slot.
class cProTrackerChiptune : public ModuleBase
{
  public:
    enum Param : uint8_t
    {
        kMacro1,
        kMacro2,
        kMacro3,
        kMacro4,
        kMacro5,
        kMacro6,
        kNote,
        kLength,
        kParamCount
    };

    cProTrackerChiptune() {}
    ~cProTrackerChiptune() {}

    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();
    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    static Synth* getSynth() { return synth; }
    void bindButtons();
    void select(int param);
    void change(int delta, bool big);
    void resetParam();
    void refreshTexts();
    void render();
    void finishRender();
    void padNote(uint8_t pad, bool on);

    uint8_t selectedParam = kMacro1;
    uint8_t note = 48;       // midi note that is rendered
    uint16_t lengthMs = 1000; // how long is captured

    enum RenderState : uint8_t
    {
        kIdle,
        kRendering
    };
    RenderState renderState = kIdle;
    bool noteHeld = false;
    elapsedMillis renderTimer;
    bool messageShown = false;
    elapsedMillis messageTimer;
    int8_t padPlaying[48];

    hControl label[8] = {};
    hControl bgLabel = -1;
    hControl frameControl = -1;

    strFrameData frameData;
};

extern cProTrackerChiptune protrackerChiptune;

#endif // PLATFORM_IMX

#endif /* INTERFACE_PROTRACKERMODE_PTCHIPTUNE_H_ */
