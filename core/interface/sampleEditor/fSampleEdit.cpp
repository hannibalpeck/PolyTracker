// ProTracker sample editor: the stock Polyend sample editor screens (main screen with start / end /
// zoom / effect list, effect parameter screen), working on one of the 31 song samples. A sample is
// 8 bit mono, the effects work on a 16 bit copy that is converted back to 8 bit when it is applied.
#include "core/graphicProcessing.h"
#include "core/interfacePopups.h"
#include "mtSampleEditorEngine.h"
#include "protrackerMode/protrackerMode.h"
#include "ptTables.h"
#include "sampleEditor/sampleEditor.h"
#include "util_math.h"

#include <cr_section_macros.h>

__NOINIT(EXTERNAL_RAM) strTrackerSpectrum cSampleEditor::spectrumData[2];
__NOINIT(EXTERNAL_RAM) strEffectDisplayParams cSampleEditor::effectDisplayParamsToAdjust[editorEffectMax];
__NOINIT(EXTERNAL_RAM) strInstrument cSampleEditor::sampleEditInstrument;

// What is auditioned (8 bit) has to stay valid while the preview voice reads it. External RAM is
// full, so it lives in the effect buffer, in the gap behind the first (<= 64K sample) edit area.
#define previewBuf8 ((int8_t*)(externalRamEffectsBuff + 262144))

cSampleEditor sampleEditor;

static cSampleEditor* SE = &sampleEditor;
static cProTrackerMode* PT = &protrackerMode;

// Paula plays C-3 (period 214) at 7093789 / 214 / 2 bytes per second
static constexpr float kC3Rate = 16574.0f;

static const uint16_t mainScreenFramePlaces[4][4] = {
    { (800 / 8) * 0 + 1, 424, 800 / 8 - 3, 55 },
    { (800 / 8) * 1 + 1, 424, 800 / 8 - 3, 55 },
    { (800 / 8) * 2 + 1, 424, 800 / 8 - 3, 55 },
    { (800 / 8) * 6 + 1, 31, 800 / 4 - 3, 391 },
};

static const uint16_t paramsScreenFramePlaces[6][4] = {
    { (800 / 8) * 0 + 1, 31, 800 / 8 - 3, 391 }, { (800 / 8) * 1 + 1, 31, 800 / 8 - 3, 391 },
    { (800 / 8) * 2 + 1, 31, 800 / 8 - 3, 391 }, { (800 / 8) * 3 + 1, 31, 800 / 8 - 3, 391 },
    { (800 / 8) * 4 + 1, 31, 800 / 8 - 3, 391 }, { (800 / 8) * 5 + 1, 31, 800 / 8 - 3, 391 },
};

// ACTION FUNCTIONS DECLARATIONS
static void functSwitchModule(uint8_t button);
static void functPads(uint8_t pad, uint8_t state, int16_t velo);

static void functMainScreenLeft();
static void functMainScreenRight();
static void functMainScreenUp();
static void functMainScreenDown();
static void functMainScreenEncoder(int16_t value);
static void functMainScreenDelete();

static void functSelectStartPoint();
static void functSelectEndPoint();
static void functSelectZoom();
static void functListUp();
static void functListDown();
static void functUndo();
static void functApply();

static void functParamsScreenLeft();
static void functParamsScreenRight();
static void functParamsScreenUp();
static void functParamsScreenDown();
static void functParamsScreenEncoder(int16_t value);
static void functParamsScreenDelete();
static void functSelectParameter(uint8_t button);
static void functParamsScreenMore();
static void functBack();
static void functPreview();
static void functPlay();
static void functStop();
static void functSave();
static void functTooLongProcessedSampleOk();

// ---------------------------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------------------------
static protracker::Sample& slotSample()
{
    return PT->module.samples[SE->slot - 1];
}

// 16 bit -> 8 bit with rounding
static uint32_t toBytes(const int16_t* src, uint32_t length, int8_t* dst)
{
    if (length > protracker::kSampleSlotBytes)
        length = protracker::kSampleSlotBytes;
    for (uint32_t i = 0; i < length; i++)
    {
        int32_t v = (src[i] + 128) >> 8;
        dst[i] = (int8_t)(v > 127 ? 127 : (v < -128 ? -128 : v));
    }
    return length;
}

static uint16_t periodForNote(int note)
{
    if (note < 0)
        note = 0;
    if (note > 35)
        note = 35;
    return protracker::kPeriodTable[(slotSample().fineTune & 0x0F) * 37 + note];
}

static uint8_t previewVolume()
{
    uint8_t v = slotSample().volume;
    return v == 0 ? 64 : v;
}

// the edited result goes back into the song's sample slot
static void commitToModule()
{
    protracker::Module& m = PT->module;
    int8_t* data = m.sampleData(SE->slot - 1);
    protracker::Sample& s = slotSample();

    if (PT->stream != nullptr)
        PT->stream->pause();

    uint32_t n = SE->currentEffect->storeToMemory(data, protracker::kMaxSampleBytes);
    n &= ~1u;
    if (n < protracker::kSampleSlotBytes)
        memset(data + n, 0, protracker::kSampleSlotBytes - n);

    s.length = n;
    if (s.loopLength > 2 && s.loopStart + s.loopLength > n)
    {
        s.loopStart = 0;
        s.loopLength = 2;
    }
    m.fixSample(SE->slot - 1);

    if (PT->stream != nullptr)
        PT->stream->resume();

    PT->dirty = true;
}

static void stopAudition()
{
    if (PT->stream != nullptr)
        PT->stream->requestPreviewStop();
}

//**************************************************************************

void cSampleEditor::start([[maybe_unused]] uint32_t options)
{
    moduleRefresh = 1;

    stopAudition();

    slot = (uint8_t)PT->sample;
    if (slot < 1)
        slot = 1;
    if (slot > protracker::kSampleCount)
        slot = protracker::kSampleCount;

    const uint32_t length = slotSample().length;
    lastEffect = nullptr;

    if (selectedEffectListIdx >= editorEffectMax)
        selectedEffectListIdx = 0;
    currentEffectIdx = (uint8_t)selectedEffectListIdx;
    currentEffect = sampleEditorEffect[currentEffectIdx];

    currentEffect->loadFromMemory(PT->module.sampleData(slot - 1), length);
    currentEffect->clearProcessedData();

    editorInstrument->isActive = length > 0;
    editorInstrument->sample.length = length;

    snprintf(currentInstrumentName, sizeof(currentInstrumentName), "%02d", slot);

    spectrumParams[0].address = currentEffect->getAddresToPolyphonicPlay(mtEffect::Channel::kLeft);
    spectrumParams[0].length = length;
    spectrumParams[1].address = nullptr;
    spectrumParams[1].length = 0;

    screenType = mainScreen;
    previewState = previewStatePreview;
    confirmedDataIsChanged = 0;
    applyingInProgress = 0;
    processingInProgress = 0;
    playingInProgress = 0;
    isLoadedData = true;
    isProcessedData = false;
    needRefreshSpectrum = 0;
    needRefreshPoints = 0;
    needRefreshPlayhead = 0;

    selection.startPoint = 0;
    selection.endPoint = MAX_16BIT;
    GP.spectrumResetZoom(0, length, &zoom);

    selectedPlace[mainScreen] = length > 0 ? startPointButton : effectButton;

    setMainScreenFunctions();
    setCommonFunctions();

    prepareDisplayDataMainScreen();
    showMainScreen();

    memcpy(effectDisplayParamsToAdjust, effectDefaultParamsTable, sizeof(effectDefaultParamsTable));

    for (uint8_t idx = 0; idx < getCurrentEffectDisplayParams().paramsNumber; idx++)
        currentEffect->setParameter(effectDisplayParamsToAdjust[currentEffectIdx].parameter[idx], idx);

    currentEffect->changeSelectionRange(selection.startPoint, selection.endPoint);
}

void cSampleEditor::update()
{
    if (needRefreshSpectrum)
    {
        needRefreshSpectrum = 0;
        refreshSpectrum();
    }
    if (needRefreshPoints)
    {
        needRefreshPoints = 0;
        refreshSpectrumPoints();
    }

    if (!currentEffect->isDataProcessed() && currentEffect->getProcessingSelectionState())
    {
        currentEffect->update();
        if (!currentEffect->getProcessingSelectionState())
        {
            const bool wasApplying = applyingInProgress;
            const bool wasPreviewing = processingInProgress;

            applyingInProgress = 0;
            processingInProgress = false;
            isProcessedData = currentEffect->isDataProcessed();

            reloadSpectrumData();
            needRefreshSpectrum = 1;

            if (wasApplying)
            {
                commitToModule();
                confirmedDataIsChanged = 1;
                editorInstrument->isActive = editorInstrument->sample.length > 0;
                resetZoom();
                screenType = cSampleEditor::mainScreen;
                switchScreen(screenType);
            }

            hideProgressPopup();

            selection.startPoint = currentEffect->getNewStartPoint();
            selection.endPoint = currentEffect->getNewEndPoint();
            refreshStartPoint();
            refreshEndPoint();

            refreshUndoState();
            restoreFunctions();

            if (wasApplying)
            {
                previewState = previewStatePreview;
                currentEffect->clearProcessedData();
            }
            else if (wasPreviewing)
            {
                setPlayFunction();
                functPlay();
            }
        }
    }

    if (applyingInProgress)
        refreshApplyingProgress();
    else if (processingInProgress)
        refreshProcessingProgress();

    // the audition has run out
    if (playingInProgress && playTimer > 150 && PT->stream != nullptr && !PT->stream->isPreviewing())
    {
        playingInProgress = 0;
        restoreFunctions();
        setPlayFunction();
    }
}

void cSampleEditor::stop()
{
    moduleRefresh = 0;
    stopAudition();
}

void cSampleEditor::restoreCurrentScreenFunctions()
{
    setCommonFunctions();

    if (screenType == enScreenType::mainScreen)
        setMainScreenFunctions();
    else if (screenType == enScreenType::effectScreen)
        setParamsScreenFunctions();
}

void cSampleEditor::setCommonFunctions()
{
    ip->setButtonObj(interfaceButtonParams, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonPerformance, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonFile, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonConfig, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonMaster, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSamplePlay, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleEdit, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleRec, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleLoad, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSong, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonPattern, buttonPress, functSwitchModule);

    if (!editorInstrument->isActive)
        return;
    ip->setPadsGlobal(functPads);
}

void cSampleEditor::setMainScreenFunctions()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearButton(interfaceButtonLeft);
    ip->clearButton(interfaceButtonRight);
    ip->clearButton(interfaceButtonUp);
    ip->clearButton(interfaceButtonDown);
    ip->clearButton(interfaceButtonDelete);
    ip->clearAllPots();

    ip->setButtonObj(interfaceButtonLeft, buttonPress, functMainScreenLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functMainScreenRight);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functMainScreenUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functMainScreenDown);
    ip->setButtonObj(interfaceButtonDelete, buttonPress, functMainScreenDelete);

    if (editorInstrument->isActive)
    {
        ip->setButtonObj(interfaceButton0, buttonPress, functSelectStartPoint);
        ip->setButtonObj(interfaceButton1, buttonPress, functSelectEndPoint);
        ip->setButtonObj(interfaceButton2, buttonPress, functSelectZoom);
        ip->setButtonObj(interfaceButton3, buttonPress, functUndo);
    }
    ip->setButtonObj(interfaceButton5, buttonPress, functApply);
    ip->setButtonObj(interfaceButton6, buttonPress, functListUp);
    ip->setButtonObj(interfaceButton7, buttonPress, functListDown);

    ip->setPotObj(interfacePot0, functMainScreenEncoder);
}

void cSampleEditor::setParamsScreenFunctions()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearButton(interfaceButtonLeft);
    ip->clearButton(interfaceButtonRight);
    ip->clearButton(interfaceButtonUp);
    ip->clearButton(interfaceButtonDown);
    ip->clearButton(interfaceButtonDelete);
    ip->clearAllPots();

    ip->setButtonObj(interfaceButtonLeft, buttonPress, functParamsScreenLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functParamsScreenRight);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functParamsScreenUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functParamsScreenDown);
    ip->setButtonObj(interfaceButtonDelete, buttonPress, functParamsScreenDelete);
    ip->setButtonObj(interfaceButton4, buttonPress, functParamsScreenMore);

    for (uint8_t i = interfaceButton0; i <= interfaceButton3; i++)
    {
        if (i < getCurrentEffectDisplayParams().paramsNumber)
            ip->setButtonObj(i, buttonPress, functSelectParameter);
        else
            break;
    }
    ip->setButtonObj(interfaceButton5, buttonPress, functBack);
    switch (previewState)
    {
        case previewStatePreview: ip->setButtonObj(interfaceButton6, buttonPress, functPreview); break;
        case previewStatePlay: ip->setButtonObj(interfaceButton6, buttonPress, functPlay); break;
        case previewStateStop: ip->setButtonObj(interfaceButton6, buttonPress, functStop); break;
    }

    ip->setButtonObj(interfaceButton7, buttonPress, functSave);

    ip->setPotObj(interfacePot0, functParamsScreenEncoder);
}

void cSampleEditor::setTooLongProcessedSampleFunctions()
{
    ip->clearAll();
    ip->setButtonObj(interfaceButton7, buttonPress, functTooLongProcessedSampleOk);
}

bool cSampleEditor::isParametersScreen()
{
    return (currentEffectIdx == editorEffectCut) || (currentEffectIdx == editorEffectReverse) ||
           (currentEffectIdx == editorEffectFadeIn) || (currentEffectIdx == editorEffectFadeOut);
}

bool cSampleEditor::canApplyCurrentEffect() const
{
    return editorInstrument->isActive && editorInstrument->sample.length > 0;
}

void cSampleEditor::switchScreen(enScreenType s)
{
    if (s == mainScreen)
    {
        prepareDisplayDataMainScreen();
        showMainScreen();
        setMainScreenFunctions();
    }
    else if (s == effectScreen)
    {
        prepareDisplayDataParamsScreen();
        showEffectParamsScreen();
        setParamsScreenFunctions();
    }
}

void cSampleEditor::restoreFunctions()
{
    setCommonFunctions();
    if (screenType == mainScreen)
        setMainScreenFunctions();
    else if (screenType == effectScreen)
        setParamsScreenFunctions();
}

// pads audition the selected part of the current result at the pad's pitch
void cSampleEditor::noteOnHandle([[maybe_unused]] uint8_t channel,
                                 uint8_t note,
                                 [[maybe_unused]] uint8_t velocity,
                                 [[maybe_unused]] int16_t source)
{
    const uint32_t total = currentEffect->getLengthToPolyphonicPlay();
    if (total < 2 || PT->stream == nullptr)
        return;

    const uint32_t end = (uint32_t)((uint32_t)selection.endPoint * (float)total / MAX_16BIT);
    const uint32_t start = (uint32_t)((uint32_t)selection.startPoint * (float)total / MAX_16BIT);
    if (end <= start + 1)
        return;

    const int16_t* src = currentEffect->getAddresToPolyphonicPlay(mtEffect::Channel::kLeft) + start;
    const uint32_t n = toBytes(src, end - start, previewBuf8);
    PT->stream->requestPreviewData(previewBuf8, n, periodForNote(note), previewVolume());
}

void cSampleEditor::noteOffHandle([[maybe_unused]] uint8_t channel,
                                  [[maybe_unused]] uint8_t note,
                                  [[maybe_unused]] uint8_t velocity,
                                  [[maybe_unused]] int16_t source)
{
    stopAudition();
}

bool cSampleEditor::isCurrentSampleStereo()
{
    return false;
}

void cSampleEditor::handleStartupWithPopupAndMenu()
{
    prepareDisplayDataMainScreen();
    showMainScreen();
}

void cSampleEditor::prepareDisplayDataMainScreen()
{
    reloadCurrentEffect();
    reloadStartPointText();
    reloadEndPointText();
    reloadZoomText();
    reloadPointsData();
    reloadFrameData(screenType);

    if (spectrumParams[0].address != nullptr && spectrumParams[0].length > 0)
    {
        GP.processSpectrum(&spectrumParams[0], &zoom, &spectrumData[0]);
    }
    else
    {
        for (uint16_t i = 0; i < 600; i++)
        {
            spectrumData[0].upperData[i] = 0;
            spectrumData[0].lowerData[i] = 0;
        }
        spectrumData[0].spectrumType = 1;
    }

    for (uint16_t i = 0; i < 600; i++)
    {
        spectrumData[1].upperData[i] = 0;
        spectrumData[1].lowerData[i] = 0;
    }
    spectrumData[1].spectrumType = 1;
}

void cSampleEditor::reloadStartPointText()
{
    float length = samplePosToTime(editorInstrument->sample.length);
    sprintf(startPointText, "%.3fs", (length * selection.startPoint) / MAX_16BIT);
}

void cSampleEditor::reloadEndPointText()
{
    float length = samplePosToTime(editorInstrument->sample.length);
    sprintf(endPointText, "%.3fs", (length * selection.endPoint) / MAX_16BIT);
}

void cSampleEditor::reloadZoomText()
{
    sprintf(zoomText, "%.2f", zoom.zoomValue);
}

void cSampleEditor::reloadPlayheadValue() {}

void cSampleEditor::reloadCurrentEffect()
{
    currentEffect = sampleEditorEffect[currentEffectIdx];

    if ((currentEffect != lastEffect) && (lastEffect != nullptr))
        currentEffect->switchToThisEffect(lastEffect);

    lastEffect = currentEffect;
}

void cSampleEditor::reloadSpectrumData()
{
    sampleEditorSampleMemory.address[mtEffect::Channel::kLeft] =
      currentEffect->getAddresToPolyphonicPlay(mtEffect::Channel::kLeft);
    sampleEditorSampleMemory.address[mtEffect::Channel::kRight] =
      currentEffect->getAddresToPolyphonicPlay(mtEffect::Channel::kRight);
    editorInstrument->sample.length = currentEffect->getLengthToPolyphonicPlay();
    spectrumParams[mtEffect::Channel::kLeft].address =
      sampleEditorSampleMemory.address[mtEffect::Channel::kLeft];
    spectrumParams[mtEffect::Channel::kLeft].length = editorInstrument->sample.length;
    spectrumParams[mtEffect::Channel::kRight].address = nullptr;
    spectrumParams[mtEffect::Channel::kRight].length = 0;
}

// popups
void cSampleEditor::reloadApplyingProgress()
{
    applyingProgress = currentEffect->getProcessingSelectionProgress();
}

void cSampleEditor::reloadProcessingProgress()
{
    processingProgress = currentEffect->getProcessingSelectionProgress();
}

void cSampleEditor::reloadPlayingProgress() {}

void cSampleEditor::reloadFrameData(enScreenType s)
{
    if (s == mainScreen)
    {
        frameData.placesCount = 4;
        for (uint8_t i = 0; i < frameData.placesCount; i++)
            frameData.places[i] = &mainScreenFramePlaces[i][0];
    }
    else if (s == effectScreen)
    {
        frameData.placesCount = getCurrentPageParams();
        for (uint8_t i = 0; i < frameData.placesCount; i++)
            frameData.places[i] = &paramsScreenFramePlaces[i][0];
    }
}

void cSampleEditor::reloadPointsData()
{
    if (selectedPlace[cSampleEditor::mainScreen] == cSampleEditor::startPointButton)
    {
        spectrumPointsData.selected = selectStart;
        bool isZoomActive = (zoom.zoomValue > 1);
        bool isPointDisplayed =
          (selection.startPoint < zoom.zoomStart || selection.startPoint > zoom.zoomEnd);
        if (isZoomActive && isPointDisplayed)
            needRefreshSpectrum = 1;
    }
    else if (selectedPlace[cSampleEditor::mainScreen] == cSampleEditor::endPointButton)
    {
        spectrumPointsData.selected = selectEnd;
        bool isZoomActive = (zoom.zoomValue > 1);
        bool isPointDisplayed =
          (selection.endPoint < zoom.zoomStart || selection.endPoint > zoom.zoomEnd);
        if (isZoomActive && isPointDisplayed)
            needRefreshSpectrum = 1;
    }
    else
    {
        spectrumPointsData.selected = 0;
    }

    if (selection.startPoint >= zoom.zoomStart && selection.startPoint <= zoom.zoomEnd)
        spectrumPointsData.startPoint =
          ((selection.startPoint - zoom.zoomStart) * (spectrumData[0].width - 1)) / zoom.zoomWidth;
    else
        spectrumPointsData.startPoint = -1;

    if (selection.endPoint >= zoom.zoomStart && selection.endPoint <= zoom.zoomEnd)
        spectrumPointsData.endPoint =
          ((selection.endPoint - zoom.zoomStart) * (spectrumData[0].width - 1)) / zoom.zoomWidth;
    else
        spectrumPointsData.endPoint = -1;
}

// params screen
void cSampleEditor::prepareDisplayDataParamsScreen()
{
    selectedPlace[effectScreen] = 0;
    effectScreenPage = 0;

    snprintf(paramsScreenLabel, 40, "Sample Editor %s", effectNamesLabels[currentEffectIdx]);
    paramsScreenLabel[39] = 0;

    for (uint8_t i = 0; i < getCurrentEffectDisplayParams().paramsNumber; i++)
    {
        reloadParameterValueText(i);
        reloadParameterBarValue(i);
    }
    reloadFrameData(effectScreen);
}

void cSampleEditor::reloadParameterValueText(uint8_t idx)
{
    const auto& params = getCurrentEffectDisplayParams();
    if (idx >= params.paramsNumber)
        return;

    float displayedValue =
      effectDisplayParamsToAdjust[currentEffectIdx].parameter[idx] * params.displayMult[idx];

    if (params.displayType[idx] == 'd')
        sprintf(parameterValueLabelPtr[idx], "%d", (int)displayedValue);
    else if (params.displayType[idx] == 'f')
        sprintf(parameterValueLabelPtr[idx], "%.2f", displayedValue);

    if (params.afterValueText[idx] != nullptr)
        strcat(parameterValueLabelPtr[idx], params.afterValueText[idx]);
}

void cSampleEditor::reloadParameterBarValue(uint8_t n)
{
    const auto& params = getCurrentEffectDisplayParams();
    if (n >= params.paramsNumber)
        return;

    float range = params.upConstrain[n] - params.downConstrain[n];
    if (range <= 0.0f)
    {
        paramsBarValue[n] = 0;
        return;
    }

    paramsBarValue[n] = (((effectDisplayParamsToAdjust[currentEffectIdx].parameter[n] -
                           params.downConstrain[n]) *
                          100) /
                         range);
}

//******************* REFRESH - RELOAD + DISPLAY
void cSampleEditor::refreshSpectrumPoints()
{
    if (screenType != mainScreen)
        return;
    reloadPointsData();
    showSpectrumPoints();
}

void cSampleEditor::refreshStartPoint()
{
    if (screenType != mainScreen)
        return;
    needRefreshPoints = 1;
    reloadStartPointText();
    showStartPointText();
}

void cSampleEditor::refreshEndPoint()
{
    if (screenType != mainScreen)
        return;
    needRefreshPoints = 1;
    reloadEndPointText();
    showEndPointText();
}

void cSampleEditor::refreshZoom()
{
    reloadZoomText();
    showZoomText();
}

void cSampleEditor::refreshEffectList()
{
    display.setControlValue(effectList, selectedEffectListIdx);
    showEffectList();

    if (SE->isParametersScreen())
    {
        display.setControlText(label[5], "Apply");
        display.setControlText2(label[5], "");
        display.setControlValue(label[5], 0);
    }
    else
    {
        display.setControlText(label[5], "Select");
        display.setControlText2(label[5], "effect");
        display.setControlValue(label[5], 1);
    }
    display.setControlColors(label[5],
                             canApplyCurrentEffect() ? display.getTheme().activeButtonLabelsColors
                                                     : display.getTheme().inactiveLabelsColors);
    display.refreshControl(label[5]);

    if (SE->isParametersScreen())
        strcpy(paramsScreenLabel, "Sample Editor");
    else
        strcpy(paramsScreenLabel, "Sample Editor 1/2");

    showTitleBar(paramsScreenLabel);

    display.synchronizeRefresh();
}

void cSampleEditor::refreshSpectrum()
{
    if (screenType != mainScreen)
        return;

    if (spectrumParams[0].address != nullptr && spectrumParams[0].length > 0)
        GP.processSpectrum(&spectrumParams[0], &zoom, &spectrumData[0]);
    else
    {
        for (uint16_t i = 0; i < 600; i++)
        {
            spectrumData[0].upperData[i] = 0;
            spectrumData[0].lowerData[i] = 0;
        }
    }

    showSpectrum();
}

void cSampleEditor::refreshPlayhead() {}

void cSampleEditor::refreshUndoState()
{
    if ((editorInstrument->isActive) && (screenType == mainScreen))
    {
        if (!currentEffect->undoParams.isEnable)
            display.setControlColors(label[3], display.getTheme().inactiveLabelsColors);
        else
            display.setControlColors(label[3], display.getTheme().activeLabelsColors);

        display.refreshControl(label[3]);
    }
}

void cSampleEditor::refreshParameter(uint8_t n)
{
    reloadParameterBarValue(n);
    reloadParameterValueText(n);

    showParameterBar(n);
    showParameterLabel(n);
}

void cSampleEditor::refreshApplyingProgress()
{
    reloadApplyingProgress();
    showProgressApplying();
}

void cSampleEditor::refreshProcessingProgress()
{
    reloadProcessingProgress();
    showProgressProcessing();
}

void cSampleEditor::refreshPlayingProgress() {}

//******************* PARAMETERS MODIFICATORS
void cSampleEditor::modStartPoint(int16_t val)
{
    uint16_t move_step = zoom.zoomWidth / 600;
    val *= move_step;

    if (selection.startPoint + val > MAX_16BIT)
        selection.startPoint = MAX_16BIT;
    else if (selection.startPoint + val < 0)
        selection.startPoint = 0;
    else
        selection.startPoint += val;

    if (selection.startPoint >= selection.endPoint)
        selection.startPoint = selection.endPoint - 1;

    bool isZoomActive = (zoom.zoomValue > 1);
    bool isNewPointChanged = zoom.lastChangedPoint != 1;
    bool isPointDisplayed =
      (selection.startPoint < zoom.zoomStart || selection.startPoint > zoom.zoomEnd);

    if (isZoomActive && (isNewPointChanged || isPointDisplayed))
        needRefreshSpectrum = 1;

    zoom.zoomPosition = selection.startPoint;
    zoom.lastChangedPoint = 1;

    currentEffect->changeSelectionRange(selection.startPoint, selection.endPoint);
    currentEffect->clearProcessedData();

    refreshStartPoint();
}

void cSampleEditor::modEndPoint(int16_t val)
{
    uint16_t move_step = zoom.zoomWidth / 600;
    val *= move_step;

    if (selection.endPoint + val > MAX_16BIT)
        selection.endPoint = MAX_16BIT;
    else if (selection.endPoint + val < 0)
        selection.endPoint = 0;
    else
        selection.endPoint += val;

    if (selection.startPoint >= selection.endPoint)
        selection.endPoint = selection.startPoint + 1;

    bool isZoomActive = (zoom.zoomValue > 1);
    bool isNewPointChanged = zoom.lastChangedPoint != 2;
    bool isPointDisplayed =
      (selection.endPoint < zoom.zoomStart || selection.endPoint > zoom.zoomEnd);

    if (isZoomActive && (isNewPointChanged || isPointDisplayed))
        needRefreshSpectrum = 1;

    zoom.zoomPosition = selection.endPoint;
    zoom.lastChangedPoint = 2;

    currentEffect->changeSelectionRange(selection.startPoint, selection.endPoint);
    currentEffect->clearProcessedData();

    refreshEndPoint();
}

void cSampleEditor::modZoom(int16_t val)
{
    GP.spectrumChangeZoom(val, editorInstrument->sample.length, &zoom);

    refreshZoom();
    needRefreshSpectrum = 1;
    needRefreshPoints = 1;
}

void cSampleEditor::resetZoom()
{
    if (zoom.zoomValue == 1.0f)
        return;

    GP.spectrumResetZoom(zoom.zoomPosition, editorInstrument->sample.length, &zoom);

    refreshZoom();
    needRefreshPoints = 1;
    needRefreshSpectrum = 1;
}

void cSampleEditor::modSelectedEffect(int16_t val)
{
    val = clamp<int>(val, -5, 5);

    uint16_t lastSelectedIdx = selectedEffectListIdx;

    if ((int32_t)selectedEffectListIdx + val > (int32_t)(editorEffectMax - 1))
        selectedEffectListIdx = editorEffectMax - 1;
    else if ((int32_t)selectedEffectListIdx + val < 0)
        selectedEffectListIdx = 0;
    else
        selectedEffectListIdx += val;

    if (selectedEffectListIdx != lastSelectedIdx)
    {
        currentEffectIdx = (uint8_t)selectedEffectListIdx;
        currentEffect = sampleEditorEffect[currentEffectIdx];
        reloadCurrentEffect();

        currentEffect->clearProcessedData();
        currentEffect->setBaseSampleLength(editorInstrument->sample.length);
        currentEffect->changeSelectionRange(selection.startPoint, selection.endPoint);

        for (uint8_t idx = 0; idx < getCurrentEffectDisplayParams().paramsNumber; idx++)
            currentEffect->setParameter(
              effectDisplayParamsToAdjust[currentEffectIdx].parameter[idx], idx);

        refreshEffectList();
        previewState = previewStatePreview;
    }
}

void cSampleEditor::setDefaultStartPoint()
{
    selection.startPoint = 0;

    if (selection.startPoint >= selection.endPoint)
        selection.startPoint = selection.endPoint - 1;

    bool isZoomActive = (zoom.zoomValue > 1);
    bool isNewPointChanged = zoom.lastChangedPoint != 1;
    bool isPointDisplayed =
      (selection.startPoint < zoom.zoomStart || selection.startPoint > zoom.zoomEnd);

    if (isZoomActive && (isNewPointChanged || isPointDisplayed))
        needRefreshSpectrum = 1;

    zoom.zoomPosition = selection.startPoint;
    zoom.lastChangedPoint = 1;

    currentEffect->changeSelectionRange(selection.startPoint, selection.endPoint);
    currentEffect->clearProcessedData();

    refreshStartPoint();
}

void cSampleEditor::setDefaultEndPoint()
{
    selection.endPoint = MAX_16BIT;

    if (selection.startPoint >= selection.endPoint)
        selection.endPoint = selection.startPoint + 1;

    bool isZoomActive = (zoom.zoomValue > 1);
    bool isNewPointChanged = zoom.lastChangedPoint != 2;
    bool isPointDisplayed =
      (selection.endPoint < zoom.zoomStart || selection.endPoint > zoom.zoomEnd);

    if (isZoomActive && (isNewPointChanged || isPointDisplayed))
        needRefreshSpectrum = 1;

    zoom.zoomPosition = selection.endPoint;
    zoom.lastChangedPoint = 2;

    currentEffect->changeSelectionRange(selection.startPoint, selection.endPoint);
    currentEffect->clearProcessedData();

    refreshEndPoint();
}

void cSampleEditor::setDefaultZoom()
{
    resetZoom();
}

// ParameterScreen
void cSampleEditor::modParameter(int16_t value, uint8_t idx)
{
    const auto& params = getCurrentEffectDisplayParams();
    float localParam = effectDisplayParamsToAdjust[currentEffectIdx].parameter[idx];
    float localVal = (value * params.changeStep[idx]);

    effectDisplayParamsToAdjust[currentEffectIdx].parameter[idx] =
      clamp<float>(localParam + localVal, params.downConstrain[idx], params.upConstrain[idx]);

    currentEffect->setParameter(effectDisplayParamsToAdjust[currentEffectIdx].parameter[idx], idx);

    setPreviewFunction();
    currentEffect->clearProcessedData();
    refreshParameter(idx);
}

void cSampleEditor::setDefaultParameter(uint8_t idx)
{
    effectDisplayParamsToAdjust[currentEffectIdx].parameter[idx] =
      effectDefaultParamsPtr[currentEffectIdx].parameter[idx];
    currentEffect->setParameter(effectDisplayParamsToAdjust[currentEffectIdx].parameter[idx], idx);

    setPreviewFunction();
    currentEffect->clearProcessedData();
    refreshParameter(idx);
}

int cSampleEditor::getSelectedParameterIdx()
{
    return effectScreenPage * kEffectPageParams + selectedPlace[enScreenType::effectScreen];
}

int cSampleEditor::getParamsPagesCount()
{
    if (getCurrentEffectDisplayParams().paramsNumber == 0)
        return 0;

    return ((getCurrentEffectDisplayParams().paramsNumber - 1) / kEffectPageParams) + 1;
}

int cSampleEditor::getCurrentPageParams()
{
    return clamp((int)getCurrentEffectDisplayParams().paramsNumber -
                   (int)effectScreenPage * kEffectPageParams,
                 0,
                 kEffectPageParams);
}

bool cSampleEditor::areThereMoreThanOneParamsPage()
{
    return getParamsPagesCount() > 1;
}

void cSampleEditor::setPreviewFunction()
{
    ip->clearButton(interfaceButton6);
    ip->setButtonObj(interfaceButton6, buttonPress, functPreview);

    previewState = previewStatePreview;

    showPreviewLabel();
}

void cSampleEditor::setPlayFunction()
{
    ip->clearButton(interfaceButton6);
    ip->setButtonObj(interfaceButton6, buttonPress, functPlay);

    previewState = previewStatePlay;

    showPlayLabel();
}

void cSampleEditor::setStopFunction()
{
    ip->clearButton(interfaceButton6);
    ip->setButtonObj(interfaceButton6, buttonPress, functStop);

    previewState = previewStateStop;

    showStopLabel();
}

void cSampleEditor::cancelGlobalPopups() {}

float cSampleEditor::samplePosToTime(uint32_t pos)
{
    return pos / kC3Rate;
}

const strEffectDisplayConstantParams& cSampleEditor::getCurrentEffectDisplayParams() const
{
    return effectDisplayConstantParams[currentEffectIdx];
}

bool cSampleEditor::isParameterList([[maybe_unused]] uint8_t paramIdx) const
{
    return false;
}

uint8_t cSampleEditor::getParameterListCount([[maybe_unused]] uint8_t paramIdx) const
{
    return 0;
}

char** cSampleEditor::getParameterListItems([[maybe_unused]] uint8_t paramIdx) const
{
    return nullptr;
}

//************************************************************************** ACTION FUNCTIONS
static void functSwitchModule(uint8_t button)
{
    if (button == interfaceButtonSampleEdit)
    {
        SE->restoreCurrentScreenFunctions();
        return;
    }
    stopAudition();
    SE->eventFunct(eventSwitchModule, SE, &button, 0);
}

static void functPads(uint8_t pad, uint8_t state, [[maybe_unused]] int16_t velo)
{
    if (SE->screenType != cSampleEditor::enScreenType::mainScreen)
        return;

    // same note map as the pattern screen: pad 13 is C-3, the upper pads fold down
    int note = 12 + pad;
    if (pad >= 48)
        return;
    if (note >= 36)
        note -= 36;

    if (state == buttonPress)
        SE->noteOnHandle(0, (uint8_t)note, 100, pad);
    else if (state == buttonRelease)
        SE->noteOffHandle(0, (uint8_t)note, 0, pad);
}

static void functMainScreenLeft()
{
    if (!SE->editorInstrument->isActive)
        return;

    if (SE->selectedPlace[cSampleEditor::mainScreen] > 0)
        SE->selectedPlace[cSampleEditor::mainScreen]--;

    SE->refreshSpectrumPoints();
    SE->showFrame();
}

static void functMainScreenRight()
{
    if (!SE->editorInstrument->isActive)
        return;

    if (SE->selectedPlace[cSampleEditor::mainScreen] < (SE->frameData.placesCount - 1))
        SE->selectedPlace[cSampleEditor::mainScreen]++;

    SE->refreshSpectrumPoints();
    SE->showFrame();
}

static void functMainScreenUp()
{
    switch (SE->selectedPlace[cSampleEditor::mainScreen])
    {
        case cSampleEditor::startPointButton: SE->modStartPoint(1); break;
        case cSampleEditor::endPointButton: SE->modEndPoint(1); break;
        case cSampleEditor::zoomButton: SE->modZoom(1); break;
        case cSampleEditor::effectButton: SE->modSelectedEffect(-1); break;
        default: break;
    }
}

static void functMainScreenDown()
{
    switch (SE->selectedPlace[cSampleEditor::mainScreen])
    {
        case cSampleEditor::startPointButton: SE->modStartPoint(-1); break;
        case cSampleEditor::endPointButton: SE->modEndPoint(-1); break;
        case cSampleEditor::zoomButton: SE->modZoom(-1); break;
        case cSampleEditor::effectButton: SE->modSelectedEffect(1); break;
        default: break;
    }
}

static void functMainScreenEncoder(int16_t value)
{
    switch (SE->selectedPlace[cSampleEditor::mainScreen])
    {
        case cSampleEditor::startPointButton: SE->modStartPoint(value); break;
        case cSampleEditor::endPointButton: SE->modEndPoint(value); break;
        case cSampleEditor::zoomButton: SE->modZoom(value); break;
        case cSampleEditor::effectButton:
            SE->modSelectedEffect(interfaceGlobals.invertValForList ? -value : value);
            break;
        default: break;
    }
}

static void functMainScreenDelete()
{
    if (!SE->editorInstrument->isActive)
        return;

    switch (SE->selectedPlace[cSampleEditor::mainScreen])
    {
        case cSampleEditor::startPointButton: SE->setDefaultStartPoint(); break;
        case cSampleEditor::endPointButton: SE->setDefaultEndPoint(); break;
        case cSampleEditor::zoomButton: SE->setDefaultZoom(); break;
        default: break;
    }
}

static void functSelectStartPoint()
{
    SE->selectedPlace[cSampleEditor::mainScreen] = cSampleEditor::startPointButton;
    SE->refreshSpectrumPoints();
    SE->showFrame();
}

static void functSelectEndPoint()
{
    SE->selectedPlace[cSampleEditor::mainScreen] = cSampleEditor::endPointButton;
    SE->refreshSpectrumPoints();
    SE->showSpectrumPoints();
    SE->showFrame();
}

static void functSelectZoom()
{
    SE->selectedPlace[cSampleEditor::mainScreen] = cSampleEditor::zoomButton;
    SE->refreshSpectrumPoints();
    SE->showFrame();
}

static void functListUp()
{
    if (SE->selectedPlace[cSampleEditor::mainScreen] != cSampleEditor::effectButton)
    {
        SE->selectedPlace[cSampleEditor::mainScreen] = cSampleEditor::effectButton;
        SE->refreshSpectrumPoints();
        SE->showFrame();
    }
    else
    {
        SE->modSelectedEffect(-1);
    }
}

static void functListDown()
{
    if (SE->selectedPlace[cSampleEditor::mainScreen] != cSampleEditor::effectButton)
    {
        SE->selectedPlace[cSampleEditor::mainScreen] = cSampleEditor::effectButton;
        SE->refreshSpectrumPoints();
        SE->showFrame();
    }
    else
    {
        SE->modSelectedEffect(1);
    }
}

static void functUndo()
{
    if (!SE->currentEffect->undoParams.isEnable)
        return;

    SE->currentEffect->undo();
    SE->refreshUndoState();

    SE->reloadSpectrumData();
    commitToModule();

    SE->selection.startPoint = SE->currentEffect->getNewStartPoint();
    SE->selection.endPoint = SE->currentEffect->getNewEndPoint();

    SE->resetZoom();
    SE->refreshStartPoint();
    SE->refreshEndPoint();
    SE->needRefreshSpectrum = 1;
}

static void functApply()
{
    if (!SE->canApplyCurrentEffect())
        return;

    if (SE->isParametersScreen())
    {
        functSave();
        return;
    }

    SE->screenType = cSampleEditor::effectScreen;
    SE->switchScreen(SE->screenType);
}

// Parameter screen functions
static void functParamsScreenLeft()
{
    if (SE->selectedPlace[cSampleEditor::effectScreen] > 0)
        SE->selectedPlace[cSampleEditor::effectScreen]--;
    SE->showFrame();
}

static void functParamsScreenRight()
{
    if (SE->selectedPlace[cSampleEditor::effectScreen] < (SE->frameData.placesCount - 1))
        SE->selectedPlace[cSampleEditor::effectScreen]++;
    SE->showFrame();
}

static void functParamsScreenUp()
{
    SE->modParameter(1, SE->getSelectedParameterIdx());
}

static void functParamsScreenDown()
{
    SE->modParameter(-1, SE->getSelectedParameterIdx());
}

static void functParamsScreenEncoder(int16_t value)
{
    SE->modParameter(value, SE->getSelectedParameterIdx());
}

static void functParamsScreenDelete()
{
    SE->setDefaultParameter(SE->getSelectedParameterIdx());
}

static void functParamsScreenMore()
{
    if (!SE->areThereMoreThanOneParamsPage())
        return;

    SE->effectScreenPage = wrap(SE->effectScreenPage + 1, 0, SE->getParamsPagesCount() - 1);
    SE->selectedPlace[SE->enScreenType::effectScreen] = 0;

    SE->reloadFrameData(SE->enScreenType::effectScreen);
    SE->showEffectParamsScreen();
}

static void functSelectParameter(uint8_t button)
{
    if (button >= SE->getCurrentPageParams())
        return;

    SE->selectedPlace[cSampleEditor::effectScreen] = button;
    SE->showFrame();
}

// the result has to fit a ProTracker sample
static bool resultFits()
{
    const uint32_t processedLength = SE->currentEffect->getExpectedProcessedSampleLength();
    if (processedLength == 0)
        return false;

    if (processedLength > protracker::kMaxSampleBytes)
    {
        SE->showPopupTooLongProcessedSampleWindow();
        SE->setTooLongProcessedSampleFunctions();
        return false;
    }
    return true;
}

static void functPreview()
{
    if (!SE->canApplyCurrentEffect() || !resultFits())
        return;

    stopAudition();

    SE->processingSteps = 1;
    SE->showPopupProcessing();
    SE->processingInProgress = 1;
    SE->ip->clearAll();

    SE->processingProgress = 0;
    SE->showProgressProcessing();

    if (!SE->currentEffect->processSelection())
    {
        SE->processingInProgress = 0;
        SE->hideProgressPopup();
        SE->restoreFunctions();
    }
}

static void functPlay()
{
    const uint32_t length = SE->currentEffect->getSampleLengthToPreview();
    if (length < 2 || PT->stream == nullptr)
        return;

    const uint32_t n = toBytes(
      SE->currentEffect->getSampleAddresToPreview(mtEffect::Channel::kLeft), length, previewBuf8);
    PT->stream->requestPreviewData(previewBuf8, n, periodForNote(24), previewVolume()); // C-3

    SE->playingInProgress = 1;
    SE->playTimer = 0;
    SE->setStopFunction();
}

static void functStop()
{
    stopAudition();
    SE->playingInProgress = 0;

    SE->restoreFunctions();
    SE->setPlayFunction();
}

static void functBack()
{
    stopAudition();
    SE->screenType = cSampleEditor::mainScreen;
    SE->switchScreen(SE->screenType);
}

static void functSave()
{
    if (!SE->canApplyCurrentEffect() || !resultFits())
        return;

    stopAudition();

    SE->applyingSteps = SE->currentEffect->isDataProcessed() ? 0 : 1;

    if (SE->applyingSteps)
    {
        SE->showPopupApplying();
        SE->applyingInProgress = 1;
        SE->applyingProgress = 0;
        SE->showProgressApplying();
        SE->ip->clearAll();
    }

    SE->currentEffect->apply();

    if (SE->applyingSteps && !SE->currentEffect->getProcessingSelectionState())
    {
        // the processing could not start
        SE->applyingInProgress = 0;
        SE->applyingProgress = 0;
        SE->hideProgressPopup();
        SE->restoreFunctions();
        return;
    }

    if (!SE->applyingSteps)
    {
        // the result of an earlier Preview was swapped in
        SE->confirmedDataIsChanged = 1;
        SE->reloadSpectrumData();
        commitToModule();

        SE->selection.startPoint = SE->currentEffect->getNewStartPoint();
        SE->selection.endPoint = SE->currentEffect->getNewEndPoint();
        SE->refreshStartPoint();
        SE->refreshEndPoint();
        SE->needRefreshSpectrum = 1;

        SE->resetZoom();

        SE->screenType = cSampleEditor::mainScreen;
        SE->switchScreen(SE->screenType);
        SE->setCommonFunctions();

        SE->previewState = cSampleEditor::previewStatePreview;
        SE->currentEffect->clearProcessedData();
        SE->refreshUndoState();
    }
}

// Too long processed sample
static void functTooLongProcessedSampleOk()
{
    SE->hideStaticPopup();
    SE->switchScreen(SE->screenType);
    SE->setCommonFunctions();
}
