#ifdef PLATFORM_IMX

#include "protrackerMode/protrackerMode.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "Platform.h"
#include "elapsedMillis.h"
#include "cr_section_macros.h"
#include "mtAudioEngine.h"
#include "mtPadBoard.h"
#include "mtPadsBacklight.h"
#include "mtSequencer.h"

#include "core/interfacePopups.h"
#include "core/songTimer.h"

#include "ptEffects.h"
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
static uint8_t s_trackClip[kRows * 4];
static bool s_hasTrackClip = false;

static char s_info[160];
static char s_title[48];
static char s_right[48];
static char s_text2[8][16];

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
static void functVuToggle(uint8_t state);
static void functShift(uint8_t state);
static void functUp();
static void functDown();
static void functLeft();
static void functRight();
static void functCopy();
static void functDelete();
static void functInsert();
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

// Pad number (0..47) -> note 0..35 (C-1..B-3). Pad 13 (index 12) is C-3, the pads above it fold
// down, because a .mod only has three octaves: pads 1-12 are C-2..B-2, 13-24 C-3..B-3, 25-36 C-1..B-1,
// 37-48 C-2..B-2.
static int padSemitone(uint8_t pad)
{
    if (pad >= 48)
        return -1;
    int note = 12 + pad;
    if (note >= 36)
        note -= 36;
    return note;
}

// the pad that shows a note in the note popup
static int padForSemitone(int note)
{
    if (note < 0 || note >= 36)
        return 12;
    return note >= 12 ? note - 12 : note + 24;
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
        PT->previewPeriod(pad + 1, protracker::kPeriodTable[24]); // C-3
    }
}

// ---------------------------------------------------------------------------------------------
// module life cycle
// ---------------------------------------------------------------------------------------------
bool cProTrackerMode::isFxHeld()
{
    return platform->getButtons()->isButtonPressed(interfaceButtonFx2);
}

bool cProTrackerMode::isHeld(int button)
{
    return platform->getButtons()->isButtonPressed(button);
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
        trackerPattern.vuLane = true; // the second effect lane is a VU meter
        initialised = true;
        loadLastSong();
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
        stream->requestPreviewStop(); // a running song keeps playing on the other screens
    }

    mtPopups.hideStepPopups();
    popupActive = false;

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
    for (uint8_t i = 0; i < 3; i++)
    {
        if (!popupActive && stepButtonsState[i] == 1 &&
            stepButtonsTimer[i] > (i == 0 ? kNotePopupDelayMs : kInstrPopupDelayMs))
        {
            if (i == 0)
                showNotePopup();
            else if (i == 1)
                showInstrumentPopup();
            else
                showFxPopup();
        }
    }

    if (refreshTimer < 20 || mode != kModeEdit)
        return;
    refreshTimer = 0;

    const bool playing = replayer.isPlaying();
    static bool wasPlaying = false;

    bool changed = false;
    if (playing && !replayer.isPatternMode() && replayer.position() != editPos)
    {
        // a song that is playing (started on the song screen) shows the pattern it is in
        editPos = replayer.position();
        lastRow = -1;
        changed = true;
    }
    if (playing)
    {
        const int row = replayer.row();
        if (row != lastRow)
        {
            lastRow = row;
            trackerPattern.playheadPosition = (int16_t)row;
            // the cursor runs with the song, so that recording lands where it shows
            trackerPattern.actualStep = (int16_t)row;
            trackerPattern.selectState = 1;
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

    // VU meters: a redraw only when a bar moved
    bool vuMoved = false;
    for (int t = 0; t < kTracks; t++)
    {
        const uint8_t v = (uint8_t)(replayer.isMuted(t) ? 0 : replayer.channelVu(t));
        if (trackerPattern.vu[t] != v)
        {
            trackerPattern.vu[t] = v;
            vuMoved = true;
        }
    }
    if (vuMoved && !changed && trackerPattern.vuLane)
        display.refreshControl(patternControl);
}

// ---------------------------------------------------------------------------------------------
// screen
// ---------------------------------------------------------------------------------------------
void cProTrackerMode::showDefaultScreen()
{
    titleBar->show(true);

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
    static const char* const kPage0[8] = { "Pattern", "Position", "Step jump", "Copy",
                                           "Paste",   "Copy",     "Paste",     "More" };
    static const char* const kPage0b[8] = { "", "", "", "Track", "Track", "Pattern", "Pattern", "" };
    static const char* const kPage1[8] = { "Preview", "Undo", "Redo", "", "", "", "", "More" };
    static const char* const kPage1b[8] = { "", "", "", "", "", "", "", "" };

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

        if (page == 0)
        {
            text = kPage0[i];
            text2 = kPage0b[i];
            if (i < 3)
                text2 = s_text2[i];

            if (i >= 3 && i <= 6)
            {
                const bool copy = (i == 3 || i == 5);
                colors = (copy || editMode) ? display.getTheme().activeButtonLabelsColors
                                            : display.getTheme().inactiveButtonLabelsColors;
                data = &labelDataRed;
                value = 1; // the second line (Track / Pattern) is only drawn with a value
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
            if (i == 0)
            {
                value = 0; // Preview works in both modes
            }
            else
            {
                colors = editMode ? display.getTheme().activeButtonLabelsColors
                                  : display.getTheme().inactiveButtonLabelsColors;
                data = &labelDataRed;
                value = 0;
            }
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
        // a muted track's name is greyed out
        display.setControlColors(label[i],
                                 (track && replayer.isMuted(i)) ? display.getTheme().inactiveLabelsColors
                                                                : display.getTheme().activeLabelsColors);
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
    ip->setButtonObj(interfaceButtonFx1, functVuToggle);
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
    ip->setButtonObj(interfaceButtonFile, buttonPress, functSwitchModule);

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
// effect values, the usual ProTracker ranges
// ---------------------------------------------------------------------------------------------
// Cxx 00..40, Bxx 00..7F, Dxx 00..63 (decimal digits), Exy only changes y, the others 00..FF
// (Fxx: 01..1F is the speed in ticks, 20..FF the tempo in BPM)
static uint8_t stepParam(uint8_t command, uint8_t old, int delta)
{
    int v = old;
    switch (command)
    {
        case 0xC: v = old + delta; return (uint8_t)(v < 0 ? 0 : (v > 64 ? 64 : v));
        case 0xB: v = old + delta; return (uint8_t)(v < 0 ? 0 : (v > 127 ? 127 : v));
        case 0xD:
        {
            v = (old >> 4) * 10 + (old & 0x0F) + delta;
            v = v < 0 ? 0 : (v > 63 ? 63 : v);
            return (uint8_t)(((v / 10) << 4) | (v % 10));
        }
        case 0xE:
        {
            v = (old & 0x0F) + delta;
            v = v < 0 ? 0 : (v > 15 ? 15 : v);
            return (uint8_t)((old & 0xF0) | v);
        }
        default: v = old + delta; return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
    }
}

// a value picked on the pads (0..255) brought into the range of the command
static uint8_t padParam(uint8_t command, uint8_t old, int value)
{
    switch (command)
    {
        case 0xC: return (uint8_t)(value * 64 / 255);
        case 0xB: return (uint8_t)(value / 2);
        case 0xD:
        {
            const int v = value * 63 / 255;
            return (uint8_t)(((v / 10) << 4) | (v % 10));
        }
        case 0xE: return (uint8_t)((old & 0xF0) | (value >> 4));
        default: return (uint8_t)value;
    }
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
            // a sample without a note makes no sound, so a new instrument brings the C-3 note along
            const uint16_t period =
              (n->period() == 0 && instrument != 0) ? (uint16_t)protracker::kPeriodTable[24] : n->period();
            n->set((uint8_t)instrument, period, n->command(), n->param());
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
            n->set(n->sample(), n->period(), n->command(), padParam(n->command(), n->param(), value));
        }
    }
}

// An entry of the Fx list goes to the effect lane. E-commands keep their value nibble, other
// effects get a sensible starting value that the jogwheel then changes.
void cProTrackerMode::writeEffect(int index)
{
    if (index < 0 || index >= protracker::kEffectCount)
        return;

    const protracker::EffectInfo& fx = protracker::kEffects[index];

    storeUndo();
    int t0, r0, t1, r1;
    selectionRect(t0, r0, t1, r1);
    for (int t = t0; t <= t1; t++)
    {
        for (int r = r0; r <= r1; r++)
        {
            protracker::Note* n = cell(t, r);
            uint8_t param = n->param();
            if (fx.paramHigh >= 0)
                param = (uint8_t)((fx.paramHigh << 4) | (n->command() == 0xE ? (param & 0x0F) : 0));
            else if (n->command() != fx.command)
                param = fx.command == 0xC ? 0x40 : (fx.command == 0xF ? 0x06 : 0x00);
            n->set(n->sample(), n->period(), fx.command, param);
        }
    }
}

// Live recording: the note lands on the row that is playing, in the track of the cursor.
void cProTrackerMode::recordNote(int note)
{
    if (note < 0 || note >= 36)
        return;

    int row = replayer.row();
    if (row < 0 || row >= kRows)
        row = trackerPattern.actualStep;

    protracker::Note* n = cell(trackerPattern.actualTrack, row);
    n->set((uint8_t)sample, (uint16_t)protracker::kPeriodTable[note], n->command(), n->param());
    dirty = true;
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

    const bool fxType = platform->getButtons()->isButtonPressed(interfaceButtonFx2);

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
                n->set(n->sample(), n->period(), n->command(), stepParam(n->command(), n->param(), delta));
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
            // only the lane under the cursor is cleared, also in a selected block
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
    int t0 = trackerPattern.actualTrack;
    int r0 = trackerPattern.actualStep;
    if (trackerPattern.selectState == 2)
    {
        int t1, r1;
        selectionRect(t0, r0, t1, r1); // paste at the top left of a selected block
    }
    for (int t = 0; t < s_clipTracks && t0 + t < kTracks; t++)
    {
        for (int r = 0; r < s_clipRows && r0 + r < kRows; r++)
            *cell(t0 + t, r0 + r) = s_clip[t * kRows + r];
    }
    if (trackerPattern.selectState == 2)
        endSelection();
    else
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

void cProTrackerMode::copyTrack()
{
    for (int r = 0; r < kRows; r++)
        memcpy(s_trackClip + r * 4, cell(trackerPattern.actualTrack, r), 4);
    s_hasTrackClip = true;
}

void cProTrackerMode::pasteTrack()
{
    if (!s_hasTrackClip)
        return;
    storeUndo();
    for (int r = 0; r < kRows; r++)
        memcpy(cell(trackerPattern.actualTrack, r), s_trackClip + r * 4, 4);
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
    viewPattern = value;
    viewPatternPos = editPos;

    // a pattern that is playing: the new pattern is played instead, from its first row
    if (replayer.isPlaying() && replayer.isPatternMode())
    {
        stream->requestPlayPattern(value, 0);
        lastRow = -1;
    }
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
        period <= 0)
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
        if (stepButtonsState[0] || stepButtonsState[1] || stepButtonsState[2])
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

void cProTrackerMode::showFxPopup()
{
    popupActive = true;
    trackerPattern.popupMode |= 2;
    refreshPattern();

    const protracker::Note& n = *cell(trackerPattern.actualTrack, trackerPattern.actualStep);
    mtPopups.showStepPopup(stepPopupFx, (int8_t)protracker::effectIndex(n.command(), n.param()));
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
    else if (state == stepPopupFx)
    {
        writeEffect(value); // the chosen effect always goes to the lane
    }

    refreshEditState();
}

// ---------------------------------------------------------------------------------------------
// files
// ---------------------------------------------------------------------------------------------
// Clears the patterns and the order list, the samples stay.
void cProTrackerMode::clearSong()
{
    stream->pause();
    replayer.stop();
    module.clearSong();
    stream->resume();

    editPos = 0;
    viewPattern = -1;
    trackerPattern.actualStep = 0;
    trackerPattern.actualTrack = 0;
    trackerPattern.selectState = 1;
    dirty = true;
    s_undoMeta[0].count = s_undoMeta[1].count = 0;
}

void cProTrackerMode::newSong()
{
    stream->pause();
    replayer.stop();
    module.clear();
    stream->resume();

    editPos = 0;
    viewPattern = -1;
    trackerPattern.actualStep = 0;
    trackerPattern.actualTrack = 0;
    trackerPattern.selectState = 1;
    sample = 1;
    dirty = false;
    filePath[0] = '\0';
    s_undoMeta[0].count = s_undoMeta[1].count = 0;
}

// After a restart the last song is opened again. /Mod/.lastinfo says how: "P" + dirty + path points
// to the .mod that was last loaded or saved; "S" + dirty + path means a snapshot of the song as it
// was at shutdown (with its unsaved changes) is in /Mod/.lastsong.
static const char* kLastSongFile = "/Mod/.lastsong";
static const char* kLastInfoFile = "/Mod/.lastinfo";

static void writeLastInfo(FileSystem* fs, char mode, bool dirty, const char* path)
{
    if (!fs->exists("/Mod"))
        fs->mkdir(0, "/Mod");
    auto [ok, handle] = fs->openFile(kLastInfoFile, FileSystem::kWriteCreateNew);
    if (!ok)
        return;
    char text[112];
    snprintf(text, sizeof(text), "%c%c%s", mode, dirty ? '1' : '0', path);
    fs->writeFile(text, strlen(text), handle);
    fs->closeFile(handle);
}

static void rememberSong(const char* path)
{
    FileSystem* fs = protrackerMode.platform_fs();
    if (fs->exists(kLastSongFile))
        fs->remove(kLastSongFile); // an older snapshot must not win over this song
    writeLastInfo(fs, 'P', false, path);
}

void cProTrackerMode::loadLastSong()
{
    FileSystem* fs = platform->getFileSystem();
    if (!fs->exists(kLastInfoFile))
        return;

    auto [ok, handle] = fs->openFile(kLastInfoFile, FileSystem::kRead);
    if (!ok)
        return;
    char info[112] = {};
    const int32_t got = fs->readFile(info, sizeof(info) - 1, handle);
    fs->closeFile(handle);
    if (got < 2)
        return;

    const char* path = info + 2;
    const char* error = nullptr;
    if (info[0] == 'S' && fs->exists(kLastSongFile))
    {
        if (loadModule(kLastSongFile, &error, false))
        {
            snprintf(filePath, sizeof(filePath), "%s", path);
            dirty = info[1] == '1';
        }
    }
    else if (info[0] == 'P' && path[0] != '\0' && fs->exists(path))
    {
        loadFile(path, &error);
    }
}

// Called while the unit shuts down: the song, with its unsaved changes, goes to /Mod/.lastsong.
bool cProTrackerMode::saveSnapshot()
{
    if (!initialised)
        return true;

    FileSystem* fs = platform->getFileSystem();
    if (!fs->exists("/Mod"))
        fs->mkdir(0, "/Mod");

    const char* error = nullptr;
    if (!writeModule(kLastSongFile, &error))
        return false;

    writeLastInfo(fs, 'S', dirty, filePath);
    return true;
}

bool cProTrackerMode::loadFile(const char* path, const char** error)
{
    return loadModule(path, error, true);
}

bool cProTrackerMode::loadModule(const char* path, const char** error, bool remember)
{
    FileSystem* fs = platform->getFileSystem();

    auto [ok, handle] = fs->openFile(path, FileSystem::kRead);
    if (!ok)
    {
        *error = "Cannot open file";
        return false;
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
    viewPattern = -1;
    trackerPattern.actualStep = 0;
    trackerPattern.actualTrack = 0;
    trackerPattern.selectState = 1;
    sample = 1;
    dirty = false;
    s_undoMeta[0].count = s_undoMeta[1].count = 0;

    if (result != protracker::Result::kOk)
    {
        filePath[0] = '\0';
        *error = protracker::resultText(result);
        return false;
    }

    snprintf(filePath, sizeof(filePath), "%s", path);
    if (remember)
        rememberSong(filePath);
    return true;
}

// ---- WAV export
// The song is rendered with the replayer, faster than real time, to a 16 bit 44.1 kHz stereo WAV
// file: from position 0 until the song ends or starts over, at most 15 minutes. It runs in slices
// (exportStep) so that the screen keeps being redrawn.
namespace
{
struct ExportState
{
    FileSystem::FileHandle handle;
    uint32_t frames = 0;
    int percent = 0;
    bool muted[kTracks] = {};
    bool active = false;
};
ExportState s_exp;

constexpr uint32_t kExportRate = 44100;
constexpr uint32_t kExportBlock = 256;
constexpr uint32_t kExportMaxFrames = kExportRate * 60 * 15;

void put32(uint8_t* d, uint32_t v)
{
    d[0] = (uint8_t)v;
    d[1] = (uint8_t)(v >> 8);
    d[2] = (uint8_t)(v >> 16);
    d[3] = (uint8_t)(v >> 24);
}

void wavHeader(uint8_t* h, uint32_t dataBytes)
{
    memcpy(h, "RIFF", 4);
    put32(h + 4, 36 + dataBytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    h[20] = 1; // PCM
    h[21] = 0;
    h[22] = 2; // stereo
    h[23] = 0;
    put32(h + 24, kExportRate);
    put32(h + 28, kExportRate * 4);
    h[32] = 4;
    h[33] = 0;
    h[34] = 16;
    h[35] = 0;
    memcpy(h + 36, "data", 4);
    put32(h + 40, dataBytes);
}
} // namespace

int cProTrackerMode::exportPercent() const
{
    return s_exp.percent;
}

bool cProTrackerMode::exportBegin(char* outPath, size_t outSize, const char** error)
{
    FileSystem* fs = platform->getFileSystem();

    const char* folder = "/Mod/Export";
    if (!fs->exists(folder))
        fs->mkdir(0, folder);

    // name: the song's file name, a number is added when it exists already
    char base[48] = "song";
    if (filePath[0] != '\0')
    {
        const char* slash = strrchr(filePath, '/');
        snprintf(base, sizeof(base), "%s", slash != nullptr ? slash + 1 : filePath);
        const size_t len = strlen(base);
        if (len > 4 && strcasecmp(base + len - 4, ".mod") == 0)
            base[len - 4] = '\0';
    }
    snprintf(outPath, outSize, "%s/%s.wav", folder, base);
    for (int n = 2; fs->exists(outPath) && n < 1000; n++)
        snprintf(outPath, outSize, "%s/%s_%d.wav", folder, base, n);

    auto [ok, handle] = fs->openFile(outPath, FileSystem::kWriteCreateNew);
    if (!ok)
    {
        *error = "Cannot create file";
        return false;
    }

    uint8_t head[44];
    wavHeader(head, 0);
    if (fs->writeFile(head, sizeof(head), handle) != (int32_t)sizeof(head))
    {
        fs->closeFile(handle);
        *error = "Cannot write the WAV file";
        return false;
    }

    stream->pause();
    replayer.stop();
    for (int t = 0; t < kTracks; t++)
    {
        s_exp.muted[t] = replayer.isMuted(t);
        replayer.setMuted(t, false); // the export is the whole song
    }

    s_exp.handle = handle;
    s_exp.frames = 0;
    s_exp.percent = 0;
    s_exp.active = true;
    replayer.play(0, 0);
    return true;
}

// Renders for about budgetMs. Returns 0 while it goes on, 1 when the file is complete, -1 on an error.
int cProTrackerMode::exportStep(uint32_t budgetMs, const char** error)
{
    if (!s_exp.active)
        return 1;

    FileSystem* fs = platform->getFileSystem();
    static int16_t left[kExportBlock];
    static int16_t right[kExportBlock];
    static int16_t out[kExportBlock * 2];

    const int length = module.songLength > 0 ? module.songLength : 1;
    bool good = true;
    elapsedMillis spent;

    while (good && replayer.isPlaying() && replayer.songLoops() == 0 &&
           s_exp.frames < kExportMaxFrames && spent < budgetMs)
    {
        replayer.render(left, right, kExportBlock);
        for (uint32_t i = 0; i < kExportBlock; i++)
        {
            out[i * 2] = left[i];
            out[i * 2 + 1] = right[i];
        }
        good = fs->writeFile(out, sizeof(out), s_exp.handle) == (int32_t)sizeof(out);
        s_exp.frames += kExportBlock;
    }

    int percent = (replayer.position() * kRows + replayer.row()) * 100 / (length * kRows);
    s_exp.percent = percent > 99 ? 99 : percent;

    const bool finished = !replayer.isPlaying() || replayer.songLoops() != 0 ||
                          s_exp.frames >= kExportMaxFrames;
    if (good && !finished)
        return 0;

    // done (or failed): restore the player and complete the header
    replayer.stop();
    for (int t = 0; t < kTracks; t++)
        replayer.setMuted(t, s_exp.muted[t]);
    stream->resume();
    s_exp.active = false;

    if (good)
    {
        uint8_t head[44];
        wavHeader(head, s_exp.frames * 4);
        good = fs->seekFile(0, s_exp.handle) &&
               fs->writeFile(head, sizeof(head), s_exp.handle) == (int32_t)sizeof(head);
    }
    fs->closeFile(s_exp.handle);

    if (!good)
    {
        *error = "Cannot write the WAV file";
        return -1;
    }
    s_exp.percent = 100;
    return 1;
}

// writes the module as a .mod file, without touching the file name or the dirty flag
bool cProTrackerMode::writeModule(const char* path, const char** error)
{
    FileSystem* fs = platform->getFileSystem();

    auto [ok, handle] = fs->openFile(path, FileSystem::kWriteCreateNew);
    if (!ok)
    {
        *error = "Cannot create file";
        return false;
    }

    FsContext ctx = { fs, handle };
    protracker::FileIo io;
    io.ctx = &ctx;
    io.write = ioWrite;

    const protracker::Result result = module.save(io);
    fs->closeFile(handle);

    if (result != protracker::Result::kOk)
    {
        *error = protracker::resultText(result);
        return false;
    }
    return true;
}

bool cProTrackerMode::saveFile(const char* path, const char** error)
{
    // a song without a title (the title is stored in the .mod header) takes the file name
    if (module.title[0] == '\0')
    {
        const char* base = strrchr(path, '/');
        base = base != nullptr ? base + 1 : path;
        size_t n = 0;
        while (base[n] != '\0' && base[n] != '.' && n < 20)
        {
            module.title[n] = (base[n] >= 0x20 && base[n] <= 0x7E) ? base[n] : '_';
            n++;
        }
        module.title[n] = '\0';
    }

    // the write blocks the UI, so show the indicator and draw it now
    mtPopups.show(0, "Saving song");
    display.drawNow();
    elapsedMillis savingShown;

    const bool written = writeModule(path, error);

    // a small song saves in a few milliseconds: keep the message readable
    while (savingShown < 400)
    {
    }

    if (!written)
        return false;

    snprintf(filePath, sizeof(filePath), "%s", path);
    dirty = false;
    rememberSong(filePath);
    return true;
}

// the shutdown sequence of the interface calls this
bool protrackerSaveOnShutdown()
{
    return protrackerMode.saveSnapshot();
}

// ---------------------------------------------------------------------------------------------
// input
// ---------------------------------------------------------------------------------------------
static void functSwitchModule(uint8_t button)
{
    PT->eventFunct(eventSwitchModule, PT, &button, 0);
}

// Pattern held: arrows and the encoder pick the pattern of the current position
static void functPattern(uint8_t state)
{
    if (state == buttonHold)
        return;

    PT->patternHeld = (state == buttonPress);
    PT->refreshEditState();
}

// Rec switches writing on and off. Rec together with Play starts the pattern and records.
static void functRecAction()
{
    if (PT->mode != cProTrackerMode::kModeEdit)
        return;

    if (PT->isHeld(interfaceButtonPlay))
    {
        if (!PT->editMode)
            PT->storeUndo(); // the whole take can be undone in one step
        PT->editMode = 1;
        if (!PT->isPlaying())
            PT->togglePlay(PT->isShiftPressed());
    }
    else
    {
        PT->editMode = !PT->editMode;
        if (PT->editMode && PT->isPlaying())
            PT->storeUndo();
    }
    PT->refreshEditState();
}

static void functPlayAction()
{
    if (PT->mode != cProTrackerMode::kModeEdit)
        return;

    if (!PT->isPlaying() && PT->isHeld(interfaceButtonRec))
    {
        if (!PT->editMode)
            PT->storeUndo();
        PT->editMode = 1;
    }

    PT->togglePlay(PT->isShiftPressed());
    PT->refreshEditState();
}

static void functNote(uint8_t state)
{
    if (PT->mode != cProTrackerMode::kModeEdit)
        return;

    if (state == buttonHold)
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

    if (state == buttonHold)
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

// Fx1 switches the VU meters in the second effect lane on and off
static void functVuToggle(uint8_t state)
{
    if (PT->mode != cProTrackerMode::kModeEdit || state != buttonPress)
        return;
    PT->trackerPattern.vuLane = !PT->trackerPattern.vuLane;
    PT->refreshPattern();
}

// Fx2 selects the effect: a ProTracker step has one effect command with its value
static void functFx(uint8_t state)
{
    if (PT->mode != cProTrackerMode::kModeEdit || state == buttonHold)
        return;

    if (state == buttonPress)
    {
        PT->editParam = cProTrackerMode::kParamFx1;
        PT->setStepButtonState(2, 1);
        PT->refreshEditState();
    }
    else
    {
        PT->setStepButtonState(2, 0);
        if (PT->popupActive)
            PT->applyPopup();
    }
}

static void functShift(uint8_t state)
{
    if (PT->mode != cProTrackerMode::kModeEdit || PT->popupActive || state == buttonHold)
        return;

    if (state == buttonPress)
    {
        PT->showMuteButtons(true);
    }
    else
    {
        // the selection stays after Shift is released, so that Copy and Delete can use it
        PT->isSelectingNow = false;
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
    // while the song plays in green mode the cursor follows the playhead vertically, but it can
    // still move between the tracks
    if (PT->replayer.isPlaying() && !PT->editMode)
    {
        if (dTrack != 0)
        {
            PT->moveTrack(dTrack);
            PT->refreshPattern();
        }
        return;
    }

    const bool shift = PT->isShiftPressed();
    if (shift)
    {
        // extend the selection (an existing one is continued)
        if (PT->trackerPattern.selectState != 2)
            PT->startSelection();
        PT->isSelectingNow = true;
    }
    else if (PT->trackerPattern.selectState == 2)
    {
        PT->endSelection(); // moving without Shift drops the selection
    }

    if (dTrack != 0)
        PT->moveTrack(dTrack);
    if (dStep != 0)
        PT->moveStep(dStep);

    if (PT->isSelectingNow)
        PT->updateSelection();

    PT->refreshPattern();
}

// At the top of the pattern Shift+Up selects the whole track, Shift+Up again the whole pattern.
// Shift+Down goes back down through those; it returns false when there is nothing to step back.
static bool growSelection(bool up)
{
    strTrackerPattern& t = PT->trackerPattern;

    int t0 = 0, r0 = 0, t1 = 0, r1 = 0;
    const bool range = t.selectState == 2;
    if (range)
        PT->selectionRect(t0, r0, t1, r1);

    const bool fullTrack = range && r0 == 0 && r1 == kRows - 1 && t0 == t1;
    const bool fullPattern = range && r0 == 0 && r1 == kRows - 1 && t0 == 0 && t1 == kTracks - 1;

    if (up)
    {
        if (fullPattern)
            return true;

        t.selectState = 2;
        t.selectColumn = 0;
        t.selectStartStep = 0;
        t.selectEndStep = kRows - 1;
        if (fullTrack)
        {
            t.selectStartTrack = 0;
            t.selectEndTrack = kTracks - 1;
        }
        else
        {
            t.selectStartTrack = t.selectEndTrack = t.actualTrack;
        }
    }
    else
    {
        if (fullPattern)
        {
            t.selectStartTrack = t.selectEndTrack = t.actualTrack;
        }
        else if (fullTrack)
        {
            PT->endSelection();
        }
        else
        {
            return false;
        }
    }

    PT->isSelectingNow = true;
    PT->refreshPattern();
    return true;
}

static void functUp()
{
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

    if (PT->isShiftPressed())
    {
        // Shift+Up selects from the cursor, one step at a time. At the top the first press selects
        // the row, the next ones the whole track and then the whole pattern.
        if (PT->trackerPattern.actualStep > 0)
        {
            moveCursor(0, -1);
            return;
        }
        if (PT->trackerPattern.selectState != 2)
        {
            PT->startSelection();
            PT->refreshPattern();
            return;
        }
        growSelection(true);
        return;
    }

    moveCursor(0, -1);
}

static void functDown()
{
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

    if (PT->isShiftPressed())
    {
        if (!growSelection(false))
            moveCursor(0, 1);
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
    if (PT->mode != cProTrackerMode::kModeEdit)
        return;

    if (PT->isShiftPressed())
        PT->pasteSelection();
    else
        PT->copySelection();
    PT->refreshPattern();
}

static void functDelete()
{
    if (PT->mode != cProTrackerMode::kModeEdit)
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
    if (state == buttonHold)
        return; // a long press is still the same press

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
            case 3: PT->copyTrack(); break;
            case 4:
                if (PT->editMode)
                    PT->pasteTrack();
                break;
            case 5: PT->copyPattern(); break;
            case 6:
                if (PT->editMode)
                    PT->pastePattern();
                break;
            default: break;
        }
    }
    else
    {
        switch (button)
        {
            case 0: // Preview
            {
                const protracker::Note& n =
                  *PT->cell(PT->trackerPattern.actualTrack, PT->trackerPattern.actualStep);
                PT->previewPeriod(n.sample() != 0 ? n.sample() : PT->sample,
                                 n.period() != 0 ? n.period() : protracker::kPeriodTable[24]); // C-3 when empty
                break;
            }
            case 1:
                if (PT->editMode)
                    PT->undo();
                break;
            case 2:
                if (PT->editMode)
                    PT->redo();
                break;
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

        if (PT->editMode && PT->isPlaying())
        {
            PT->recordNote(semitone); // live recording
            PT->previewNote(semitone);
        }
        else if (PT->editMode)
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
            PT->previewPeriod(PT->sample, protracker::kPeriodTable[24]);
            if (PT->editMode && PT->isPlaying())
            {
                PT->recordNote(24); // recording: the sample and its C-3 note
            }
            else if (PT->editMode)
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
