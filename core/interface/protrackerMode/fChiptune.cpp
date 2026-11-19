#ifdef PLATFORM_IMX

#include "protrackerMode/ptChiptune.h"

#include <stdio.h>
#include <string.h>

#include "Synth.h"
#include "mtAudioEngine.h"
#include "Platform.h"
#include "core/interfaceDefs.h"
#include "core/interfacePopups.h"
#include "core/titleBar.h"
#include "mtStructs.h"
#include "ptSamples.h"
#include "protrackerMode/protrackerMode.h"
#include "util_math.h"

cProTrackerChiptune protrackerChiptune;
static cProTrackerChiptune* CT = &protrackerChiptune;
static cProTrackerMode* PT = &protrackerMode;

extern int16_t* externalRamEffectsBuff;
// the captured note, 16 bit, waits here until it is converted to 8 bit (the effect buffer is idle)
#define captureBuf (externalRamEffectsBuff + 524288)

// sample rate at which a sample sounds at its natural pitch when it is played at C-3 (period 428)
static constexpr float kRenderRate = 8287.14f;
static constexpr uint32_t kMaxFrames = protracker::kMaxSampleBytes; // one slot
static constexpr uint16_t kMaxLengthMs = 7900;                       // < kMaxFrames / kRenderRate
static constexpr uint16_t kMinLengthMs = 100;

static const uint16_t kFramePlaces[8][4] = {
    { (800 / 8) * 0 + 1, 424, 800 / 8 - 3, 55 }, { (800 / 8) * 1 + 1, 424, 800 / 8 - 3, 55 },
    { (800 / 8) * 2 + 1, 424, 800 / 8 - 3, 55 }, { (800 / 8) * 3 + 1, 424, 800 / 8 - 3, 55 },
    { (800 / 8) * 4 + 1, 424, 800 / 8 - 3, 55 }, { (800 / 8) * 5 + 1, 424, 800 / 8 - 3, 55 },
    { (800 / 8) * 6 + 1, 424, 800 / 8 - 3, 55 }, { (800 / 8) * 7 + 1, 424, 800 / 8 - 3, 55 },
};

static char s_title[8][24];
static char s_text2[8][16];
static char s_msg[40];

static void functSwitchModule(uint8_t button);
static void functUp();
static void functDown();
static void functLeft();
static void functRight();
static void functEncoder(int16_t value);
static void functDelete();
static void functSelect(uint8_t button);
static void functPlay();
static void functPads(uint8_t pad, uint8_t state, int16_t velo);

static Synth* sy()
{
    return cProTrackerChiptune::getSynth();
}

static SynthPartSettings& part()
{
    return mtProject.values.synthPart[0];
}

static const char* noteName(int midi)
{
    static const char* const names[12] = { "C", "C#", "D", "D#", "E", "F",
                                           "F#", "G", "G#", "A", "A#", "B" };
    static char text[8];
    snprintf(text, sizeof(text), "%s%d", names[midi % 12], midi / 12 - 1);
    return text;
}

static bool macroBipolar(int idx)
{
    return sy()->getPatch(0).getMetadata().macros[idx].bipolar;
}

static int macroMax(int idx)
{
    return macroBipolar(idx) ? kCCMidValue * 2 : kCCMaxValue;
}

static void applyMacro(int idx)
{
    engine.setSynthParam(audioEngine::SynthParam(audioEngine::SynthParam::kMacro1 + idx),
                         0,
                         part().macroValue[idx],
                         audioEngine::SynthValuesSource::kInstrument);
}

void cProTrackerChiptune::initDisplayControls()
{
    frameData.placesCount = 8;
    for (uint8_t i = 0; i < 8; i++)
        frameData.places[i] = &kFramePlaces[i][0];

    strControlProperties prop;
    prop.style = 0;
    prop.value = 0;
    prop.data = &frameData;
    frameControl = display.createControl<cFrame>(&prop);

    strControlProperties labelProperties;
    for (uint8_t i = 0; i < 8; i++)
    {
        labelProperties.value = 1;
        labelProperties.colors = display.getTheme().activeButtonLabelsColors;
        labelProperties.style = (controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
        labelProperties.x = (800 / 8) * i + (800 / 16);
        labelProperties.w = 800 / 8 - 6;
        labelProperties.y = 424;
        labelProperties.h = 55;
        label[i] = display.createControl<cLabel>(&labelProperties);
    }

    strControlProperties backgroundLabelProperties;
    backgroundLabelProperties.text = nullptr;
    backgroundLabelProperties.colors = nullptr;
    backgroundLabelProperties.value = 255;
    backgroundLabelProperties.style = controlStyleNoTransparency | controlStyleShow;
    backgroundLabelProperties.x = 0;
    backgroundLabelProperties.w = 800;
    backgroundLabelProperties.y = 425;
    backgroundLabelProperties.h = 54;
    bgLabel = display.createControl<cBgLabel>(&backgroundLabelProperties);
}

void cProTrackerChiptune::bindButtons()
{
    ip->clearAllButtons();
    ip->clearAllPots();
    ip->clearGlobalPads();

    for (uint8_t b = interfaceButton0; b <= interfaceButton7; b++)
        ip->setButtonObj(b, buttonPress, functSelect);

    ip->setButtonObj(interfaceButtonUp, buttonPress, functUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDown);
    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functRight);
    ip->setButtonObj(interfaceButtonDelete, buttonPress, functDelete);
    ip->setButtonObj(interfaceButtonPlay, buttonPress, functPlay);
    ip->setPotObj(interfacePot0, functEncoder);
    ip->setPadsGlobal(functPads);

    ip->setButtonObj(interfaceButtonFile, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonPattern, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSong, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonConfig, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonMaster, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleEdit, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleRec, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleLoad, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSamplePlay, buttonPress, functSwitchModule);
}

void cProTrackerChiptune::start(uint32_t)
{
    moduleRefresh = 1;
    PT->stream->setEnabled(true);
    PT->stopPreview();
    renderState = kIdle;
    messageShown = false;
    for (auto& p : padPlaying)
        p = -1;

#ifdef FEATURE_SYNTH
    // part 1 becomes the chiptune synth (once; its macros are kept after that)
    constexpr int kChiptuneType = 6;
    SynthPartSettings& s = part();
    if (s.synthType != kChiptuneType || sy()->getInstrumentTypeIdx(0) != kChiptuneType)
    {
        s.synthType = kChiptuneType;
        s.voiceCount = 1;
        s.gainDb = Synth::kGainDefault;
        s.panning = Synth::kPanningDefault;
        s.reverbSend = 0;
        s.delaySend = 0;
        sy()->updateToSettings(mtProject.values);
        sy()->initSynthPartMacrosValues(0, s);
    }
    engine.refreshSynthInstrumentParams(s, 0);
#endif

    bindButtons();

    titleBar->show(true);
    titleBar->setTitle("Chiptune");
    titleBar->setRightLabel("");

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlStyle2(label[i], controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
        display.setControlValue(label[i], 1);
        display.setControlShow(label[i]);
    }
    display.setControlShow(bgLabel);
    display.refreshControl(bgLabel);
    display.setControlShow(frameControl);
    display.setControlValue(frameControl, selectedParam);
    display.refreshControl(frameControl);

    refreshTexts();
    display.synchronizeRefresh();
}

void cProTrackerChiptune::stop()
{
    moduleRefresh = 0;
#ifdef FEATURE_SYNTH
    if (renderState == kRendering)
    {
        engine.getSynthCapture()->cancel();
        renderState = kIdle;
    }
    for (int i = 0; i < 48; i++)
        if (padPlaying[i] >= 0)
            sy()->triggerTestNote(0, padPlaying[i], false);
    if (noteHeld)
        sy()->triggerTestNote(0, note, false);
    noteHeld = false;
#endif
    if (messageShown)
    {
        mtPopups.hide();
        messageShown = false;
    }
}

void cProTrackerChiptune::update()
{
    if (messageShown && messageTimer > 1200)
    {
        mtPopups.hide();
        messageShown = false;
    }

#ifdef FEATURE_SYNTH
    if (renderState != kRendering)
        return;

    SynthCapture* cap = engine.getSynthCapture();

    // the note is held for 70 % of the capture, the rest is the release
    if (noteHeld && renderTimer > (uint32_t)lengthMs * 7 / 10)
    {
        sy()->triggerTestNote(0, note, false);
        noteHeld = false;
    }

    if (cap->isDone())
        finishRender();
    else if (renderTimer > (uint32_t)lengthMs + 1500) // never got any audio blocks
    {
        cap->cancel();
        if (noteHeld)
            sy()->triggerTestNote(0, note, false);
        noteHeld = false;
        renderState = kIdle;
    }
#endif
}

void cProTrackerChiptune::refreshTexts()
{
    for (uint8_t i = 0; i < kParamCount; i++)
    {
        if (i <= kMacro6)
        {
#ifdef FEATURE_SYNTH
            const auto& macro = sy()->getPatch(0).getMetadata().macros[i];
            snprintf(s_title[i], sizeof(s_title[i]), "M%d %s", i + 1, macro.displayName.c_str());
            const int v = part().macroValue[i];
            if (macro.bipolar)
                snprintf(s_text2[i], sizeof(s_text2[i]), "%d", v - kCCMidValue);
            else
                snprintf(s_text2[i], sizeof(s_text2[i]), "%d", v);
#else
            snprintf(s_title[i], sizeof(s_title[i]), "M%d", i + 1);
            s_text2[i][0] = 0;
#endif
        }
        else if (i == kNote)
        {
            snprintf(s_title[i], sizeof(s_title[i]), "Note");
            snprintf(s_text2[i], sizeof(s_text2[i]), "%s", noteName(note));
        }
        else
        {
            snprintf(s_title[i], sizeof(s_title[i]), "Length");
            snprintf(s_text2[i], sizeof(s_text2[i]), "%u.%us", lengthMs / 1000, (lengthMs % 1000) / 100);
        }
        display.setControlText(label[i], s_title[i]);
        display.setControlText2(label[i], s_text2[i]);
        display.refreshControl(label[i]);
    }
}

void cProTrackerChiptune::select(int param)
{
    selectedParam = (uint8_t)clamp<int>(param, 0, kParamCount - 1);
    display.setControlValue(frameControl, selectedParam);
    display.refreshControl(frameControl);
    display.synchronizeRefresh();
}

void cProTrackerChiptune::change(int delta, bool big)
{
    if (renderState == kRendering)
        return;

    if (selectedParam <= kMacro6)
    {
#ifdef FEATURE_SYNTH
        const int idx = selectedParam;
        part().macroValue[idx] =
          (uint8_t)clamp<int>((int)part().macroValue[idx] + delta * (big ? 10 : 1), kCCMinValue, macroMax(idx));
        applyMacro(idx);
#endif
    }
    else if (selectedParam == kNote)
    {
        note = (uint8_t)clamp<int>((int)note + delta * (big ? 12 : 1), 24, 96);
    }
    else
    {
        lengthMs = (uint16_t)clamp<int>((int)lengthMs + delta * (big ? 1000 : 100), kMinLengthMs, kMaxLengthMs);
    }
    refreshTexts();
    display.synchronizeRefresh();
}

void cProTrackerChiptune::resetParam()
{
    if (renderState == kRendering)
        return;

    if (selectedParam <= kMacro6)
    {
#ifdef FEATURE_SYNTH
        const int idx = selectedParam;
        part().macroValue[idx] = macroBipolar(idx) ? kCCMidValue : kCCMinValue;
        applyMacro(idx);
#endif
    }
    else if (selectedParam == kNote)
        note = 48;
    else
        lengthMs = 1000;
    refreshTexts();
    display.synchronizeRefresh();
}

void cProTrackerChiptune::render()
{
#ifdef FEATURE_SYNTH
    if (renderState == kRendering)
        return;

    PT->stopPreview();
    for (int i = 0; i < 48; i++)
        if (padPlaying[i] >= 0)
        {
            sy()->triggerTestNote(0, padPlaying[i], false);
            padPlaying[i] = -1;
        }

    uint32_t frames = (uint32_t)(kRenderRate * lengthMs / 1000.0f);
    if (frames > kMaxFrames)
        frames = kMaxFrames;
    frames &= ~1u;

    engine.getSynthCapture()->start(captureBuf, frames, kRenderRate);
    renderTimer = 0;
    sy()->triggerTestNote(0, note, true);
    noteHeld = true;
    renderState = kRendering;

    snprintf(s_msg, sizeof(s_msg), "Rendering...");
    mtPopups.show(0, s_msg);
    messageShown = true;
    messageTimer = 0;
    display.drawNow();
#endif
}

void cProTrackerChiptune::finishRender()
{
#ifdef FEATURE_SYNTH
    SynthCapture* cap = engine.getSynthCapture();
    const uint32_t frames = cap->captured() & ~1u;
    const int peak = cap->peak();
    renderState = kIdle;
    if (noteHeld)
        sy()->triggerTestNote(0, note, false);
    noteHeld = false;

    if (frames < 2 || peak < 64)
    {
        snprintf(s_msg, sizeof(s_msg), "Nothing rendered");
    }
    else
    {
        // fade out the last few milliseconds so the sample ends without a click, then normalise
        const uint32_t fade = 40 < frames ? 40 : frames;
        for (uint32_t i = 0; i < fade; i++)
            captureBuf[frames - 1 - i] = (int16_t)((int32_t)captureBuf[frames - 1 - i] * (int32_t)i / (int32_t)fade);

        const int slot = PT->sample - 1;
        PT->stream->pause();
        int8_t* dst = PT->module.sampleData(slot);
        for (uint32_t i = 0; i < frames; i++)
        {
            int32_t v = (int32_t)captureBuf[i] * 127 / peak;
            dst[i] = (int8_t)clamp<int32_t>(v, -127, 127);
        }
        protracker::Sample& s = PT->module.samples[slot];
        s.length = frames;
        s.volume = 64;
        s.fineTune = 0;
        s.loopStart = 0;
        s.loopLength = 2;
        PT->module.fixSample(slot);
        char name[24];
        snprintf(name, sizeof(name), "Chip %s", noteName(note));
        protracker::setSlotName(PT->module, slot, name);
        PT->stream->resume();
        PT->dirty = true;

        snprintf(s_msg, sizeof(s_msg), "Rendered to sample %02d", PT->sample);
    }

    mtPopups.show(0, s_msg);
    messageShown = true;
    messageTimer = 0;
    display.synchronizeRefresh();
#endif
}

void cProTrackerChiptune::padNote(uint8_t pad, bool on)
{
#ifdef FEATURE_SYNTH
    if (pad >= 48 || renderState == kRendering)
        return;
    if (on)
    {
        const int midi = 36 + pad;
        padPlaying[pad] = (int8_t)midi;
        sy()->triggerTestNote(0, midi, true);
    }
    else if (padPlaying[pad] >= 0)
    {
        sy()->triggerTestNote(0, padPlaying[pad], false);
        padPlaying[pad] = -1;
    }
#endif
}

static void functSwitchModule(uint8_t button)
{
    CT->eventFunct(eventSwitchModule, CT, &button, 0);
}

static void functUp()
{
    CT->change(1, PT->isShiftPressed());
}

static void functDown()
{
    CT->change(-1, PT->isShiftPressed());
}

static void functLeft()
{
    CT->select(CT->selectedParam - 1);
}

static void functRight()
{
    CT->select(CT->selectedParam + 1);
}

static void functEncoder(int16_t value)
{
    CT->change(value, PT->isShiftPressed());
}

static void functDelete()
{
    CT->resetParam();
}

static void functSelect(uint8_t button)
{
    CT->select(button - interfaceButton0);
}

static void functPlay()
{
    CT->render();
}

static void functPads(uint8_t pad, uint8_t state, [[maybe_unused]] int16_t velo)
{
    if (state == buttonPress)
        CT->padNote(pad, true);
    else if (state == buttonRelease)
        CT->padNote(pad, false);
}

#endif // PLATFORM_IMX
