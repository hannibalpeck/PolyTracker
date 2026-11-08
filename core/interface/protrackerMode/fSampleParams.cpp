#ifdef PLATFORM_IMX

#include "protrackerMode/ptSampleParams.h"

#include <stdio.h>
#include <string.h>

#include "Platform.h"
#include "core/graphicProcessing.h"
#include "core/interfaceDefs.h"
#include "core/titleBar.h"
#include "protrackerMode/protrackerMode.h"
#include "util_math.h"

#include <cr_section_macros.h>

cProTrackerSampleParams protrackerSampleParams;
static cProTrackerSampleParams* SP = &protrackerSampleParams;
static cProTrackerMode* PT = &protrackerMode;

extern int16_t* externalRamEffectsBuff;
// the waveform is drawn from a 16 bit copy, kept in the (otherwise idle) effect buffer
#define waveCopy16 (externalRamEffectsBuff + 524288)

__NOINIT(EXTERNAL_RAM) static strTrackerSpectrum s_spectrumData;
static strSpectrumParams s_spectrumParams;
static strZoomParams s_zoom;

static char s_text2[cProTrackerSampleParams::kParamCount][16];
static char s_rightLabel[8];

static const uint16_t kFramePlaces[7][4] = {
    { (800 / 8) * 0 + 1, 424, 800 / 8 - 3, 55 }, { (800 / 8) * 1 + 1, 424, 800 / 8 - 3, 55 },
    { (800 / 8) * 2 + 1, 424, 800 / 8 - 3, 55 }, { (800 / 8) * 3 + 1, 424, 800 / 8 - 3, 55 },
    { (800 / 8) * 4 + 1, 424, 800 / 8 - 3, 55 }, { (800 / 8) * 5 + 1, 424, 800 / 8 - 3, 55 },
    { (800 / 8) * 6 + 1, 424, 800 / 8 - 3, 55 },
};

static const char* const kTitles[cProTrackerSampleParams::kParamCount] = {
    "Sample", "Volume", "Finetune", "Loop", "Loop start", "Loop end", "Length"
};

static void functSwitchModule(uint8_t button);
static void functUp();
static void functDown();
static void functLeft();
static void functRight();
static void functEncoder(int16_t value);
static void functDelete();
static void functSelect(uint8_t button);
static void functPads(uint8_t pad, uint8_t state, int16_t velo);

static protracker::Sample& smp()
{
    return PT->module.samples[PT->sample - 1];
}

static bool shiftHeld()
{
    return PT->isShiftPressed();
}

void cProTrackerSampleParams::initDisplayControls()
{
    frameData.placesCount = 7;
    for (uint8_t i = 0; i < 7; i++)
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

    // waveform
    s_spectrumData.init(600, strTrackerSpectrum::PointsLimit::k300);
    strControlProperties spectrumProperties;
    spectrumProperties.x = 0;
    spectrumProperties.y = 75 + ((300 - (MAX_16BIT / 300)) / 2);
    spectrumProperties.w = 600;
    spectrumProperties.h = MAX_16BIT / 300;
    pointsData = { 0, 0, 600, 0, 0, 0 };
    spectrumProperties.data = &pointsData;
    points = display.createControl<cPoints>(&spectrumProperties);
    spectrumProperties.data = &s_spectrumData;
    spectrum = display.createControl<cSpectrum>(&spectrumProperties);

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

void cProTrackerSampleParams::bindButtons()
{
    ip->clearAllButtons();
    ip->clearAllPots();
    ip->clearGlobalPads();

    for (uint8_t b = interfaceButton0; b <= interfaceButton6; b++)
        ip->setButtonObj(b, buttonPress, functSelect);

    ip->setButtonObj(interfaceButtonUp, buttonPress, functUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDown);
    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functRight);
    ip->setButtonObj(interfaceButtonDelete, buttonPress, functDelete);
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
    ip->setButtonObj(interfaceButtonParams, buttonPress, functSwitchModule);
}

void cProTrackerSampleParams::start(uint32_t)
{
    moduleRefresh = 1;
    PT->stream->setEnabled(true);
    PT->stopPreview();
    needRefreshWave = false;

    bindButtons();

    titleBar->show(true);
    titleBar->setTitle("Sample Parameters");

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlStyle2(label[i], controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
        display.setControlValue(label[i], 1);
        display.setControlShow(label[i]);
    }
    display.setControlText(label[7], "");
    display.setControlText2(label[7], "");
    display.setControlShow(bgLabel);
    display.refreshControl(bgLabel);
    display.setControlShow(spectrum);
    display.setControlShow(points);
    display.setControlShow(frameControl);

    refreshAll();
}

void cProTrackerSampleParams::stop()
{
    moduleRefresh = 0;
    PT->stopPreview();
}

void cProTrackerSampleParams::update()
{
    if (needRefreshWave)
    {
        needRefreshWave = false;
        refreshWave();
    }
}

void cProTrackerSampleParams::refreshAll()
{
    refreshTexts();
    refreshWave();
    display.setControlValue(frameControl, selectedParam - kSample);
    display.refreshControl(frameControl);
    display.synchronizeRefresh();
}

void cProTrackerSampleParams::refreshWave()
{
    protracker::Sample& s = smp();
    const int8_t* data = PT->module.sampleData(PT->sample - 1);
    const uint32_t length = s.length;

    for (uint32_t i = 0; i < length; i++)
        waveCopy16[i] = (int16_t)(data[i] * 256);

    if (length > 0)
    {
        s_spectrumParams.address = waveCopy16;
        s_spectrumParams.length = length;
        GP.spectrumResetZoom(0, length, &s_zoom);
        GP.processSpectrum(&s_spectrumParams, &s_zoom, &s_spectrumData);
    }
    else
    {
        for (uint16_t i = 0; i < 600; i++)
        {
            s_spectrumData.upperData[i] = 0;
            s_spectrumData.lowerData[i] = 0;
        }
        s_spectrumData.spectrumType = 1;
    }
    display.refreshControl(spectrum);
    refreshPoints();
}

void cProTrackerSampleParams::refreshPoints()
{
    protracker::Sample& s = smp();
    const bool looping = s.length > 0 && s.loopLength > 2;

    if (looping)
    {
        pointsData.startPoint = (int16_t)(((uint64_t)s.loopStart * 599) / s.length);
        pointsData.endPoint = (int16_t)(((uint64_t)(s.loopStart + s.loopLength) * 599) / s.length);
    }
    else
    {
        pointsData.startPoint = -1;
        pointsData.endPoint = -1;
    }
    pointsData.selected = selectedParam == kLoopStart ? selectStart
                          : selectedParam == kLoopEnd ? selectEnd
                                                      : 0;
    display.refreshControl(points);
}

void cProTrackerSampleParams::refreshTexts()
{
    protracker::Sample& s = smp();
    const int fine = (s.fineTune & 8) ? (int)(s.fineTune & 15) - 16 : (int)(s.fineTune & 15);
    const bool looping = s.length > 0 && s.loopLength > 2;

    snprintf(s_text2[kSample], sizeof(s_text2[kSample]), "%02d", PT->sample);
    snprintf(s_text2[kVolume], sizeof(s_text2[kVolume]), "%d", s.volume);
    snprintf(s_text2[kFineTune], sizeof(s_text2[kFineTune]), "%d", fine);
    snprintf(s_text2[kLoop], sizeof(s_text2[kLoop]), "%s", looping ? "Forward" : "1-Shot");
    snprintf(s_text2[kLoopStart], sizeof(s_text2[kLoopStart]), "%u", (unsigned)s.loopStart);
    snprintf(s_text2[kLoopEnd],
             sizeof(s_text2[kLoopEnd]),
             "%u",
             looping ? (unsigned)(s.loopStart + s.loopLength) : (unsigned)s.length);
    snprintf(s_text2[kLength], sizeof(s_text2[kLength]), "%u", (unsigned)s.length);

    for (uint8_t i = 0; i < kParamCount; i++)
    {
        display.setControlText(label[i], kTitles[i]);
        display.setControlText2(label[i], s_text2[i]);
        display.setControlColors(label[i],
                                 (s.length == 0 && i >= kLoop) ? display.getTheme().inactiveLabelsColors
                                                               : display.getTheme().activeLabelsColors);
        display.refreshControl(label[i]);
    }

    snprintf(s_rightLabel, sizeof(s_rightLabel), "%02d", PT->sample);
    titleBar->setRightLabel(s_rightLabel);
}

void cProTrackerSampleParams::select(int param)
{
    if (param < 0)
        param = 0;
    if (param >= kParamCount)
        param = kParamCount - 1;
    selectedParam = (uint8_t)param;
    display.setControlValue(frameControl, selectedParam);
    display.refreshControl(frameControl);
    refreshPoints();
}

// Changes the selected parameter. Positions and the length are in bytes, always even, and move one
// word per step (a coarse step with Shift held).
void cProTrackerSampleParams::change(int delta, bool big)
{
    protracker::Sample& s = smp();
    const int step = big ? 256 : 2;
    bool wave = false;

    switch (selectedParam)
    {
        case kSample:
        {
            const int n = clamp<int>(PT->sample + delta, 1, protracker::kSampleCount);
            if (n == PT->sample)
                return;
            PT->sample = n;
            wave = true;
            break;
        }
        case kVolume: s.volume = (uint8_t)clamp<int>(s.volume + delta * (big ? 8 : 1), 0, 64); break;
        case kFineTune:
        {
            int fine = (s.fineTune & 8) ? (int)(s.fineTune & 15) - 16 : (int)(s.fineTune & 15);
            fine = clamp<int>(fine + delta, -8, 7);
            s.fineTune = (uint8_t)(fine & 15);
            break;
        }
        case kLoop:
        {
            if (s.length < 4)
                return;
            if (delta > 0 && s.loopLength <= 2)
            {
                s.loopStart = 0;
                s.loopLength = s.length;
            }
            else if (delta < 0)
            {
                s.loopStart = 0;
                s.loopLength = 2;
            }
            wave = true;
            break;
        }
        case kLoopStart:
        {
            if (s.length < 4 || s.loopLength <= 2)
                return;
            const int end = (int)(s.loopStart + s.loopLength);
            const int start = clamp<int>((int)s.loopStart + delta * step, 0, end - 4);
            s.loopStart = (uint32_t)start & ~1u;
            s.loopLength = (uint32_t)(end - (int)s.loopStart);
            wave = true;
            break;
        }
        case kLoopEnd:
        {
            if (s.length < 4 || s.loopLength <= 2)
                return;
            const int end = clamp<int>((int)(s.loopStart + s.loopLength) + delta * step,
                                       (int)s.loopStart + 4,
                                       (int)s.length);
            s.loopLength = (uint32_t)(end - (int)s.loopStart) & ~1u;
            wave = true;
            break;
        }
        case kLength:
        {
            if (s.length == 0)
                return;
            const int len = clamp<int>((int)s.length + delta * step * (big ? 1 : 1), 2,
                                       (int)protracker::kMaxSampleBytes);
            s.length = (uint32_t)len & ~1u;
            if (s.loopLength > 2 && s.loopStart + s.loopLength > s.length)
            {
                if (s.loopStart + 4 > s.length)
                {
                    s.loopStart = 0;
                    s.loopLength = 2;
                }
                else
                    s.loopLength = s.length - s.loopStart;
            }
            wave = true;
            break;
        }
        default: return;
    }

    PT->module.fixSample(PT->sample - 1);
    PT->dirty = true;
    refreshTexts();
    if (wave)
        needRefreshWave = true;
    else
        display.synchronizeRefresh();
}

void cProTrackerSampleParams::resetParam()
{
    protracker::Sample& s = smp();
    switch (selectedParam)
    {
        case kVolume: s.volume = 64; break;
        case kFineTune: s.fineTune = 0; break;
        case kLoop:
        case kLoopStart:
        case kLoopEnd:
            s.loopStart = 0;
            s.loopLength = 2;
            break;
        default: return;
    }
    PT->module.fixSample(PT->sample - 1);
    PT->dirty = true;
    refreshTexts();
    needRefreshWave = true;
}

// The pads audition the sample, with its loop, at their pitch (same note map as the pattern screen).
void cProTrackerSampleParams::playNote(int note, bool on)
{
    if (on)
        PT->previewNote(note);
    else
        PT->stopPreview();
}

static void functSwitchModule(uint8_t button)
{
    if (button == interfaceButtonSamplePlay)
        return;
    SP->eventFunct(eventSwitchModule, SP, &button, 0);
}

static void functUp()
{
    SP->change(1, shiftHeld());
}

static void functDown()
{
    SP->change(-1, shiftHeld());
}

static void functLeft()
{
    SP->select(SP->selectedParam - 1);
}

static void functRight()
{
    SP->select(SP->selectedParam + 1);
}

static void functEncoder(int16_t value)
{
    SP->change(value, shiftHeld());
}

static void functDelete()
{
    SP->resetParam();
}

static void functSelect(uint8_t button)
{
    SP->select(button - interfaceButton0);
}

static void functPads(uint8_t pad, uint8_t state, [[maybe_unused]] int16_t velo)
{
    if (pad >= 48)
        return;
    int note = 12 + pad; // pad 13 is C-3, the upper pads fold down
    if (note >= 36)
        note -= 36;

    if (state == buttonPress)
        SP->playNote(note, true);
    else if (state == buttonRelease)
        SP->playNote(note, false);
}

#endif // PLATFORM_IMX
