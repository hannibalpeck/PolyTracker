#include "mtAudioEngine.h"
#include "mtConfig.h"
#include "mtPadBoard.h"
#include "mtPadsBacklight.h"
#include "mtSequencer.h"


#include "masterParams/masterParams.h"
#include "mtGainLevelLogarithmicTab.h"

#include "Platform.h"
#include "ampLogarythmicValues.h"
#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#include "fileManager.h"
#include "math.h"
#include "mtMasterEnhancer.h"
#include "util_math.h"
#ifdef FEATURE_SYNTH
#include "Synth.h"
#endif

cMasterParams masterParams;
static cMasterParams* MP = &masterParams;

extern strMtProject mtProject;

static void functPlayAction();
static void functRecAction();

static void functLeft();
static void functRight();
static void functUp();
static void functDown();
static void functDelete();

// master
static void functSelectVolume(uint8_t state);

// delay
// reverb
// limiter
// equalizer

static void functEncoder(int16_t value);
static void functSwitchModule(uint8_t button);
static void functSwitchModeMaster(uint8_t state);

// mixer
static void functSoloMute(uint8_t state);
static void functSoloMuteTrack(uint8_t n, uint8_t state);
// mixer delay reverb
static void functSoloMuteDelay(uint8_t state);
static void functSoloMuteReverb(uint8_t state);
static void functSoloMuteDryMix(uint8_t state);
static void functSoloMuteLineIn(uint8_t state);
static void functSoloMuteDelayReverb(uint8_t state);
#ifdef FEATURE_SYNTH
static void functSelectSynth1(uint8_t state);
static void functSelectSynth2(uint8_t state);
static void functSelectSynth3(uint8_t state);
#endif

// MASTER EDIT FUNCTIONS
void changeVolume(int16_t value);

void setDefaultMasterVolume();

// limiter screen


static void functMiniMiddle(uint8_t state);

static void functPads(uint8_t pad, uint8_t state, int16_t velo);

static void functMenu(uint8_t state);

// zmiana nazwy trackow
static void functEditModeToggle();

static void functCancelEditTrackName();
static void functSaveTrackName();

static void functEnterKeyExport();
static void functLeftKeyboard();
static void functRightKeyboard();
static void functUpKeyboard();
static void functDownKeyboard();
static void functDeleteKeyboard();
static void functInsertKeyboard();

void changeMixerOnMidiCC(uint8_t cc_idx, uint8_t cc_value);

//---------------------------------------

static elapsedMillis refreshTimer;

void cMasterParams::update()
{

    if (displayType == displayMode::mixer && !keyboardManager.getState())
    {
        for (uint8_t i = 0; i < 8; i++)
        {
            calcTrackLevel(i);
        }
    }
    else if (displayType == displayMode::mixerDelayReverb && !keyboardManager.getState())
    {
        calcDelayLevel();
        calcReverbLevel();
        calcLineInLevel();
        calcDryMixLevel();
#ifdef FEATURE_SYNTH
        calcSynthLevels();
#endif
    }

    // gdyby byla taka potrzeba mozna sprobowac tu odswiezac rowniez levele, a obliczac w kazdej
    // petli
    if (refreshTimer < 50)
        return;
    refreshTimer = 0;

    if (displayType == displayMode::mixer && !keyboardManager.getState())
    {
        for (uint8_t i = 0; i < 8; i++)
        {
            if (trackLevel[i].value != trackLevel[i].lastValue)
            {
                showLevelBar(i, &trackLevel[i]);
            }
            trackLevel[i].lastValue = trackLevel[i].value;
        }
        display.synchronizeRefresh();
    }
    else if (displayType == displayMode::mixerDelayReverb && !keyboardManager.getState() &&
             !mtMenuPopup.isPopupVisible())
    {
        for (uint8_t i = 0; i < 2; i++)
        {
            if (delayLevel[i].value != delayLevel[i].lastValue)
            {
                showLevelBar(delayVolumeScreenPosition + i * 4, &delayLevel[i]);
            }
            delayLevel[i].lastValue = delayLevel[i].value;
        }

        for (uint8_t i = 0; i < 2; i++)
        {
            if (reverbLevel[i].value != reverbLevel[i].lastValue)
            {
                showLevelBar(reverbVolumeScreenPosition + i * 4, &reverbLevel[i]);
            }
            reverbLevel[i].lastValue = reverbLevel[i].value;
        }

        for (uint8_t i = 0; i < 2; i++)
        {
            if (lineInLevel[i].value != lineInLevel[i].lastValue)
            {
                showLevelBar(lineInVolumeScreenPosition + i * 4, &lineInLevel[i]);
            }
            lineInLevel[i].lastValue = lineInLevel[i].value;
        }

        for (uint8_t i = 0; i < 2; i++)
        {
            if (dryMixLevel[i].value != dryMixLevel[i].lastValue)
            {
                showLevelBar(dryMixVolumeScreenPosition + i * 4, &dryMixLevel[i]);
            }
            dryMixLevel[i].lastValue = dryMixLevel[i].value;
        }

#ifdef FEATURE_SYNTH
        static constexpr int kSynthOffset = 8;
        for (int synthIdx = 0; synthIdx < kSynthParts; synthIdx++)
        {

            if (synthLevel[synthIdx].value != synthLevel[synthIdx].lastValue)
            {
                showLevelBar(kSynthOffset + synthIdx, &synthLevel[synthIdx]);
                synthLevel[synthIdx].lastValue = synthLevel[synthIdx].value;
            }
        }
#endif

        display.synchronizeRefresh();
    }

    if (displayType == displayMode::mixer && !keyboardManager.getState())
    {
        refreshAllTracksMixerVolume();
    }
    else if (displayType == displayMode::mixerDelayReverb && !keyboardManager.getState())
    {
        refreshOtherMixerVolume();
    }

}

// parametr options - przycisk modulu z ktorego nastapilo wejscie do mastera
void cMasterParams::start(uint32_t options)
{
    moduleRefresh = 1;

    //	displayType = display_t::masterValues;

    sourceModuleButton = options & 0xff;

    // drugi bajt to numer podekranu
    uint8_t moduleMode = (options >> 8) & 0xff;
    if (moduleMode == 1)
        displayType = displayMode::masterValues;
    else if (moduleMode == 2)
        displayType = displayMode::mixer;
    else if (moduleMode == 3)
        displayType = displayMode::mixerDelayReverb;

    //--------------------------------------------------------------------
    // ustawienie funkcji
    ip->setButtonObj(interfaceButtonParams, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonPerformance, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonFile, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonConfig, buttonPress, functSwitchModule);
    // ip->setButtonObj(interfaceButtonMaster, buttonPress, functSwitchModule);

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

    isSolo = false;

    showMasterScreen();
    setMasterScreenFunct();
    activateLabelsBorder();

    if (displayType == displayMode::mixer)
    {
        switchToMixer();
    }
    else if (displayType == displayMode::masterValues)
    {
        switchToMaster();
    }
    else if (displayType == cMasterParams::displayMode::mixerDelayReverb)
    {
        switchToMixerDelayReverb();
    }

    displayTypeBeforeMasterPress = displayType;
    selectedPlaceBeforeMasterPress = selectedPlace;
    showMasterVolumeOnHold = true;
    resetMixerLevels();
    resetMixerDelayReverbLevels();

    lightUpPadBoard();
}

void cMasterParams::stop()
{
    sequencer.stopManualNotes();
    moduleRefresh = 0;
}

void cMasterParams::hideControlsUnderMiniMenu()
{
    hideControlUnderMenu();
}

uint8_t cMasterParams::getCurrentModeIdx()
{
    return displayType == displayMode::masterValues ? 0
                                                    : (displayType == displayMode::mixer ? 1 : 2);
}

void cMasterParams::setMasterScreenFunct()
{
    // funkcje
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearAllPots();

    ip->setButtonObj(interfaceButtonPlay, buttonPress, functPlayAction);
    ip->setButtonObj(interfaceButtonRec, buttonPress, functRecAction);

    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functRight);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDown);
    ip->setButtonObj(interfaceButtonDelete, buttonPress, functDelete);

    ip->setButtonObj(interfaceButton0, functSelectVolume);
    ip->setButtonObj(interfaceButtonMiniMiddle, functMiniMiddle);

    ip->setPotObj(interfacePot0, functEncoder);
}

void cMasterParams::setKeyboardExportFunctions()
{
    // funkcje
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearButtonsRange(interfaceButtonPlay, interfaceButtonRight);
    // ip->clearButton(interfaceButtonMaster);

    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeftKeyboard);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functRightKeyboard);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functUpKeyboard);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDownKeyboard);

    ip->setButtonObj(interfaceButton0, buttonPress, functEnterKeyExport);
    // ip->setButtonObj(interfaceButton4,buttonPress, functAutoNameTrackName);

    ip->setButtonObj(interfaceButton6, buttonPress, functCancelEditTrackName);
    ip->setButtonObj(interfaceButton7, buttonPress, functSaveTrackName);

    ip->setButtonObj(interfaceButtonDelete, buttonPress, functDeleteKeyboard);
    ip->setButtonObj(interfaceButtonInsert, buttonPress, functInsertKeyboard);
}

//==============================================================================================================
// tracker mini middle button
static void functMiniMiddle(uint8_t state)
{
    if (state != buttonPress)
    {
        return;
    }

    if (MP->keyboardManager.getState())
    {
        MP->keyboardManager.confirmKey();
    }
}

// ##############################################################################################
// ###############################        ACTION BUTTONS        #################################
// ##############################################################################################
//==============================================================================================================

static void functEncoder(int16_t value)
{
#ifdef TRACKER_MINI
    if (MP->keyboardManager.getState())
    {
        MP->keyboardManager.confirmKey();
        return;
    }
#endif

    if (MP->displayType == cMasterParams::displayMode::mixer)
    {
        for (uint8_t track = 0; track < 8; track++)
        {
            if (MP->trackIsEdited[track])
            {
                MP->changeTrackVolume(value, track);
                MP->ignoreMuteRelease[track] = true;
            }
        }
        return;
    }

    if (MP->displayType == cMasterParams::displayMode::mixerDelayReverb)
    {
        if (MP->delayIsEdited)
        {
            MP->changeDelayVolume(value);
            MP->ignoreMuteReleaseDelay = true;
        }
        if (MP->reverbIsEdited)
        {
            MP->changeReverbVolume(value);
            MP->ignoreMuteReleaseReverb = true;
        }
        if (MP->dryMixIsEdited)
        {
            MP->changeDryMixVolume(value);
            MP->ignoreMuteReleaseDryMix = true;
        }
        if (MP->lineInIsEdited)
        {
            MP->changeLineInVolume(value);
            MP->ignoreMuteReleaseLineIn = true;
        }

#ifdef FEATURE_SYNTH
        for (int synthIdx = 0; synthIdx < kSynthParts; synthIdx++)
        {
            if (MP->synthGainIsEdited[synthIdx])
            {
                MP->changeSynthGain(synthIdx, value);
            }
        }
#endif
        return;
    }

    changeVolume(value);
}

static void functLeft()
{
    if (MP->displayType == cMasterParams::displayMode::mixer)
    {
        return;
    }
    if (MP->displayType == cMasterParams::displayMode::mixerDelayReverb)
    {
        return;
    }

    MP->activateLabelsBorder();
}

static void functRight()
{
    if (MP->displayType == cMasterParams::displayMode::mixer)
    {
        return;
    }
    if (MP->displayType == cMasterParams::displayMode::mixerDelayReverb)
    {
        return;
    }

    MP->activateLabelsBorder();
}

static void functUp()
{
    if (MP->displayType == cMasterParams::displayMode::mixer)
    {
        for (uint8_t track = 0; track < 8; track++)
        {
            if (MP->trackIsEdited[track])
            {
                MP->changeTrackVolume(1, track);
                MP->ignoreMuteRelease[track] = true;
            }
        }

        return;
    }

    if (MP->displayType == cMasterParams::displayMode::mixerDelayReverb)
    {
        if (MP->delayIsEdited)
        {
            MP->changeDelayVolume(1);
            MP->ignoreMuteReleaseDelay = true;
        }
        if (MP->reverbIsEdited)
        {
            MP->changeReverbVolume(1);
            MP->ignoreMuteReleaseReverb = true;
        }
        if (MP->dryMixIsEdited)
        {
            MP->changeDryMixVolume(1);
            MP->ignoreMuteReleaseDryMix = true;
        }
        if (MP->lineInIsEdited)
        {
            MP->changeLineInVolume(1);
            MP->ignoreMuteReleaseLineIn = true;
        }
#ifdef FEATURE_SYNTH
        for (int synthIdx = 0; synthIdx < kSynthParts; synthIdx++)
        {
            if (MP->synthGainIsEdited[synthIdx])
            {
                MP->changeSynthGain(synthIdx, 1);
            }
        }
#endif

        return;
    }

    changeVolume(1);
}

static void functDown()
{
    if (MP->displayType == cMasterParams::displayMode::mixer)
    {
        for (uint8_t track = 0; track < 8; track++)
        {
            if (MP->trackIsEdited[track])
            {
                MP->changeTrackVolume(-1, track);
                MP->ignoreMuteRelease[track] = true;
            }
        }

        return;
    }

    if (MP->displayType == cMasterParams::displayMode::mixerDelayReverb)
    {
        if (MP->delayIsEdited)
        {
            MP->changeDelayVolume(-1);
            MP->ignoreMuteReleaseDelay = true;
        }
        if (MP->reverbIsEdited)
        {
            MP->changeReverbVolume(-1);
            MP->ignoreMuteReleaseReverb = true;
        }
        if (MP->dryMixIsEdited)
        {
            MP->changeDryMixVolume(-1);
            MP->ignoreMuteReleaseDryMix = true;
        }
        if (MP->lineInIsEdited)
        {
            MP->changeLineInVolume(-1);
            MP->ignoreMuteReleaseLineIn = true;
        }
#ifdef FEATURE_SYNTH
        for (int synthIdx = 0; synthIdx < kSynthParts; synthIdx++)
        {
            if (MP->synthGainIsEdited[synthIdx])
            {
                MP->changeSynthGain(synthIdx, -1);
            }
        }
#endif
        return;
    }

    changeVolume(-1);
}

static void functDelete()
{
    if (MP->displayType == cMasterParams::displayMode::mixer)
    {
        for (uint8_t track = 0; track < 8; track++)
        {
            if (MP->trackIsEdited[track])
            {
                MP->setDefaultTrackVolume(track);
                MP->ignoreMuteRelease[track] = true;
            }
        }
        return;
    }

    if (MP->displayType == cMasterParams::displayMode::mixerDelayReverb)
    {
        if (MP->delayIsEdited)
        {
            MP->setDefaultDelayVolume();
            MP->ignoreMuteReleaseDelay = true;
        }
        if (MP->reverbIsEdited)
        {
            MP->setDefaultReverbVolume();
            MP->ignoreMuteReleaseReverb = true;
        }
        if (MP->dryMixIsEdited)
        {
            MP->setDefaultDryMixVolume();
            ;
            MP->ignoreMuteReleaseDryMix = true;
        }
        if (MP->lineInIsEdited)
        {
            MP->setDefaultLineInVolume();
            MP->ignoreMuteReleaseLineIn = true;
        }

#ifdef FEATURE_SYNTH
        for (int synthIdx = 0; synthIdx < kSynthParts; synthIdx++)
        {
            if (MP->synthGainIsEdited[synthIdx])
            {
                MP->setDefaultSynthGain(synthIdx);
            }
        }
#endif
        return;
    }

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

static void functRecAction()
{
}

static void functSelectVolume(uint8_t state)
{
    if (state > buttonPress)
    {
        return;
    }

    uint8_t node = 0;

    if (state == buttonPress)
    {
        MP->selectedPlace = node;
    }
    MP->activateLabelsBorder();
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

void cMasterParams::switchScreen()
{
    selectedPlace = 0;

    if (displayType == displayMode::masterValues)
    {
        switchToMixer();
    }
    else if (displayType == displayMode::mixerDelayReverb)
    {
        switchToMaster();
    }
    else if (displayType == cMasterParams::displayMode::mixer)
    {
        switchToMixerDelayReverb();
    }
}

void cMasterParams::returnToScreen()
{
    selectedPlace = selectedPlaceBeforeMasterPress;

    if (displayTypeBeforeMasterPress == displayMode::mixer)
    {
        switchToMixer();
    }
    else if (displayTypeBeforeMasterPress == displayMode::masterValues)
    {
        switchToMaster();
    }
    else if (displayTypeBeforeMasterPress == cMasterParams::displayMode::mixerDelayReverb)
    {
        switchToMixerDelayReverb();
    }
}
void cMasterParams::switchToMaster()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearButton(interfaceButtonShift);

    ip->setButtonObj(interfaceButton0, functSelectVolume);

    keyboardManager.deactivateKeyboard();

    displayType = displayMode::masterValues;

    showMasterScreen();
}
void cMasterParams::switchToMixer()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);

    ip->setButtonObj(interfaceButton0, functSoloMuteTrack);

    ip->setButtonObj(interfaceButton1, functSoloMuteTrack);
    ip->setButtonObj(interfaceButton2, functSoloMuteTrack);

    ip->setButtonObj(interfaceButton3, functSoloMuteTrack);
    ip->setButtonObj(interfaceButton4, functSoloMuteTrack);
    ip->setButtonObj(interfaceButton5, functSoloMuteTrack);
    ip->setButtonObj(interfaceButton6, functSoloMuteTrack);
    ip->setButtonObj(interfaceButton6, functSoloMuteTrack);
    ip->setButtonObj(interfaceButton7, functSoloMuteTrack);

    ip->setButtonObj(interfaceButtonShift, functSoloMute);

    ip->setButtonObj(interfaceButtonPlay, buttonPress, functPlayAction);
    ip->setButtonObj(interfaceButtonRec, buttonPress, functEditModeToggle);

    editTrackNameMode = 0;
    displayType = displayMode::mixer;

    keyboardManager.deactivateKeyboard();

    resetMixerLevels();
    showMixerScreen();
}

void cMasterParams::switchToMixerDelayReverb()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearButton(interfaceButtonRec);

    ip->setButtonObj(interfaceButton0, functSoloMuteDelay);
    ip->setButtonObj(interfaceButton1, functSoloMuteReverb);
    ip->setButtonObj(interfaceButton2, functSoloMuteDryMix);
    ip->setButtonObj(interfaceButton3, functSoloMuteLineIn);
#ifdef FEATURE_SYNTH
    ip->setButtonObj(interfaceButton4, functSelectSynth1);
    ip->setButtonObj(interfaceButton5, functSelectSynth2);
    ip->setButtonObj(interfaceButton6, functSelectSynth3);
#endif
    ip->setButtonObj(interfaceButtonShift, functSoloMuteDelayReverb);

    ip->setButtonObj(interfaceButtonPlay, buttonPress, functPlayAction);

    keyboardManager.deactivateKeyboard();

    editTrackNameMode = 0;
    displayType = displayMode::mixerDelayReverb;

    resetMixerDelayReverbLevels();
    showMixerDelayReverbScreen();
}

// delay
// reverb
// limiter screen
// equalizer
void cMasterParams::calcTrackLevel(uint8_t n)
{
    float localTrackLevel = instrumentPlayer[n].getRMSValue();
    if (localTrackLevel == -1.0f)
    {
        return;
    }

    calcLevel(&trackLevel[n], localTrackLevel);
}

void cMasterParams::calcDelayLevel()
{
    for (uint8_t i = 0; i < 2; i++)
    {
        localDelayLevel[i] = engine.getDelayRMS(i);
        if (localDelayLevel[i] != -1.0f)
        {
            calcLevel(&delayLevel[i], localDelayLevel[i]);
        }
    }
}

void cMasterParams::calcReverbLevel()
{
    for (uint8_t i = 0; i < 2; i++)
    {
        localReverbLevel[i] = engine.getReverbRMS(i);
        if (localReverbLevel[i] != -1.0f)
        {
            calcLevel(&reverbLevel[i], localReverbLevel[i]);
        }
    }
}

void cMasterParams::calcLineInLevel()
{
    for (uint8_t i = 0; i < 2; i++)
    {
        localLineInLevel[i] = engine.getLineInRMS(i);
        if (localLineInLevel[i] != -1.0f)
        {
            calcLevel(&lineInLevel[i], localLineInLevel[i]);
        }
    }
}

#ifdef FEATURE_SYNTH
void cMasterParams::calcSynthLevels()
{
    for (int synthIdx = 0; synthIdx < kSynthParts; synthIdx++)
    {
        localSynthVoiceLevel[synthIdx] =
          (engine.getSynthVoiceRMS(0, synthIdx) + engine.getSynthVoiceRMS(1, synthIdx)) / 2.0f;
        if (localSynthVoiceLevel[synthIdx] != -1.0f)
        {
            calcLevel(&synthLevel[synthIdx], localSynthVoiceLevel[synthIdx]);
        }
    }
}
#endif

// musi byc wywolana po  calcDelayLevel i calcReverbLevel i calc LineInLevel
void cMasterParams::calcDryMixLevel()
{
    for (uint8_t i = 0; i < 2; i++)
    {
        localDryMixLevel[i] = engine.getDryMixRMS(i);
        if (localDryMixLevel[i] != -1.0f)
        {
            calcLevel(&dryMixLevel[i], localDryMixLevel[i]);
        }
    }
}

float cMasterParams::calcLevelDecybelValue(uint8_t val)
{
    return 20 * log10f((float)mixAmpLogValues[val]);
}

void cMasterParams::calcLevel(strTrackLevel* level, float value)
{
    constexpr float RMS_LEVEL_0DB = 0.71f;
    constexpr int16_t LOW_MIXER_LEVEL_IN_DECIBELS = -36;
    constexpr int16_t UP_MIXER_LEVEL_IN_DECIBELS = 6;
    constexpr uint8_t MIN_LEVEL_BAR = 0;
    constexpr uint8_t CLIP_TRESHOLD_LEVEL_BAR = 95;
    constexpr uint8_t MAX_LEVEL_BAR = 100;

    level->measureSum += value;
    level->measureCounter++;

    if (level->measureCounter == 10)
    {
        level->measureCounter = 0;

        float averageMeasure = level->measureSum / 10.0f;
        int16_t averageMeasureInDecibels = (averageMeasure > 0.0f)
                                             ? 10 * log10f(averageMeasure / RMS_LEVEL_0DB)
                                             : LOW_MIXER_LEVEL_IN_DECIBELS;

        if (averageMeasureInDecibels < LOW_MIXER_LEVEL_IN_DECIBELS)
            averageMeasureInDecibels = LOW_MIXER_LEVEL_IN_DECIBELS;
        else if (averageMeasureInDecibels > UP_MIXER_LEVEL_IN_DECIBELS)
            averageMeasureInDecibels = UP_MIXER_LEVEL_IN_DECIBELS;

        uint8_t localLevel = (averageMeasureInDecibels <= 0)
                               ? mapClamped<int>(averageMeasureInDecibels,
                                                 LOW_MIXER_LEVEL_IN_DECIBELS,
                                                 0,
                                                 MIN_LEVEL_BAR,
                                                 CLIP_TRESHOLD_LEVEL_BAR)
                               : mapClamped<int>(averageMeasureInDecibels,
                                                 0,
                                                 UP_MIXER_LEVEL_IN_DECIBELS,
                                                 CLIP_TRESHOLD_LEVEL_BAR,
                                                 MAX_LEVEL_BAR);

        if (((level->timer > 500)) && (level->value != 0))
        {
            level->timer = 0;
            level->value--;
        }
        if (localLevel > (level->value + 1))
            level->value = localLevel;

        level->measureSum = 0;
    }
}

void cMasterParams::resetMixerLevels()
{
    for (uint8_t track = 0; track < 8; track++)
    {
        resetLevel(&trackLevel[track]);

        while (instrumentPlayer[track].getRMSValue() != -1.0f)
            ;
    }
}

void cMasterParams::resetMixerDelayReverbLevels()
{
    for (uint8_t channel = 0; channel < 2; channel++)
    {
        resetLevel(&delayLevel[channel]);
        resetLevel(&reverbLevel[channel]);
        resetLevel(&dryMixLevel[channel]);
        resetLevel(&lineInLevel[channel]);

#ifdef FEATURE_SYNTH
        for (int synthIdx = 0; synthIdx < kSynthParts; synthIdx++)
        {
            resetLevel(&synthLevel[synthIdx]);
        }
#endif

        while (engine.getDelayRMS(channel) != -1.0f)
            ;
        while (engine.getReverbRMS(channel) != -1.0f)
            ;
        while (engine.getDryMixRMS(channel) != -1.0f)
            ;
        while (engine.getLineInRMS(channel) != -1.0f)
            ;

#ifdef FEATURE_SYNTH
        for (int synthIdx = 0; synthIdx < kSynthParts; synthIdx++)
        {
            while (engine.getSynthVoiceRMS(channel, synthIdx) != -1.0f)
                ;
        }
#endif
    }
}

void cMasterParams::resetLevel(strTrackLevel* level)
{
    level->lastValue = 0;
    level->measureCounter = 0;
    level->measureSum = 0.0f;
    level->redColorTimer = 350;
    level->timer = 0;
    level->value = 0;
}

// mixer
// funkcja podpieta do shifta - zamienia mute na solo
static void functSoloMute(uint8_t state)
{
    if (MP->displayType == cMasterParams::displayMode::masterValues)
    {
        return;
    }
    if (MP->editTrackNameMode)
    {
        MP->isSolo = false;
        return;
    }

    if (state == buttonPress || state == buttonRelease)
    {
        MP->isSolo = state;
        MP->showMixerScreen();
    }
}

static void functSoloMuteTrack(uint8_t n, uint8_t state)
{
    if (MP->displayType == cMasterParams::displayMode::masterValues)
    {
        return;
    }

    if (state == buttonRelease)
    {
        if (MP->editTrackNameMode)
        {
            return;
        }

        MP->trackIsEdited[n] = 0;
        if (MP->ignoreMuteRelease[n])
        {
            MP->ignoreMuteRelease[n] = false;
            return;
        }

        if (MP->isSolo)
        {
            if ((uint8_t)MP->soloTrack == n)
            {
                MP->soloTrack = -1;
                for (uint8_t i = 0; i < 8; i++)
                {
                    mtProject.values.trackMute[i] = 0;
                }
            }
            else
            {
                for (uint8_t i = 0; i < 8; i++)
                {
                    mtProject.values.trackMute[i] = 1;
                }
                mtProject.values.trackMute[n] = 0;
                MP->soloTrack = n;
            }
        }
        else
        {
            mtProject.values.trackMute[n] = !mtProject.values.trackMute[n];
            MP->soloTrack = -1;
        }

        for (uint8_t i = 0; i < 8; i++)
        {
            engine.muteTrack(i, mtProject.values.trackMute[i]);
            MP->showLevelBar(i, &MP->trackLevel[i]);
        }
        MP->showMixerScreen();
    }

    if (state == buttonPress)
    {
        if (MP->editTrackNameMode)
        {
            MP->editTrackName(n);
            return;
        }
        MP->trackIsEdited[n] = true;
    }
}

// mixer delay reverb
static void functSoloMuteDelay(uint8_t state)
{
    if (MP->displayType == cMasterParams::displayMode::masterValues)
    {
        return;
    }

    if (state == buttonRelease)
    {
        MP->delayIsEdited = 0;
        if (MP->ignoreMuteReleaseDelay)
        {
            MP->ignoreMuteReleaseDelay = false;
            return;
        }

        if (MP->isSolo)
        {
            if (MP->soloTrack == cMasterParams::delayIsSolo)
            {
                MP->soloTrack = -1;

                mtProject.values.delayMute = 0;
                mtProject.values.dryMixMute = 0;
                mtProject.values.reverbMute = 0;
                mtProject.values.lineInMute = 0;
            }
            else
            {
                mtProject.values.delayMute = 0;
                mtProject.values.dryMixMute = 1;
                mtProject.values.reverbMute = 1;
                mtProject.values.lineInMute = 1;
                MP->soloTrack = cMasterParams::delayIsSolo;
            }
        }
        else
        {
            mtProject.values.delayMute = !mtProject.values.delayMute;
            MP->soloTrack = -1;
        }

        MP->showLevelBar(cMasterParams::delayVolumeScreenPosition, &MP->delayLevel[0]);
        MP->showLevelBar(cMasterParams::delayVolumeScreenPosition + 4, &MP->delayLevel[1]);
        engine.refreshTrackVolume();
        engine.refreshDelayVolume();
        engine.refreshReverbVolume();
        engine.refreshLineInStreamVolume();
        engine.refreshLineInStreamReverbSend();
        engine.refreshLineInStreamDelaySend();
        MP->showMixerDelayReverbScreen();
    }

    if (state == buttonPress)
    {
        MP->delayIsEdited = true;
    }
}

static void functSoloMuteReverb(uint8_t state)
{
    if (MP->displayType == cMasterParams::displayMode::masterValues)
    {
        return;
    }

    if (state == buttonRelease)
    {
        MP->reverbIsEdited = 0;
        if (MP->ignoreMuteReleaseReverb)
        {
            MP->ignoreMuteReleaseReverb = false;
            return;
        }

        if (MP->isSolo)
        {
            if (MP->soloTrack == cMasterParams::reverbIsSolo)
            {
                MP->soloTrack = -1;

                mtProject.values.delayMute = 0;
                mtProject.values.dryMixMute = 0;
                mtProject.values.reverbMute = 0;
                mtProject.values.lineInMute = 0;
            }
            else
            {
                mtProject.values.delayMute = 1;
                mtProject.values.dryMixMute = 1;
                mtProject.values.reverbMute = 0;
                mtProject.values.lineInMute = 1;
                MP->soloTrack = cMasterParams::reverbIsSolo;
            }
        }
        else
        {
            mtProject.values.reverbMute = !mtProject.values.reverbMute;
            MP->soloTrack = -1;
        }

        MP->showLevelBar(cMasterParams::reverbVolumeScreenPosition, &MP->reverbLevel[0]);
        MP->showLevelBar(cMasterParams::reverbVolumeScreenPosition + 4, &MP->reverbLevel[1]);
        engine.refreshTrackVolume();
        engine.refreshDelayVolume();
        engine.refreshReverbVolume();
        engine.refreshLineInStreamVolume();
        engine.refreshLineInStreamReverbSend();
        engine.refreshLineInStreamDelaySend();
        MP->showMixerDelayReverbScreen();
    }

    if (state == buttonPress)
    {
        MP->reverbIsEdited = true;
    }
}

static void functSoloMuteDryMix(uint8_t state)
{
    if (MP->displayType == cMasterParams::displayMode::masterValues)
    {
        return;
    }

    if (state == buttonRelease)
    {
        MP->dryMixIsEdited = false;
        if (MP->ignoreMuteReleaseDryMix)
        {
            MP->ignoreMuteReleaseDryMix = false;
            return;
        }

        if (MP->isSolo)
        {
            if (MP->soloTrack == cMasterParams::dryMixIsSolo)
            {
                MP->soloTrack = -1;

                mtProject.values.delayMute = 0;
                mtProject.values.dryMixMute = 0;
                mtProject.values.reverbMute = 0;
                mtProject.values.lineInMute = 0;
            }
            else
            {
                mtProject.values.delayMute = 1;
                mtProject.values.dryMixMute = 0;
                mtProject.values.reverbMute = 1;
                mtProject.values.lineInMute = 1;
                MP->soloTrack = cMasterParams::dryMixIsSolo;
            }
        }
        else
        {
            mtProject.values.dryMixMute = !mtProject.values.dryMixMute;
            MP->soloTrack = -1;
        }

        MP->showLevelBar(cMasterParams::dryMixVolumeScreenPosition, &MP->dryMixLevel[0]);
        MP->showLevelBar(cMasterParams::dryMixVolumeScreenPosition + 4, &MP->dryMixLevel[1]);
        engine.refreshTrackVolume();
        engine.refreshDelayVolume();
        engine.refreshReverbVolume();
        engine.refreshLineInStreamVolume();
        MP->showMixerDelayReverbScreen();
    }

    if (state == buttonPress)
    {
        MP->dryMixIsEdited = true;
    }
}
#ifdef FEATURE_SYNTH
static void functSelectSynth1(uint8_t state)
{
    if (MP->displayType == cMasterParams::displayMode::masterValues)
    {
        return;
    }

    if (state == buttonPress)
    {
        MP->synthGainIsEdited[0] = true;
    }
    else if (state == buttonRelease)
    {
        MP->synthGainIsEdited[0] = false;
    }
}

static void functSelectSynth2(uint8_t state)
{
    if (MP->displayType == cMasterParams::displayMode::masterValues)
    {
        return;
    }

    if (state == buttonPress)
    {
        MP->synthGainIsEdited[1] = true;
    }
    else if (state == buttonRelease)
    {
        MP->synthGainIsEdited[1] = false;
    }
}

static void functSelectSynth3(uint8_t state)
{
    if (MP->displayType == cMasterParams::displayMode::masterValues)
    {
        return;
    }

    if (state == buttonPress)
    {
        MP->synthGainIsEdited[2] = true;
    }
    else if (state == buttonRelease)
    {
        MP->synthGainIsEdited[2] = false;
    }
}
#endif

static void functSoloMuteLineIn(uint8_t state)
{
    if (MP->displayType == cMasterParams::displayMode::masterValues)
    {
        return;
    }

    if (state == buttonRelease)
    {
        MP->lineInIsEdited = false;
        if (MP->ignoreMuteReleaseLineIn)
        {
            MP->ignoreMuteReleaseLineIn = false;
            return;
        }

        if (MP->isSolo)
        {
            if (MP->soloTrack == cMasterParams::lineInIsSolo)
            {
                MP->soloTrack = -1;

                mtProject.values.delayMute = 0;
                mtProject.values.dryMixMute = 0;
                mtProject.values.reverbMute = 0;
                mtProject.values.lineInMute = 0;
            }
            else
            {
                mtProject.values.delayMute = 1;
                mtProject.values.dryMixMute = 1;
                mtProject.values.reverbMute = 1;
                mtProject.values.lineInMute = 0;
                MP->soloTrack = cMasterParams::lineInIsSolo;
            }
        }
        else
        {
            mtProject.values.lineInMute = !mtProject.values.lineInMute;
            MP->soloTrack = -1;
        }

        MP->showLevelBar(cMasterParams::lineInVolumeScreenPosition, &MP->lineInLevel[0]);
        MP->showLevelBar(cMasterParams::lineInVolumeScreenPosition + 4, &MP->lineInLevel[1]);
        engine.refreshTrackVolume();
        engine.refreshDelayVolume();
        engine.refreshReverbVolume();
        engine.refreshLineInStreamVolume();
        engine.refreshLineInStreamDelaySend();
        engine.refreshLineInStreamReverbSend();
        MP->showMixerDelayReverbScreen();
    }

    if (state == buttonPress)
    {
        MP->lineInIsEdited = true;
    }
}

// funkcja podpieta do shifta - zamienia mute na solo
static void functSoloMuteDelayReverb(uint8_t state)
{
    if (MP->displayType == cMasterParams::displayMode::masterValues)
    {
        return;
    }

    if (state == buttonPress || state == buttonRelease)
    {
        MP->isSolo = state;
        MP->showMixerDelayReverbScreen();
    }
}

static void functSwitchModeMaster(uint8_t state)
{
    if (state == buttonPress)
    {
        MP->displayTypeBeforeMasterPress = MP->displayType;
        MP->selectedPlaceBeforeMasterPress = MP->selectedPlace;

        MP->sourceModuleButton = interfaceButtonMaster;
        MP->showMasterVolumeOnHold = true;
    }
    else if (state == buttonHold)
    {

        if (MP->showMasterVolumeOnHold)
        {
            MP->showMasterVolumeOnHold = false;
            MP->selectedPlace = 0;
            MP->switchToMaster();
        }
    }
    else if (state == buttonRelease)
    {

        if (MP->sourceModuleButton == interfaceButtonMaster)
        {
            if (MP->showMasterVolumeOnHold)
            {
                MP->switchScreen();
            }
            else
            {
                MP->returnToScreen();
            }
        }
        else
        {
            if (!MP->showMasterVolumeOnHold)
            {
                MP->displayType = MP->displayTypeBeforeMasterPress;
                MP->selectedPlace = MP->selectedPlaceBeforeMasterPress;

                if (MP->sourceModuleButton == interfaceButtonSampleRec)
                {
                    MP->eventFunct(eventSwitchToPreviousModule, MP, &MP->sourceModuleButton, 0);
                }
                else
                {
                    MP->eventFunct(eventSwitchToPreviousModule, MP, 0, 0);
                }
            }
        }
    }
}

//======================================================================================================================
// master
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
    // zabezpieczenie przed przypadkowym wysadzeniem glosnikow
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

// equalizer
// Mixer
void cMasterParams::changeTrackVolume(int16_t val, uint8_t track)
{
    if (mtProject.values.trackVolume[track] + val > TRACK_VOLUME_MAX)
        mtProject.values.trackVolume[track] = TRACK_VOLUME_MAX;
    else if (mtProject.values.trackVolume[track] + val < TRACK_VOLUME_MIN)
        mtProject.values.trackVolume[track] = TRACK_VOLUME_MIN;
    else
        mtProject.values.trackVolume[track] += val;

    engine.refreshTrackVolume();
    newFileManager.setProjectStructChanged();
    showTrackVolumeBar(track);
}
void cMasterParams::setDefaultTrackVolume(uint8_t track)
{
    mtProject.values.trackVolume[track] = DEFAULT_TRACK_VOLUME;
    engine.refreshTrackVolume();
    newFileManager.setProjectStructChanged();
    showTrackVolumeBar(track);
}

void cMasterParams::changeReverbVolume(int16_t val)
{
    if (mtProject.values.reverbVolume + val > REVERB_VOLUME_MAX)
        mtProject.values.reverbVolume = REVERB_VOLUME_MAX;
    else if (mtProject.values.reverbVolume + val < REVERB_VOLUME_MIN)
        mtProject.values.reverbVolume = REVERB_VOLUME_MIN;
    else
        mtProject.values.reverbVolume += val;

    newFileManager.setProjectStructChanged();
    showReverbVolumeBar();
    engine.refreshReverbVolume();
}
void cMasterParams::changeDelayVolume(int16_t val)
{
    if (mtProject.values.delayVolume + val > DELAY_VOLUME_MAX)
        mtProject.values.delayVolume = DELAY_VOLUME_MAX;
    else if (mtProject.values.delayVolume + val < DELAY_VOLUME_MIN)
        mtProject.values.delayVolume = DELAY_VOLUME_MIN;
    else
        mtProject.values.delayVolume += val;

    newFileManager.setProjectStructChanged();
    showDelayVolumeBar();
    engine.refreshDelayVolume();
}
void cMasterParams::changeDryMixVolume(int16_t val)
{
    if (mtProject.values.dryMixVolume + val > DRY_MIX_VOLUME_MAX)
        mtProject.values.dryMixVolume = DRY_MIX_VOLUME_MAX;
    else if (mtProject.values.dryMixVolume + val < DRY_MIX_VOLUME_MIN)
        mtProject.values.dryMixVolume = DRY_MIX_VOLUME_MIN;
    else
        mtProject.values.dryMixVolume += val;

    newFileManager.setProjectStructChanged();
    showDryMixVolumeBar();
    engine.refreshTrackVolume();
}

void cMasterParams::changeLineInVolume(int16_t val)
{
    if (mtProject.values.lineInVolume + val > LINE_IN_VOLUME_MAX)
        mtProject.values.lineInVolume = LINE_IN_VOLUME_MAX;
    else if (mtProject.values.lineInVolume + val < LINE_IN_VOLUME_MIN)
        mtProject.values.lineInVolume = LINE_IN_VOLUME_MIN;
    else
        mtProject.values.lineInVolume += val;

    newFileManager.setProjectStructChanged();
    showLineInVolumeBar();
    engine.refreshLineInStreamVolume();
    engine.refreshLineInStreamDelaySend();
    engine.refreshLineInStreamReverbSend();
}

void cMasterParams::changeLineInDelaySend(int16_t val)
{
    mtProject.values.lineInDelaySend =
      clamp<int>(mtProject.values.lineInDelaySend + val, 0, LINE_IN_SEND_MAX);

    newFileManager.setProjectStructChanged();
    showLineInDelaySend();
    engine.refreshLineInStreamDelaySend();
}

void cMasterParams::changeLineInReverbSend(int16_t val)
{
    mtProject.values.lineInReverbSend =
      clamp<int>(mtProject.values.lineInReverbSend + val, 0, LINE_IN_SEND_MAX);

    newFileManager.setProjectStructChanged();
    showLineInReverbSend();
    engine.refreshLineInStreamReverbSend();
}

#ifdef FEATURE_SYNTH
void cMasterParams::changeSynthGain(int synthIdx, int16_t val)
{
    mtProject.values.synthPart[synthIdx].gainDb =
      clamp<float>(mtProject.values.synthPart[synthIdx].gainDb + (val * 0.5),
                   Synth::kGainSilence,
                   Synth::kGainMax);

    engine.setSynthParam(audioEngine::SynthParam::kVolume,
                         synthIdx,
                         mtProject.values.synthPart[synthIdx].gainDb,
                         audioEngine::SynthValuesSource::kInstrument);

    showSynthGain(synthIdx);

    newFileManager.setProjectStructChanged();
}

void cMasterParams::setDefaultSynthGain(int synthIdx)
{
    mtProject.values.synthPart[synthIdx].gainDb =
      clamp<float>(Synth::kGainDefault, Synth::kGainSilence, Synth::kGainMax);

    engine.setSynthParam(audioEngine::SynthParam::kVolume,
                         synthIdx,
                         mtProject.values.synthPart[synthIdx].gainDb,
                         audioEngine::SynthValuesSource::kInstrument);

    showSynthGain(synthIdx);

    newFileManager.setProjectStructChanged();
}
#endif

void cMasterParams::setDefaultReverbVolume()
{
    mtProject.values.reverbVolume = DEFAULT_REVERB_VOLUME;
    newFileManager.setProjectStructChanged();
    showReverbVolumeBar();
    engine.refreshReverbVolume();
}
void cMasterParams::setDefaultDelayVolume()
{
    mtProject.values.delayVolume = DEFAULT_DELAY_VOLUME;
    newFileManager.setProjectStructChanged();
    showDelayVolumeBar();
    engine.refreshDelayVolume();
}
void cMasterParams::setDefaultDryMixVolume()
{
    mtProject.values.dryMixVolume = DEFAULT_DRY_MIX_VOLUME;
    newFileManager.setProjectStructChanged();
    showDryMixVolumeBar();
    engine.refreshTrackVolume();
}

void cMasterParams::setDefaultLineInVolume()
{
    mtProject.values.lineInVolume = DEFAULT_LINE_IN_VOLUME;
    newFileManager.setProjectStructChanged();
    showLineInVolumeBar();
    engine.refreshLineInStreamVolume();
}

void cMasterParams::setDefaultLineInDelaySend()
{
    mtProject.values.lineInDelaySend = 0;
    newFileManager.setProjectStructChanged();
    showLineInDelaySend();
    engine.refreshLineInStreamDelaySend();
}

void cMasterParams::setDefaultLineInReverbSend()
{
    mtProject.values.lineInReverbSend = 0;
    newFileManager.setProjectStructChanged();
    showLineInReverbSend();
    engine.refreshLineInStreamReverbSend();
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

    if (MP->keyboardManager.getState())
    {
        MP->keyboardManager.onPadChange(pad, state);
        return;
    }

    bool isBeatSlice =
      mtProject.instrument[mtProject.values.lastUsedInstrument].playMode == playModeBeatSlice;

    if (state == buttonPress)
    {
        // uint8_t note = mtPadBoard.convertPadToNote(pad);
        // if(note > 48) note = 48;
        // editorInstrument->tune = note;

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
// ###############################################################################################
//                            NAZYWANIE TRACKOW

// przelaczanie trybu edycji nazw trackow
static void functEditModeToggle()
{
    if (MP->displayType != MP->displayMode::mixer)
    {
        return;
    }

    MP->editTrackNameMode = !MP->editTrackNameMode;

    MP->showEditTracksNamesMode();
}

void cMasterParams::editTrackName(uint8_t button)
{
    actualEditingTrackName = button;

    MP->setKeyboardExportFunctions();

    MP->keyboardManager.fillName(&mtProject.values.TrackNames[button][0]);
    MP->keyboardManager.activateKeyboard(7);

    MP->showKeyboardExport();
}

// anuluj edycje nazwy tracka
static void functCancelEditTrackName()
{
    MP->setMasterScreenFunct();
    MP->switchToMixer();
    MP->editTrackNameMode = 1;
    MP->showEditTracksNamesMode();

    MP->keyboardManager.deactivateKeyboard();
}

// zapisz nazwe tracka
static void functSaveTrackName()
{
    if (MP->actualEditingTrackName < kSampleTracks)
    {
        strncpy(&mtProject.values.TrackNames[MP->actualEditingTrackName][0],
                MP->keyboardManager.getName(),
                7);
        mtProject.values.TrackNames[MP->actualEditingTrackName][7] = 0;
    }

    newFileManager.setProjectStructChanged();

    MP->setMasterScreenFunct();
    MP->switchToMixer();
    MP->editTrackNameMode = 1;
    MP->showEditTracksNamesMode();

    MP->keyboardManager.deactivateKeyboard();
}

// Zatwierdzenie znaku w klawiaturze
static void functEnterKeyExport()
{
    MP->keyboardManager.confirmKey();
}

// Modyfikacja zaznaczenia znaku w klawiaturze
static void functLeftKeyboard()
{
    MP->keyboardManager.makeMove('a');
}

// Modyfikacja zaznaczenia znaku w klawiaturze
static void functRightKeyboard()
{
    MP->keyboardManager.makeMove('d');
}

// Modyfikacja zaznaczenia znaku w klawiaturze
static void functUpKeyboard()
{
    MP->keyboardManager.makeMove('w');
}

// Modyfikacja zaznaczenia znaku w klawiaturze
static void functDownKeyboard()
{
    MP->keyboardManager.makeMove('s');
}

// Backspace znaku z przycisku delete
static void functDeleteKeyboard()
{
    MP->keyboardManager.makeBackspace();
}

// Zatwierdzenie znaku z przycisku insert
static void functInsertKeyboard()
{
    MP->keyboardManager.confirmKey();
}

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

    if (!mtInterface.isMasterScreenActive())
        return;

    MP->refreshVolumeFromCC[ccToIdx] = true;
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
