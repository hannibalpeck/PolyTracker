#include "mtAudioEngine.h"
#include "mtConfig.h"
#include "mtPadBoard.h"
#include "mtPadsBacklight.h"
#include "mtSequencer.h"

#include "masterParams/masterParams.h"

#include "Platform.h"
#include "ampLogarythmicValues.h"
#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#include "fileManager.h"
#include "math.h"
#include "util_math.h"

cMasterParams masterParams;
static cMasterParams* MP = &masterParams;

extern strMtProject mtProject;

static void functPlayAction();
static void functUp();
static void functDown();
static void functDelete();
static void functEncoder(int16_t value);
static void functSwitchModule(uint8_t button);
static void functSwitchModeMaster(uint8_t state);
static void functPads(uint8_t pad, uint8_t state, int16_t velo);
static void functMenu(uint8_t state);

void changeVolume(int16_t value);
void setDefaultMasterVolume();
void changeMixerOnMidiCC(uint8_t cc_idx, uint8_t cc_value);

// options: low byte = button of the module we came from
void cMasterParams::start(uint32_t options)
{
    moduleRefresh = 1;

    sourceModuleButton = options & 0xff;

    ip->setButtonObj(interfaceButtonParams, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonPerformance, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonFile, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonConfig, buttonPress, functSwitchModule);

    ip->setButtonObj(interfaceButtonMaster, functSwitchModeMaster);

    ip->setButtonObj(interfaceButtonSamplePlay, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleEdit, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleRec, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleLoad, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSong, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonPattern, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonGame, buttonPress, functSwitchModule);

    ip->setButtonObj(interfaceButtonMiniMenu, functMenu);

    ip->setPadsGlobal(functPads);

    setMasterScreenFunct();
    switchToMaster();

    showMasterVolumeOnHold = true;

    lightUpPadBoard();
}

void cMasterParams::stop()
{
    sequencer.stopManualNotes();
    moduleRefresh = 0;
}

void cMasterParams::setMasterScreenFunct()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearAllPots();

    ip->setButtonObj(interfaceButtonPlay, buttonPress, functPlayAction);

    ip->setButtonObj(interfaceButtonUp, buttonPress, functUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDown);
    ip->setButtonObj(interfaceButtonDelete, buttonPress, functDelete);

    ip->setPotObj(interfacePot0, functEncoder);
}

void cMasterParams::switchToMaster()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearButton(interfaceButtonShift);

    keyboardManager.deactivateKeyboard();

    showMasterScreen();
}

//==============================================================================================================

static void functEncoder(int16_t value)
{
    changeVolume(value);
}

static void functUp()
{
    changeVolume(1);
}

static void functDown()
{
    changeVolume(-1);
}

static void functDelete()
{
    setDefaultMasterVolume();
}

static void functPlayAction()
{
    if (MP->sourceModuleButton == interfaceButtonSampleRec)
    {
        return;
    }

    if (sequencer.getSeqState() == Sequencer::SEQ_STATE_STOP)
    {
        if (MP->buttons->isButtonPressed(interfaceButtonShift))
        {
            sequencer.playSong();
        }
        else
        {
            sequencer.playPattern();
        }
    }
    else
    {
        sequencer.stop();
    }
}

static void functSwitchModule(uint8_t button)
{
    if (MP->sourceModuleButton == interfaceButtonSampleRec)
    {
        MP->eventFunct(eventSwitchModule, MP, &MP->sourceModuleButton, &button);
    }
    else
    {
        MP->eventFunct(eventSwitchModule, MP, &button, 0);
    }
}

static void functSwitchModeMaster(uint8_t state)
{
    if (state == buttonPress)
    {
        MP->sourceModuleButton = interfaceButtonMaster;
        MP->showMasterVolumeOnHold = true;
    }
    else if (state == buttonHold)
    {
        if (MP->showMasterVolumeOnHold)
        {
            MP->showMasterVolumeOnHold = false;
            MP->switchToMaster();
        }
    }
    else if (state == buttonRelease)
    {
        if (MP->sourceModuleButton != interfaceButtonMaster && !MP->showMasterVolumeOnHold)
        {
            if (MP->sourceModuleButton == interfaceButtonSampleRec)
            {
                MP->eventFunct(eventSwitchToPreviousModule, MP, &MP->sourceModuleButton, 0);
            }
            else
            {
                MP->eventFunct(eventSwitchToPreviousModule, MP, 0, 0);
            }
        }
        else if (!MP->showMasterVolumeOnHold)
        {
            MP->switchToMaster();
        }
    }
}

static void functMenu(uint8_t state)
{
    if (state == buttonPress)
    {
        mtMenuPopup.showModulePopup(MP);
    }
    else if (state == buttonRelease)
    {
        mtMenuPopup.hideModulePopup(MP);
    }
}

//======================================================================================================================
// master volume
void changeVolume(int16_t value)
{
    if (mtConfig.audioCodecConfig.volume + value < MASTER_VOLUME_MIN)
        mtConfig.audioCodecConfig.volume = MASTER_VOLUME_MIN;
    else if (mtConfig.audioCodecConfig.volume + value > MASTER_VOLUME_MAX)
        mtConfig.audioCodecConfig.volume = MASTER_VOLUME_MAX;
    else
        mtConfig.audioCodecConfig.volume += value;

    engine.setHeadphonesVolume(mtConfig.audioCodecConfig.volume);
    saveConfig();

    if (MP->moduleRefresh)
    {
        MP->showVolume();
    }
}

void setDefaultMasterVolume()
{
    // protection against suddenly blowing the speakers
    int16_t volumeDifference = MASTER_VOLUME_DEFAULT - mtConfig.audioCodecConfig.volume;
    if (volumeDifference >= 10)
    {
        mtConfig.audioCodecConfig.volume += 10;
    }
    else
    {
        mtConfig.audioCodecConfig.volume = MASTER_VOLUME_DEFAULT;
    }

    engine.setHeadphonesVolume(mtConfig.audioCodecConfig.volume);

    saveConfig();

    MP->showVolume();
}

void cMasterParams::lightUpPadBoard()
{
    padsBacklight.clearGlobalPads(0, 1, 1);

    bool isBeatSlice =
      mtProject.instrument[mtProject.values.lastUsedInstrument].playMode == playModeBeatSlice;

    if (isBeatSlice)
    {
        for (uint8_t pad = 0;
             pad < mtProject.instrument[mtProject.values.lastUsedInstrument].sliceNumber;
             pad++)
        {
            padsBacklight.setBackLayer(1, mtConfig.values.padsLightBack, pad);
        }
    }
}

static void functPads(uint8_t pad, uint8_t state, int16_t velo)
{
    (void)velo;

    bool isBeatSlice =
      mtProject.instrument[mtProject.values.lastUsedInstrument].playMode == playModeBeatSlice;

    if (state == buttonPress)
    {
        padsBacklight.setFrontLayer(1, mtConfig.values.padsLightFront, pad);
        uint8_t noteFromPad = mtPadBoard.getNoteFromPad(pad, isBeatSlice);

        sequencer.handleNoteOn(Sequencer::GRID_OUTSIDE_PATTERN, noteFromPad, -1, pad);
    }
    else if (state == buttonRelease)
    {
        padsBacklight.setFrontLayer(0, 0, pad);
        uint8_t noteFromPad = mtPadBoard.getNoteFromPad(pad, isBeatSlice);

        sequencer.handleNoteOff(Sequencer::GRID_OUTSIDE_PATTERN, noteFromPad, 0, pad);
    }
}

// MIDI CC 71..82 still set the project's track / delay / reverb / dry / line-in volumes
uint8_t* const indexedMixerValues[12] = {
    &mtProject.values.trackVolume[0], &mtProject.values.trackVolume[1],
    &mtProject.values.trackVolume[2], &mtProject.values.trackVolume[3],
    &mtProject.values.trackVolume[4], &mtProject.values.trackVolume[5],
    &mtProject.values.trackVolume[6], &mtProject.values.trackVolume[7],
    &mtProject.values.delayVolume,    &mtProject.values.reverbVolume,
    &mtProject.values.dryMixVolume,   &mtProject.values.lineInVolume
};

void changeMixerOnMidiCC(uint8_t cc_idx, uint8_t cc_value)
{
    constexpr uint8_t MIXER_CC_RANGE_MIN = 71;
    constexpr uint8_t MIXER_CC_RANGE_MAX = 82;

    if (cc_idx > MIXER_CC_RANGE_MAX)
        return;
    if (cc_idx < MIXER_CC_RANGE_MIN)
        return;

    uint8_t ccToIdx = cc_idx - MIXER_CC_RANGE_MIN;

    *(indexedMixerValues[ccToIdx]) = ((uint16_t)(cc_value * 100)) / 127;

    if ((ccToIdx < 8) || (ccToIdx == 10))
        engine.refreshTrackVolume();
    else if (ccToIdx == 8)
        engine.refreshDelayVolume();
    else if (ccToIdx == 9)
        engine.refreshReverbVolume();
    else if (ccToIdx == 11)
        engine.refreshLineInStreamVolume();
}
