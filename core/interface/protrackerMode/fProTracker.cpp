#ifdef PLATFORM_IMX

#include "protrackerMode/protrackerMode.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "Platform.h"
#include "cr_section_macros.h"
#include "mtAudioEngine.h"
#include "mtPadBoard.h"
#include "mtPadsBacklight.h"
#include "mtSequencer.h"

#include "core/interfacePopups.h"
#include "core/songTimer.h"

#include "ptTables.h"

cProTrackerMode protrackerMode;
static cProTrackerMode* PT = &protrackerMode;

// ---------------------------------------------------------------------------------------------
// memory
// ---------------------------------------------------------------------------------------------
__NOINIT(EXTERNAL_RAM) static uint8_t s_pool[PROTRACKER_POOL_SIZE];
static_assert(PROTRACKER_POOL_SIZE >= protracker::kPoolBytes,
              "PROTRACKER_POOL_SIZE (mtStructs.h) is too small for a ProTracker module");

constexpr int kTracks = protracker::kChannelCount;
constexpr int kRows = protracker::kRowCount;

// undo and redo keep copies of a whole pattern
struct UndoStack
{
    uint8_t count = 0;
    uint8_t pattern[cProTrackerMode::kUndoDepth];
};
static UndoStack s_undoMeta[2]; // 0 undo, 1 redo
__NOINIT(EXTERNAL_RAM) static uint8_t s_undoData[2][cProTrackerMode::kUndoDepth][protracker::kPatternBytes];

__NOINIT(EXTERNAL_RAM) static protracker::Note s_clip[kTracks * kRows];
static int s_clipTracks = 0;
static int s_clipRows = 0;
__NOINIT(EXTERNAL_RAM) static uint8_t s_patternClip[protracker::kPatternBytes];
static bool s_hasPatternClip = false;

__NOINIT(EXTERNAL_RAM) static char s_list[(cProTrackerMode::kVisibleFileRows + 1) *
                                          (cProTrackerMode::kFileNameLen + 8)];
__NOINIT(EXTERNAL_RAM) static char s_fileNames[cProTrackerMode::kMaxFiles * cProTrackerMode::kFileNameLen];
static char s_info[160];
static char s_title[48];
static char s_right[48];
static char s_text2[8][16];
static const char* kSongFolder = "ProTracker";

constexpr uint32_t kNotePopupDelayMs = 500;
constexpr uint32_t kInstrPopupDelayMs = 180;

// ---------------------------------------------------------------------------------------------
// input handlers
// ---------------------------------------------------------------------------------------------
static void functSwitchModule(uint8_t button);
static void functPattern(uint8_t state);
static void functPlayAction();
static void functRecAction();
static void functNote(uint8_t state);
static void functInstr(uint8_t state);
static void functFx(uint8_t state);
static void functShift(uint8_t state);
static void functUp();
static void functDown();
static void functLeft();
static void functRight();
static void functCopy();
static void functDelete();
static void functInsert();
static void functFile();
static void functEnter();
static void functEncoder(int16_t value);
static void functActionButton(uint8_t button, uint8_t state);
static void functMuteTrack(uint8_t button, uint8_t state);
static void functPads(uint8_t pad, uint8_t state, int16_t velo);

// ---------------------------------------------------------------------------------------------
// formatting
// ---------------------------------------------------------------------------------------------
static const char kHexDigits[] = "0123456789ABCDEF";

static int noteFromPeriod(uint16_t period)
{
    if (period == 0)
        return -1;

    int best = 0;
    int bestDiff = 1 << 30;
    for (int i = 0; i < 36; i++)
    {
        int diff = protracker::kPeriodTable[i] - (int)period;
        if (diff < 0)
            diff = -diff;
        if (diff < bestDiff)
        {
            bestDiff = diff;
            best = i;
        }
    }
    return best;
}

static void formatNote(char* out, uint16_t period)
{
    static const char names[12][3] = { "C-", "C#", "D-", "D#", "E-", "F-",
                                       "F#", "G-", "G#", "A-", "A#", "B-" };
    const int note = noteFromPeriod(period);
    if (note < 0)
    {
        out[0] = out[1] = out[2] = '-';
        out[3] = 0;
        return;
    }

    out[0] = names[note % 12][0];
    out[1] = names[note % 12][1];
    out[2] = (char)('1' + note / 12);
    out[3] = 0;
}

// Pad number -> semitone above the lowest pad, with the same pad layout as the stock firmware.
static int padSemitone(uint8_t pad)
{
    if (pad >= 48)
        return -1;
    const int lowest = mtPadBoard.getNoteFromPad(36); // the pad that holds the lowest note
    return (int)mtPadBoard.getNoteFromPad((int8_t)pad) - lowest;
}

static int padForSemitone(int semitone)
{
    for (int pad = 0; pad < 48; pad++)
    {
        if (padSemitone((uint8_t)pad) == semitone)
            return pad;
    }
    return 36;
}

static bool isButtonDown(int button)
{
    return PT->isShiftPressed() ? (button == interfaceButtonShift) : false;
}

// ---------------------------------------------------------------------------------------------
// SD card glue
// ---------------------------------------------------------------------------------------------
namespace
{
struct FsContext
{
    FileSystem* fs;
    FileSystem::FileHandle handle;
};

uint32_t ioRead(void* ctx, void* dst, uint32_t count)
{
    FsContext* c = (FsContext*)ctx;
    uint32_t total = 0;
    while (total < count)
    {
        int32_t got = c->fs->readFile((uint8_t*)dst + total, count - total, c->handle);
        if (got <= 0)
            break;
        total += (uint32_t)got;
    }
    return total;
}

bool ioWrite(void* ctx, const void* src, uint32_t count)
{
    FsContext* c = (FsContext*)ctx;
    return c->fs->writeFile(src, count, c->handle) == (int32_t)count;
}

bool ioSeek(void* ctx, uint32_t offset)
{
    FsContext* c = (FsContext*)ctx;
    return c->fs->seekFile(offset, c->handle);
}

bool isModuleFileName(const char* name)
{
    const size_t len = strlen(name);
    if (len > 4 && strcasecmp(name + len - 4, ".mod") == 0)
        return true;
    return len > 4 && strncasecmp(name, "mod.", 4) == 0; // Amiga style "mod.songname"
}
} // namespace

// ---------------------------------------------------------------------------------------------
// hooks for the stock popups
// ---------------------------------------------------------------------------------------------
const char* protrackerSampleName(int index)
{
    if (index < 0 || index >= protracker::kSampleCount)
        return "";
    return protrackerMode.module.samples[index].name;
}

// A pad pressed while the Note or Instr popup is open.
void protrackerPopupPad(uint8_t pad, uint8_t pressed)
{
    if (!pressed)
    {
        PT->stopPreview();
        return;
    }

    if (mtPopups.getStepPopupState() == stepPopupNote)
    {
        const int semitone = padSemitone(pad);
        if (semitone >= 0)
            PT->previewNote(PT->octave * 12 + semitone);
    }
    else if (mtPopups.getStepPopupState() == stepPopupInstr)
    {
        PT->previewPeriod(pad + 1, protracker::kPeriodTable[12]); // C-2
    }
}

// ---------------------------------------------------------------------------------------------
// module life cycle
// ---------------------------------------------------------------------------------------------
bool cProTrackerMode::isFxHeld()
{
    return platform->getButtons()->isButtonPressed(interfaceButtonFx1) ||
           platform->getButtons()->isButtonPressed(interfaceButtonFx2);
}

bool cProTrackerMode::isShiftPressed()
{
    return platform->getButtons()->isButtonPressed(interfaceButtonShift);
}

protracker::Note* cProTrackerMode::cell(int track, int row)
{
    return &module.pattern(currentPattern())[row * kTracks + track];
}

void cProTrackerMode::start(uint32_t options)
{
    (void)options;
    moduleRefresh = 1;

    if (!initialised)
    {
        module.attach(s_pool, sizeof(s_pool));
        replayer.init(&module, AUDIO_SAMPLE_RATE_EXACT);
        stream = engine.getAudioOutput()->getProTrackerStream();
        stream->attach(&replayer);
        trackerPattern.init();
        trackerPattern.patternLength = kRows;
        trackerPattern.selectState = 1;
        trackerPattern.showPlayhead = true;
        initialised = true;
    }

    // the sample sequencer and this mode don't share the output
    sequencer.stop();
    songTimer.hide();

    stream->setEnabled(true);

    mode = kModeEdit;
    page = 0;
    selectedPlace = -1;
    patternHeld = false;
    shiftMuteView = false;
    popupActive = false;
    stepButtonsState[0] = stepButtonsState[1] = 0;
    trackerPattern.popupMode = 0;
    isSelectingNow = false;
    trackerPattern.selectState = 1;
    refreshTimer = 0;

    bindButtons();
    showDefaultScreen();
}

void cProTrackerMode::stop()
{
    moduleRefresh = 0;

    if (stream != nullptr)
    {
        stream->requestPreviewStop();
        stream->requestStop();
        stream->setEnabled(false);
    }

    mtPopups.hideStepPopups();
    popupActive = false;

    display.setControlHide(listBox);
    display.setControlHide(infoLabel);
    display.setControlHide(frameControl);
    padsBacklight.clearGlobalPads(1, 1, 1);
    songTimer.show();
}

void cProTrackerMode::update()
{
    if (!initialised)
        return;

    // popups open when Note or Instr is held for a moment
    for (uint8_t i = 0; i < 2; i++)
    {
        if (!popupActive && stepButtonsState[i] == 1 &&
            stepButtonsTimer[i] > (i == 0 ? kNotePopupDelayMs : kInstrPopupDelayMs))
        {
            if (i == 0)
                showNotePopup();
            else
                showInstrumentPopup();
        }
    }

    if (refreshTimer < 20 || mode != kModeEdit)
        return;
    refreshTimer = 0;

    const bool playing = replayer.isPlaying();
    static bool wasPlaying = false;

    bool changed = false;
    if (playing)
    {
        const int row = replayer.row();
        if (row != lastRow)
        {
            lastRow = row;
            trackerPattern.playheadPosition = (int16_t)row;
            if (!editMode)
            {
                trackerPattern.actualStep = (int16_t)row;
                trackerPattern.selectState = 1;
            }
            changed = true;
        }
    }
    else if (wasPlaying)
    {
        lastRow = -1;
        trackerPattern.playheadPosition = 0;
        changed = true;
    }
    wasPlaying = playing;

    if (changed)
    {
        refreshPattern();
        refreshLabels();
    }
}

// ---------------------------------------------------------------------------------------------
// screen
// ---------------------------------------------------------------------------------------------
void cProTrackerMode::showDefaultScreen()
{
    titleBar->show(true);

    display.setControlHide(listBox);
    display.setControlHide(infoLabel);
    display.setControlShow(patternControl);
    display.refreshControl(patternControl);

    display.setControlShow(bgLabel);
    display.refreshControl(bgLabel);

    refreshEditState();
    activateLabelsBorder();
    display.synchronizeRefresh();
}

void cProTrackerMode::refreshTitle()
{
    snprintf(s_title, sizeof(s_title), "%s%s  Pos %02d  Pattern %02d",
             module.title[0] != '\0' ? module.title : "untitled",
             dirty ? "*" : "",
             editPos,
             currentPattern());
    titleBar->setTitle(s_title);

    const protracker::Sample& s = module.samples[sampleIndex()];
    snprintf(s_right, sizeof(s_right), "%02d. %s", sample, s.name);
    titleBar->setRightLabel(s_right);
}

void cProTrackerMode::showMessage(const char* message)
{
    snprintf(s_info, sizeof(s_info), "%s", message);
    display.setControlText(infoLabel, s_info);
    display.setControlShow(infoLabel);
    display.refreshControl(infoLabel);
    display.synchronizeRefresh();
}

void cProTrackerMode::refreshEditState()
{
    trackerPattern.selectColor = editMode ? 0 : 1; // red when writing, green otherwise
    refreshLabels();
    refreshPattern();
    refreshTitle();
}

void cProTrackerMode::refreshPattern()
{
    if (mode == kModeFileList)
    {
        int first = fileSelected - kVisibleFileRows / 2;
        if (first > fileCount - kVisibleFileRows)
            first = fileCount - kVisibleFileRows;
        if (first < 0)
            first = 0;

        int n = 0;
        s_list[0] = '\0';
        if (fileCount == 0)
            n += snprintf(s_list, sizeof(s_list), "No .mod files found in /%s on the SD card", kSongFolder);

        for (int i = first; i < fileCount && i < first + kVisibleFileRows; i++)
        {
            n += snprintf(s_list + n,
                          sizeof(s_list) - (size_t)n,
                          "%s %s\n",
                          i == fileSelected ? ">" : " ",
                          fileList[i]);
            if (n >= (int)sizeof(s_list) - 4)
                break;
        }

        display.setControlText(listBox, s_list);
        display.refreshControl(listBox);
        return;
    }

    trackerPattern.patternLength = kRows;
    trackerPattern.selectedParam = editParam > kParamFx1 ? kParamFx1 : editParam;
    trackerPattern.middleVisibleStep = trackerPattern.actualStep;

    for (int track = 0; track < kTracks; track++)
    {
        trackerPattern.inactive[track] = replayer.isMuted(track) ? 1 : 0;

        for (int r = 0; r < 15; r++)
        {
            strTrackerPattern::strTracks::strRow& row = trackerPattern.track[track].row[r];
            const int step = trackerPattern.actualStep - 7 + r;

            if (step < 0 || step >= kRows)
            {
                memset(&row, 0, sizeof(row));
                continue;
            }

            const protracker::Note& n = *cell(track, step);

            formatNote(row.note, n.period());

            if (n.sample() == 0)
            {
                row.instr[0] = row.instr[1] = '-';
            }
            else
            {
                row.instr[0] = (char)('0' + n.sample() / 10);
                row.instr[1] = (char)('0' + n.sample() % 10);
            }
            row.instr[2] = row.instr[3] = 0;

            if (n.command() == 0 && n.param() == 0)
            {
                row.fx[0][0] = row.fx[0][1] = row.fx[0][2] = row.fx[0][3] = '-';
            }
            else
            {
                row.fx[0][0] = kHexDigits[n.command()];
                row.fx[0][1] = ' ';
                row.fx[0][2] = kHexDigits[n.param() >> 4];
                row.fx[0][3] = kHexDigits[n.param() & 0xF];
            }
            row.fx[0][4] = 0;
            memset(row.fx[1], 0, sizeof(row.fx[1]));
        }
    }

    display.refreshControl(patternControl);
}

void cProTrackerMode::refreshHeldValues()
{
    snprintf(s_text2[0], sizeof(s_text2[0]), "%02d", currentPattern());
    snprintf(s_text2[1], sizeof(s_text2[1]), "%02d/%02d", editPos, module.songLength);
    snprintf(s_text2[2], sizeof(s_text2[2]), "%d", editStep);
}

void cProTrackerMode::refreshLabels()
{
    static const char* const kPage0[8] = { "Pattern", "Position", "Step jump", "",
                                           "Preview", "Undo",     "Redo",      "More" };
    static const char* const kPage1[8] = { "", "", "Duplicate", "", "", "Copy", "Paste", "More" };
    static const char* const kPage1b[8] = { "", "", "Pattern", "", "", "Pattern", "Pattern", "" };
    static const char* const kFiles[8] = { "Cancel", "Save", "New", "", "", "", "", "Load" };

    if (shiftMuteView)
        return;

    refreshHeldValues();

    for (uint8_t i = 0; i < 8; i++)
    {
        const char* text = "";
        const char* text2 = "";
        const uint32_t* colors = display.getTheme().activeLabelsColors;
        strLabelData* data = &labelDataGreen;
        int value = 1;

        if (mode == kModeFileList)
        {
            text = kFiles[i];
            data = &labelDataGreen;
            value = 0;
        }
        else if (page == 0)
        {
            text = kPage0[i];
            if (i < 3)
                text2 = s_text2[i];

            if (i == 3)
            {
                colors = display.getTheme().inactiveLabelsColors;
                data = &labelDataRed;
                value = 0;
            }
            else if (i == 5 || i == 6)
            {
                colors = editMode ? display.getTheme().activeButtonLabelsColors
                                  : display.getTheme().inactiveButtonLabelsColors;
                data = &labelDataRed;
                value = 0;
            }
            else if (i == 4)
            {
                value = 0;
            }
            else if (i == 7)
            {
                colors = display.getTheme().moreButtonColors;
                data = &labelDataMore;
            }
        }
        else
        {
            text = kPage1[i];
            text2 = kPage1b[i];
            colors = editMode ? display.getTheme().activeButtonLabelsColors
                              : display.getTheme().inactiveButtonLabelsColors;
            data = &labelDataRed;
            if (i == 7)
            {
                colors = display.getTheme().moreButtonColors;
                data = &labelDataMore;
            }
        }

        display.setControlText(label[i], text);
        display.setControlText2(label[i], text2);
        display.setControlColors(label[i], colors);
        display.setControlData(label[i], data);
        display.setControlValue(label[i], text[0] == '\0' ? 0 : value);
        display.setControlStyle(label[i],
                                controlStyleCenterX | controlStyleCenterY | controlStyleFont3 |
                                  controlStyleShow |
                                  (i == 7 ? controlStyleSpecialFx2
                                          : (display.getTheme().patternEditorBottomButtonsLines
                                               ? controlStyleSpecialFx
                                               : 0)));
        display.setControlStyle2(label[i],
                                 controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
        display.refreshControl(label[i]);
    }

    labelDataMore.dotIndicator.current = page;
    display.synchronizeRefresh();
}

void cProTrackerMode::activateLabelsBorder()
{
    if (selectedPlace < 0 || selectedPlace > frameData.placesCount - 1)
    {
        display.setControlHide(frameControl);
        display.refreshControl(frameControl);
        return;
    }

    display.setControlValue(frameControl, selectedPlace);
    display.setControlShow(frameControl);
    display.refreshControl(frameControl);
}

// Shift turns the first four bottom buttons into track mutes, like the stock pattern editor.
void cProTrackerMode::showMuteButtons(bool show)
{
    if (mode != kModeEdit)
        return;

    shiftMuteView = show;

    if (!show)
    {
        bindButtons();
        refreshLabels();
        return;
    }

    for (uint8_t i = 0; i < 8; i++)
    {
        static char names[4][10];
        const bool track = i < kTracks;
        if (track)
            snprintf(names[i], sizeof(names[i]), "Track %d", i + 1);

        display.setControlText(label[i], track ? names[i] : "");
        display.setControlText2(label[i], track ? (replayer.isMuted(i) ? "Unmute" : "Mute") : "");
        display.setControlColors(label[i], display.getTheme().activeLabelsColors);
        display.setControlValue(label[i], 0);
        display.refreshControl(label[i]);
        ip->setButtonObj(i, functMuteTrack);
    }
    display.synchronizeRefresh();
}

// ---------------------------------------------------------------------------------------------
// button bindings
// ---------------------------------------------------------------------------------------------
void cProTrackerMode::bindButtons()
{
    ip->clearAllButtons();
    ip->clearAllPots();
    ip->clearGlobalPads();

    for (uint8_t b = interfaceButton0; b <= interfaceButton7; b++)
        ip->setButtonObj(b, functActionButton);

    ip->setButtonObj(interfaceButtonPlay, buttonPress, functPlayAction);
    ip->setButtonObj(interfaceButtonRec, buttonPress, functRecAction);
    ip->setButtonObj(interfaceButtonNote, functNote);
    ip->setButtonObj(interfaceButtonInstr, functInstr);
    ip->setButtonObj(interfaceButtonFx1, functFx);
    ip->setButtonObj(interfaceButtonFx2, functFx);
    ip->setButtonObj(interfaceButtonPattern, functPattern);
    ip->setButtonObj(interfaceButtonShift, functShift);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDown);
    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functRight);
    ip->setButtonObj(interfaceButtonCopy, buttonPress, functCopy);
    ip->setButtonObj(interfaceButtonDelete, buttonPress, functDelete);
    ip->setButtonObj(interfaceButtonInsert, buttonPress, functInsert);
    ip->setButtonObj(interfaceButtonMiniMiddle, buttonPress, functEnter);
    ip->setButtonObj(interfaceButtonFile, buttonPress, functFile);

    ip->setPotObj(interfacePot0, functEncoder);
    ip->setPadsGlobal(functPads);

    // the other screens
    ip->setButtonObj(interfaceButtonConfig, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonMaster, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSamplePlay, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleEdit, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleRec, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleLoad, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSong, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonParams, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonPerformance, buttonPress, functSwitchModule);
}

// ---------------------------------------------------------------------------------------------
// cursor and selection
// ---------------------------------------------------------------------------------------------
void cProTrackerMode::moveStep(int delta)
{
    int step = trackerPattern.actualStep + delta;
    step %= kRows;
    if (step < 0)
        step += kRows;
    trackerPattern.actualStep = (int16_t)step;
}

void cProTrackerMode::moveTrack(int delta)
{
    int track = trackerPattern.actualTrack + delta;
    if (track < 0)
        track = kTracks - 1;
    if (track >= kTracks)
        track = 0;
    trackerPattern.actualTrack = (int8_t)track;
}

void cProTrackerMode::moveCursorByStep()
{
    if (editStep > 0)
        moveStep(editStep);
}

void cProTrackerMode::startSelection()
{
    trackerPattern.selectState = 2;
    trackerPattern.selectStartTrack = trackerPattern.selectEndTrack = trackerPattern.actualTrack;
    trackerPattern.selectStartStep = trackerPattern.selectEndStep = trackerPattern.actualStep;
    trackerPattern.selectColumn = 0;
    isSelectingNow = true;
}

void cProTrackerMode::updateSelection()
{
    trackerPattern.selectEndTrack = trackerPattern.actualTrack;
    trackerPattern.selectEndStep = trackerPattern.actualStep;
}

void cProTrackerMode::endSelection()
{
    isSelectingNow = false;
    trackerPattern.selectState = 1;
    trackerPattern.selectColumn = 0;
}

void cProTrackerMode::selectionRect(int& t0, int& r0, int& t1, int& r1)
{
    if (trackerPattern.selectState == 2)
    {
        t0 = trackerPattern.selectStartTrack;
        t1 = trackerPattern.selectEndTrack;
        r0 = trackerPattern.selectStartStep;
        r1 = trackerPattern.selectEndStep;
        if (t0 > t1)
            std::swap(t0, t1);
        if (r0 > r1)
            std::swap(r0, r1);
    }
    else
    {
        t0 = t1 = trackerPattern.actualTrack;
        r0 = r1 = trackerPattern.actualStep;
    }
}

// ---------------------------------------------------------------------------------------------
// undo
// ---------------------------------------------------------------------------------------------
static void pushSnapshot(int stack, int pattern)
{
    UndoStack& u = s_undoMeta[stack];
    if (u.count == cProTrackerMode::kUndoDepth)
    {
        memmove(&s_undoData[stack][0],
                &s_undoData[stack][1],
                (cProTrackerMode::kUndoDepth - 1) * protracker::kPatternBytes);
        memmove(&u.pattern[0], &u.pattern[1], cProTrackerMode::kUndoDepth - 1);
        u.count--;
    }
    memcpy(s_undoData[stack][u.count], PT->module.pattern(pattern), protracker::kPatternBytes);
    u.pattern[u.count] = (uint8_t)pattern;
    u.count++;
}

void cProTrackerMode::storeUndo()
{
    pushSnapshot(0, currentPattern());
    s_undoMeta[1].count = 0;
    dirty = true;
}

void cProTrackerMode::undo()
{
    UndoStack& u = s_undoMeta[0];
    if (u.count == 0)
        return;

    u.count--;
    const int pattern = u.pattern[u.count];
    pushSnapshot(1, pattern);
    memcpy(module.pattern(pattern), s_undoData[0][u.count], protracker::kPatternBytes);
    dirty = true;
}

void cProTrackerMode::redo()
{
    UndoStack& r = s_undoMeta[1];
    if (r.count == 0)
        return;

    r.count--;
    const int pattern = r.pattern[r.count];
    pushSnapshot(0, pattern);
    memcpy(module.pattern(pattern), s_undoData[1][r.count], protracker::kPatternBytes);
    dirty = true;
}

// ---------------------------------------------------------------------------------------------
// editing
// ---------------------------------------------------------------------------------------------
int cProTrackerMode::writeNote(int semitone)
{
    const int note = octave * 12 + semitone;
    if (note < 0 || note >= 36)
        return -1;

    storeUndo();
    int t0, r0, t1, r1;
    selectionRect(t0, r0, t1, r1);
    for (int t = t0; t <= t1; t++)
    {
        for (int r = r0; r <= r1; r++)
        {
            protracker::Note* n = cell(t, r);
            n->set((uint8_t)sample, (uint16_t)protracker::kPeriodTable[note], n->command(), n->param());
        }
    }
    return note;
}

void cProTrackerMode::writeInstrument(int instrument)
{
    if (instrument < 0 || instrument > protracker::kSampleCount)
        return;

    storeUndo();
    int t0, r0, t1, r1;
    selectionRect(t0, r0, t1, r1);
    for (int t = t0; t <= t1; t++)
    {
        for (int r = r0; r <= r1; r++)
        {
            protracker::Note* n = cell(t, r);
            n->set((uint8_t)instrument, n->period(), n->command(), n->param());
        }
    }
}

void cProTrackerMode::writeFxValue(int value)
{
    storeUndo();
    int t0, r0, t1, r1;
    selectionRect(t0, r0, t1, r1);
    for (int t = t0; t <= t1; t++)
    {
        for (int r = r0; r <= r1; r++)
        {
            protracker::Note* n = cell(t, r);
            n->set(n->sample(), n->period(), n->command(), (uint8_t)value);
        }
    }
}

void cProTrackerMode::writeFxType(int type)
{
    storeUndo();
    int t0, r0, t1, r1;
    selectionRect(t0, r0, t1, r1);
    for (int t = t0; t <= t1; t++)
    {
        for (int r = r0; r <= r1; r++)
        {
            protracker::Note* n = cell(t, r);
            n->set(n->sample(), n->period(), (uint8_t)(type & 0xF), n->param());
        }
    }
}

// the encoder in red mode: change the value under the cursor
void cProTrackerMode::changeValue(int delta)
{
    if (delta == 0)
        return;

    storeUndo();
    int t0, r0, t1, r1;
    selectionRect(t0, r0, t1, r1);

    const bool fxType = platform->getButtons()->isButtonPressed(interfaceButtonFx1) ||
                        platform->getButtons()->isButtonPressed(interfaceButtonFx2);

    for (int t = t0; t <= t1; t++)
    {
        for (int r = r0; r <= r1; r++)
        {
            protracker::Note* n = cell(t, r);

            if (editParam == kParamNote)
            {
                int step = delta;
                if (step == 10 || step == -10)
                    step = step > 0 ? 12 : -12; // a coarse step is an octave
                int note = noteFromPeriod(n->period());
                note = note < 0 ? 12 : note + step;
                if (note < 0)
                    note = 0;
                if (note > 35)
                    note = 35;
                const uint8_t s = n->sample() != 0 ? n->sample() : (uint8_t)sample;
                n->set(s, (uint16_t)protracker::kPeriodTable[note], n->command(), n->param());
            }
            else if (editParam == kParamInstr)
            {
                int s = (n->sample() != 0 ? n->sample() : sample) + delta;
                if (s < 1)
                    s = 1;
                if (s > protracker::kSampleCount)
                    s = protracker::kSampleCount;
                n->set((uint8_t)s, n->period(), n->command(), n->param());
                sample = s;
            }
            else if (fxType)
            {
                int type = n->command() + (delta > 0 ? 1 : -1);
                if (type < 0)
                    type = 0;
                if (type > 15)
                    type = 15;
                n->set(n->sample(), n->period(), (uint8_t)type, n->param());
            }
            else
            {
                int value = n->param() + delta;
                if (value < 0)
                    value = 0;
                if (value > 255)
                    value = 255;
                n->set(n->sample(), n->period(), n->command(), (uint8_t)value);
            }
        }
    }
}

void cProTrackerMode::clearSelection()
{
    storeUndo();
    int t0, r0, t1, r1;
    selectionRect(t0, r0, t1, r1);
    for (int t = t0; t <= t1; t++)
    {
        for (int r = r0; r <= r1; r++)
        {
            protracker::Note* n = cell(t, r);
            if (editParam == kParamNote)
                n->set(0, 0, n->command(), n->param());
            else if (editParam == kParamInstr)
                n->set(0, n->period(), n->command(), n->param());
            else
                n->set(n->sample(), n->period(), 0, 0);
        }
    }
}

void cProTrackerMode::backspace()
{
    storeUndo();
    int t0, r0, t1, r1;
    selectionRect(t0, r0, t1, r1);
    const int from = trackerPattern.actualStep;
    if (from <= 0)
        return;

    for (int t = t0; t <= t1; t++)
    {
        for (int r = from - 1; r < kRows - 1; r++)
            *cell(t, r) = *cell(t, r + 1);
        cell(t, kRows - 1)->set(0, 0, 0, 0);
    }
    moveStep(-1);
}

void cProTrackerMode::insertRow()
{
    storeUndo();
    int t0, r0, t1, r1;
    selectionRect(t0, r0, t1, r1);
    const int from = trackerPattern.actualStep;

    for (int t = t0; t <= t1; t++)
    {
        for (int r = kRows - 1; r > from; r--)
            *cell(t, r) = *cell(t, r - 1);
        cell(t, from)->set(0, 0, 0, 0);
    }
}

void cProTrackerMode::copySelection()
{
    int t0, r0, t1, r1;
    selectionRect(t0, r0, t1, r1);
    s_clipTracks = t1 - t0 + 1;
    s_clipRows = r1 - r0 + 1;
    for (int t = 0; t < s_clipTracks; t++)
    {
        for (int r = 0; r < s_clipRows; r++)
            s_clip[t * kRows + r] = *cell(t0 + t, r0 + r);
    }
}

void cProTrackerMode::pasteSelection()
{
    if (s_clipTracks == 0)
        return;

    storeUndo();
    const int t0 = trackerPattern.actualTrack;
    const int r0 = trackerPattern.actualStep;
    for (int t = 0; t < s_clipTracks && t0 + t < kTracks; t++)
    {
        for (int r = 0; r < s_clipRows && r0 + r < kRows; r++)
            *cell(t0 + t, r0 + r) = s_clip[t * kRows + r];
    }
    moveStep(s_clipRows);
}

void cProTrackerMode::copyPattern()
{
    memcpy(s_patternClip, module.pattern(currentPattern()), protracker::kPatternBytes);
    s_hasPatternClip = true;
}

void cProTrackerMode::pastePattern()
{
    if (!s_hasPatternClip)
        return;
    storeUndo();
    memcpy(module.pattern(currentPattern()), s_patternClip, protracker::kPatternBytes);
}

// A copy of the current pattern goes to the first pattern number that isn't in use yet.
void cProTrackerMode::duplicatePattern()
{
    int highest = 0;
    for (int i = 0; i < protracker::kOrderCount; i++)
    {
        if (module.order[i] > highest)
            highest = module.order[i];
    }
    if (highest + 1 >= protracker::kMaxPatterns)
        return;

    const int target = highest + 1;
    memcpy(module.pattern(target), module.pattern(currentPattern()), protracker::kPatternBytes);
    module.order[editPos] = (uint8_t)target;
    dirty = true;
}

// ---------------------------------------------------------------------------------------------
// held button parameters
// ---------------------------------------------------------------------------------------------
void cProTrackerMode::setPattern(int value)
{
    if (value < 0)
        value = 0;
    if (value >= protracker::kMaxPatterns)
        value = protracker::kMaxPatterns - 1;
    module.order[editPos] = (uint8_t)value;
    dirty = true;
}

void cProTrackerMode::changePattern(int delta)
{
    setPattern(currentPattern() + delta);
}

void cProTrackerMode::setPosition(int value)
{
    if (value < 0)
        value = 0;
    if (value >= module.songLength)
        value = module.songLength - 1;
    editPos = value;
}

void cProTrackerMode::changePosition(int delta)
{
    setPosition(editPos + delta);
}

void cProTrackerMode::setEditStep(int value)
{
    if (value < 0)
        value = 0;
    if (value > 16)
        value = 16;
    editStep = value;
}

void cProTrackerMode::changeEditStep(int delta)
{
    setEditStep(editStep + delta);
}

// ---------------------------------------------------------------------------------------------
// sound
// ---------------------------------------------------------------------------------------------
void cProTrackerMode::previewPeriod(int sampleNumber, int period)
{
    if (stream == nullptr || sampleNumber < 1 || sampleNumber > protracker::kSampleCount ||
        period <= 0 || replayer.isPlaying())
        return;

    stream->requestPreview(sampleNumber - 1, (uint16_t)period);
}

void cProTrackerMode::previewNote(int note)
{
    if (note < 0 || note >= 36)
        return;

    const int fineTune = module.samples[sampleIndex()].fineTune & 0x0F;
    previewPeriod(sample, protracker::kPeriodTable[fineTune * 37 + note]);
}

void cProTrackerMode::stopPreview()
{
    if (stream != nullptr)
        stream->requestPreviewStop();
}

void cProTrackerMode::togglePlay(bool fromCursor)
{
    if (replayer.isPlaying())
    {
        stream->requestStop();
        lastRow = -1;
        trackerPattern.playheadPosition = 0;
        return;
    }

    stopPreview();
    stream->requestPlayPattern(currentPattern(), fromCursor ? trackerPattern.actualStep : 0);
}

void cProTrackerMode::toggleMute(int track)
{
    replayer.setMuted(track, !replayer.isMuted(track));
}

// ---------------------------------------------------------------------------------------------
// popups
// ---------------------------------------------------------------------------------------------
void cProTrackerMode::setStepButtonState(uint8_t button, uint8_t state)
{
    if (state)
    {
        if (popupActive || isShiftPressed() || patternHeld || mode != kModeEdit)
            return;
        if (stepButtonsState[0] || stepButtonsState[1])
            return;

        stepButtonsState[button] = 1;
        stepButtonsTimer[button] = 0;
    }
    else
    {
        stepButtonsState[button] = 0;
    }
}

void cProTrackerMode::showNotePopup()
{
    popupActive = true;
    trackerPattern.popupMode |= 2;
    refreshPattern();

    const int note = noteFromPeriod(cell(trackerPattern.actualTrack, trackerPattern.actualStep)->period());
    mtPopups.showStepPopup(stepPopupNote, (int8_t)padForSemitone(note < 0 ? 12 : note - octave * 12));
}

void cProTrackerMode::showInstrumentPopup()
{
    popupActive = true;
    trackerPattern.popupMode |= 2;
    refreshPattern();

    const protracker::Note& n = *cell(trackerPattern.actualTrack, trackerPattern.actualStep);
    mtPopups.showStepPopup(stepPopupInstr, (int8_t)((n.sample() != 0 ? n.sample() : sample) - 1));
}

void cProTrackerMode::cancelPopups()
{
    mtPopups.hideStepPopups();
    popupActive = false;
    trackerPattern.popupMode = 0;
    stopPreview();
    bindButtons();
    refreshEditState();
    activateLabelsBorder();
}

// releasing Note or Instr closes the popup and uses the chosen value
void cProTrackerMode::applyPopup()
{
    const enumStepPopups state = mtPopups.getStepPopupState();
    const int value = mtPopups.getStepPopupValue();

    cancelPopups();

    if (state == stepPopupNote)
    {
        if (editMode)
        {
            const int semitone = padSemitone((uint8_t)value);
            if (semitone >= 0 && writeNote(semitone) >= 0)
                moveCursorByStep();
        }
    }
    else if (state == stepPopupInstr)
    {
        sample = value + 1;
        if (editMode)
            writeInstrument(sample);
    }

    refreshEditState();
}

// ---------------------------------------------------------------------------------------------
// files
// ---------------------------------------------------------------------------------------------
void cProTrackerMode::showFileList()
{
    FileSystem* fs = platform->getFileSystem();
    if (!fs->isSdCardInserted())
    {
        showMessage("NO SD CARD");
        return;
    }

    fs->mkdir(0, kSongFolder);

    fileCount = kMaxFiles;
    fs->listDirContents(kSongFolder,
                        FileSystem::kFiles,
                        isModuleFileName,
                        s_fileNames,
                        sizeof(s_fileNames),
                        kFileNameLen - 1,
                        fileCount,
                        fileList);
    fileSelected = 0;

    mode = kModeFileList;
    selectedPlace = -1;
    activateLabelsBorder();

    display.setControlHide(patternControl);
    display.setControlShow(listBox);

    refreshLabels();
    refreshPattern();
    snprintf(s_info, sizeof(s_info), "SONGS  /%s  (%d files)", kSongFolder, fileCount);
    display.setControlText(infoLabel, s_info);
    display.setControlShow(infoLabel);
    display.refreshControl(infoLabel);
    display.synchronizeRefresh();
}

void cProTrackerMode::hideFileList()
{
    mode = kModeEdit;
    bindButtons();
    showDefaultScreen();
}

void cProTrackerMode::moveFileSelection(int delta)
{
    if (fileCount == 0)
        return;

    fileSelected += delta;
    if (fileSelected < 0)
        fileSelected = 0;
    if (fileSelected >= fileCount)
        fileSelected = fileCount - 1;
    refreshPattern();
}

void cProTrackerMode::newSong()
{
    stream->pause();
    replayer.stop();
    module.clear();
    stream->resume();

    editPos = 0;
    trackerPattern.actualStep = 0;
    trackerPattern.actualTrack = 0;
    sample = 1;
    dirty = false;
    s_undoMeta[0].count = s_undoMeta[1].count = 0;
}

void cProTrackerMode::loadSelectedFile()
{
    if (fileCount == 0)
    {
        hideFileList();
        return;
    }

    FileSystem* fs = platform->getFileSystem();

    char path[96];
    snprintf(path, sizeof(path), "%s/%s", kSongFolder, fileList[fileSelected]);

    auto [ok, handle] = fs->openFile(path, FileSystem::kRead);
    if (!ok)
    {
        hideFileList();
        showMessage("CANNOT OPEN FILE");
        return;
    }

    FsContext ctx = { fs, handle };
    protracker::FileIo io;
    io.ctx = &ctx;
    io.read = ioRead;
    io.seek = ioSeek;

    stream->pause();
    replayer.stop();
    const protracker::Result result = module.load(io);
    if (result != protracker::Result::kOk)
        module.clear(); // never leave a half loaded song behind
    stream->resume();

    fs->closeFile(handle);

    editPos = 0;
    trackerPattern.actualStep = 0;
    trackerPattern.actualTrack = 0;
    sample = 1;
    dirty = false;
    s_undoMeta[0].count = s_undoMeta[1].count = 0;

    hideFileList();
    if (result != protracker::Result::kOk)
        showMessage(protracker::resultText(result));
}

void cProTrackerMode::saveSong()
{
    FileSystem* fs = platform->getFileSystem();
    if (!fs->isSdCardInserted())
    {
        showMessage("NO SD CARD");
        return;
    }

    fs->mkdir(0, kSongFolder);

    // lower case file name from the song title, never overwrite an existing file
    char base[24];
    int n = 0;
    for (int i = 0; i < 20 && module.title[i] != '\0' && n < (int)sizeof(base) - 1; i++)
    {
        char c = module.title[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        base[n++] = ok ? c : '_';
    }
    if (n == 0)
    {
        snprintf(base, sizeof(base), "untitled");
        n = 8;
    }
    base[n] = '\0';

    char path[96];
    snprintf(path, sizeof(path), "%s/%s.mod", kSongFolder, base);
    for (int i = 1; i <= 999 && fs->exists(path); i++)
        snprintf(path, sizeof(path), "%s/%s-%d.mod", kSongFolder, base, i);

    auto [ok, handle] = fs->openFile(path, FileSystem::kWriteCreateNew);
    if (!ok)
    {
        showMessage("CANNOT CREATE FILE");
        return;
    }

    FsContext ctx = { fs, handle };
    protracker::FileIo io;
    io.ctx = &ctx;
    io.write = ioWrite;

    const protracker::Result result = module.save(io);
    fs->closeFile(handle);

    if (result == protracker::Result::kOk)
    {
        dirty = false;
        char message[96];
        snprintf(message, sizeof(message), "SAVED %s", path);
        showMessage(message);
    }
    else
    {
        showMessage(protracker::resultText(result));
    }
}

// ---------------------------------------------------------------------------------------------
// input
// ---------------------------------------------------------------------------------------------
static void functSwitchModule(uint8_t button)
{
    PT->eventFunct(eventSwitchModule, PT, &button, 0);
}

static void functFile()
{
    if (PT->mode == cProTrackerMode::kModeEdit && !PT->popupActive)
        PT->showFileList();
}

// Pattern held: arrows and the encoder pick the pattern of the current position
static void functPattern(uint8_t state)
{
    if (PT->mode == cProTrackerMode::kModeFileList)
    {
        if (state == buttonPress)
            PT->hideFileList();
        return;
    }

    PT->patternHeld = (state == buttonPress);
    PT->refreshEditState();
}

static void functRecAction()
{
    if (PT->mode != cProTrackerMode::kModeEdit)
        return;

    PT->editMode = !PT->editMode;
    PT->refreshEditState();
}

static void functPlayAction()
{
    if (PT->mode != cProTrackerMode::kModeEdit)
        return;

    PT->togglePlay(PT->isShiftPressed());
    PT->refreshEditState();
}

static void functNote(uint8_t state)
{
    if (PT->mode != cProTrackerMode::kModeEdit)
        return;

    if (state == buttonPress)
    {
        PT->editParam = cProTrackerMode::kParamNote;
        PT->setStepButtonState(0, 1);
        PT->refreshEditState();
    }
    else
    {
        PT->setStepButtonState(0, 0);
        if (PT->popupActive)
            PT->applyPopup();
    }
}

static void functInstr(uint8_t state)
{
    if (PT->mode != cProTrackerMode::kModeEdit)
        return;

    if (state == buttonPress)
    {
        PT->editParam = cProTrackerMode::kParamInstr;
        PT->setStepButtonState(1, 1);
        PT->refreshEditState();
    }
    else
    {
        PT->setStepButtonState(1, 0);
        if (PT->popupActive)
            PT->applyPopup();
    }
}

// Fx1 and Fx2 both select the effect: a ProTracker step has one effect command with its value
static void functFx(uint8_t state)
{
    if (PT->mode != cProTrackerMode::kModeEdit || state != buttonPress)
        return;

    PT->editParam = cProTrackerMode::kParamFx1;
    PT->refreshEditState();
}

static void functShift(uint8_t state)
{
    if (PT->mode != cProTrackerMode::kModeEdit || PT->popupActive)
        return;

    if (state == buttonPress)
    {
        PT->showMuteButtons(true);
    }
    else
    {
        PT->endSelection();
        PT->showMuteButtons(false);
        PT->refreshPattern();
    }
}

static void functMuteTrack(uint8_t button, uint8_t state)
{
    if (state != buttonPress || button >= kTracks)
        return;

    PT->toggleMute(button);
    PT->showMuteButtons(true);
    PT->refreshPattern();
}

static void moveCursor(int dTrack, int dStep)
{
    // while the song plays in green mode the cursor follows the playhead
    if (PT->replayer.isPlaying() && !PT->editMode)
        return;

    const bool shift = PT->isShiftPressed();
    if (shift && !PT->isSelectingNow)
        PT->startSelection();
    else if (!shift && PT->isSelectingNow)
        PT->endSelection();
    else if (!shift && PT->trackerPattern.selectState == 2)
        PT->endSelection();

    if (dTrack != 0)
        PT->moveTrack(dTrack);
    if (dStep != 0)
        PT->moveStep(dStep);

    if (PT->isSelectingNow)
        PT->updateSelection();

    PT->refreshPattern();
}

static void functUp()
{
    if (PT->mode == cProTrackerMode::kModeFileList)
    {
        PT->moveFileSelection(-1);
        return;
    }

    if (PT->selectedPlace >= 0 || PT->patternHeld)
    {
        if (PT->patternHeld || PT->selectedPlace == 0)
            PT->changePattern(1);
        else if (PT->selectedPlace == 1)
            PT->changePosition(1);
        else if (PT->selectedPlace == 2)
            PT->changeEditStep(1);
        PT->refreshEditState();
        return;
    }

    moveCursor(0, -1);
}

static void functDown()
{
    if (PT->mode == cProTrackerMode::kModeFileList)
    {
        PT->moveFileSelection(1);
        return;
    }

    if (PT->selectedPlace >= 0 || PT->patternHeld)
    {
        if (PT->patternHeld || PT->selectedPlace == 0)
            PT->changePattern(-1);
        else if (PT->selectedPlace == 1)
            PT->changePosition(-1);
        else if (PT->selectedPlace == 2)
            PT->changeEditStep(-1);
        PT->refreshEditState();
        return;
    }

    moveCursor(0, 1);
}

static void functLeft()
{
    if (PT->mode != cProTrackerMode::kModeEdit)
        return;

    if (PT->selectedPlace >= 0 || PT->patternHeld)
    {
        functDown();
        return;
    }

    moveCursor(-1, 0);
}

static void functRight()
{
    if (PT->mode != cProTrackerMode::kModeEdit)
        return;

    if (PT->selectedPlace >= 0 || PT->patternHeld)
    {
        functUp();
        return;
    }

    moveCursor(1, 0);
}

static void functCopy()
{
    if (PT->mode != cProTrackerMode::kModeEdit || !PT->editMode)
        return;

    if (PT->isShiftPressed())
        PT->pasteSelection();
    else
        PT->copySelection();
    PT->refreshPattern();
}

static void functDelete()
{
    if (PT->mode != cProTrackerMode::kModeEdit || !PT->editMode)
        return;

    if (PT->isShiftPressed())
    {
        PT->backspace();
    }
    else
    {
        PT->clearSelection();
        if (PT->trackerPattern.selectState != 2)
            PT->moveCursorByStep();
    }
    PT->refreshPattern();
}

static void functInsert()
{
    if (PT->mode != cProTrackerMode::kModeEdit || !PT->editMode)
        return;

    if (PT->isShiftPressed())
        PT->trackerPattern.actualStep = 0; // Shift+Insert is Home
    else
        PT->insertRow();
    PT->refreshPattern();
}

// Enter previews the step in green mode, and writes the current note, instrument or effect in red
static void functEnter()
{
    if (PT->mode == cProTrackerMode::kModeFileList)
    {
        PT->loadSelectedFile();
        return;
    }

    const protracker::Note& n = *PT->cell(PT->trackerPattern.actualTrack, PT->trackerPattern.actualStep);
    const int note = noteFromPeriod(n.period());

    if (!PT->editMode)
    {
        PT->previewNote(note);
        return;
    }

    if (PT->editParam == cProTrackerMode::kParamInstr)
        PT->writeInstrument(PT->sample);
    else if (PT->editParam == cProTrackerMode::kParamNote)
        PT->writeNote(note < 0 ? 12 : note - PT->octave * 12);
    PT->moveCursorByStep();
    PT->refreshPattern();
}

static void functEncoder(int16_t value)
{
    value = interfaceGlobals.invertValForList ? -value : value;

    if (PT->mode == cProTrackerMode::kModeFileList)
    {
        PT->moveFileSelection(value > 0 ? 1 : -1);
        return;
    }

    if (PT->patternHeld || PT->selectedPlace >= 0)
    {
        const int place = PT->patternHeld ? 0 : PT->selectedPlace;
        if (place == 0)
            PT->changePattern(value);
        else if (place == 1)
            PT->changePosition(value);
        else if (place == 2)
            PT->changeEditStep(value);
        PT->refreshEditState();
        return;
    }

    if (PT->editMode)
    {
        PT->changeValue(value);
        PT->refreshPattern();
        PT->refreshTitle();
        return;
    }

    // green mode: the encoder moves the cursor
    if (!PT->replayer.isPlaying())
    {
        PT->moveStep(value > 0 ? 1 : -1);
        PT->refreshPattern();
    }
}

static void functActionButton(uint8_t button, uint8_t state)
{
    if (PT->mode == cProTrackerMode::kModeFileList)
    {
        if (state != buttonPress)
            return;

        if (button == 0)
            PT->hideFileList();
        else if (button == 1)
        {
            PT->saveSong();
        }
        else if (button == 2)
        {
            PT->newSong();
            PT->hideFileList();
        }
        else if (button == 7)
            PT->loadSelectedFile();
        return;
    }

    // Pattern, Position and Step jump are held while the arrows, the encoder or the pads change them
    if (button <= 2 && PT->page == 0)
    {
        if (state == buttonPress)
        {
            PT->selectedPlace = (int8_t)button;
        }
        else if (PT->selectedPlace == (int8_t)button)
        {
            PT->selectedPlace = -1;
        }
        PT->activateLabelsBorder();
        PT->refreshEditState();
        return;
    }

    if (state != buttonPress)
        return;

    if (button == 7)
    {
        PT->page = (PT->page + 1) % 2;
        PT->refreshLabels();
        return;
    }

    if (PT->page == 0)
    {
        switch (button)
        {
            case 4: // Preview
            {
                const protracker::Note& n =
                  *PT->cell(PT->trackerPattern.actualTrack, PT->trackerPattern.actualStep);
                PT->previewPeriod(n.sample() != 0 ? n.sample() : PT->sample, n.period());
                break;
            }
            case 5:
                if (PT->editMode)
                    PT->undo();
                break;
            case 6:
                if (PT->editMode)
                    PT->redo();
                break;
            default: break;
        }
    }
    else if (PT->editMode)
    {
        switch (button)
        {
            case 2: PT->duplicatePattern(); break;
            case 5: PT->copyPattern(); break;
            case 6: PT->pastePattern(); break;
            default: break;
        }
    }

    PT->refreshEditState();
}

static void functPads(uint8_t pad, uint8_t state, [[maybe_unused]] int16_t velo)
{
    if (state == padRelease)
    {
        PT->stopPreview();
        return;
    }

    if (state != padPress || PT->mode != cProTrackerMode::kModeEdit)
        return;

    // a held Pattern, Position or Step jump button takes the pads
    if (PT->patternHeld || PT->selectedPlace >= 0)
    {
        const int place = PT->patternHeld ? 0 : PT->selectedPlace;
        if (place == 0)
            PT->setPattern(pad);
        else if (place == 1)
            PT->setPosition(pad);
        else if (place == 2)
            PT->setEditStep(pad);
        PT->refreshEditState();
        return;
    }

    const bool fxTypeHeld = PT->isFxHeld();

    if (PT->editParam == cProTrackerMode::kParamNote)
    {
        const int semitone = padSemitone(pad);
        if (semitone < 0)
            return;

        if (PT->editMode)
        {
            const int note = PT->writeNote(semitone);
            if (note >= 0)
            {
                PT->previewNote(note);
                PT->moveCursorByStep();
            }
        }
        else
        {
            PT->previewNote(PT->octave * 12 + semitone);
        }
    }
    else if (PT->editParam == cProTrackerMode::kParamInstr)
    {
        if (pad < protracker::kSampleCount)
        {
            PT->sample = pad + 1;
            PT->previewPeriod(PT->sample, protracker::kPeriodTable[12]);
            if (PT->editMode)
            {
                PT->writeInstrument(PT->sample);
                PT->moveCursorByStep();
            }
        }
    }
    else if (PT->editMode)
    {
        if (fxTypeHeld)
            PT->writeFxType(pad);
        else
            PT->writeFxValue((pad * 255 + 23) / 47);
        PT->moveCursorByStep();
    }

    PT->refreshPattern();
    PT->refreshTitle();
}

#endif // PLATFORM_IMX
