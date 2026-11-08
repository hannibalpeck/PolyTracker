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

#include "core/interfaceMenuPopup.h"
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

// Text buffers for the display. They live in external RAM to spare the fast internal RAM; they are
// always filled completely before they are handed to the display.
__NOINIT(EXTERNAL_RAM) static char s_gutter[cProTrackerMode::kMaxVisibleRows * 4 + 8];
__NOINIT(EXTERNAL_RAM) static char
  s_channel[protracker::kChannelCount][cProTrackerMode::kMaxVisibleRows * 16 + 8];
__NOINIT(EXTERNAL_RAM) static char
  s_list[cProTrackerMode::kMaxVisibleRows * (cProTrackerMode::kFileNameLen + 8) + 8];
__NOINIT(EXTERNAL_RAM) static char s_fileNames[cProTrackerMode::kMaxFiles * cProTrackerMode::kFileNameLen];
static char s_info[160];
static char s_title[48];
static const char* kSongFolder = "ProTracker";

// ---------------------------------------------------------------------------------------------
// input handlers
// ---------------------------------------------------------------------------------------------
static void functSwitchModule(uint8_t button);
static void functGame();
static void functInstr();
static void functPattern();
static void functPlayAction();
static void functUp();
static void functDown();
static void functLeft();
static void functRight();
static void functDelete();
static void functEnter();
static void functEncoder(int16_t value);
static void functActionButton(uint8_t button, uint8_t state);
static void functPads(uint8_t pad, uint8_t state, int16_t velo);

// ---------------------------------------------------------------------------------------------
// formatting
// ---------------------------------------------------------------------------------------------
static const char kHexDigits[] = "0123456789ABCDEF";

static void formatNote(char* out, uint16_t period)
{
    static const char names[12][3] = { "C-", "C#", "D-", "D#", "E-", "F-",
                                       "F#", "G-", "G#", "A-", "A#", "B-" };
    if (period == 0)
    {
        out[0] = out[1] = out[2] = '-';
        return;
    }

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

    out[0] = names[best % 12][0];
    out[1] = names[best % 12][1];
    out[2] = (char)('1' + best / 12);
}

// "C-2 01 C20" (10 characters)
static int formatCell(char* out, const protracker::Note& n)
{
    formatNote(out, n.period());
    out[3] = ' ';

    if (n.sample() == 0)
    {
        out[4] = out[5] = '.';
    }
    else
    {
        out[4] = kHexDigits[(n.sample() >> 4) & 0xF];
        out[5] = kHexDigits[n.sample() & 0xF];
    }
    out[6] = ' ';

    if (n.command() == 0 && n.param() == 0)
    {
        out[7] = out[8] = out[9] = '.';
    }
    else
    {
        out[7] = kHexDigits[n.command()];
        out[8] = kHexDigits[n.param() >> 4];
        out[9] = kHexDigits[n.param() & 0xF];
    }
    return 10;
}

static protracker::Note* cellAt(int position, int row, int channel)
{
    const int pattern = PT->module.order[position];
    return &PT->module.pattern(pattern)[row * protracker::kChannelCount + channel];
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
// module life cycle
// ---------------------------------------------------------------------------------------------
bool cProTrackerMode::isShiftPressed()
{
    return platform->getButtons()->isButtonPressed(interfaceButtonShift);
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
        initialised = true;
    }

    // the sample sequencer and this mode don't share the output
    sequencer.stop();
    songTimer.hide();

    stream->setEnabled(true);

    mode = kModeEdit;
    page = 0;
    hexIndex = 0;
    lastViewKey = 0xFFFFFFFF;

    ip->setButtonObj(interfaceButtonPerformance, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonParams, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonFile, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonConfig, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonMaster, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSamplePlay, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleEdit, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleRec, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleLoad, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSong, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonPattern, buttonPress, functPattern);

    showDefaultScreen();
    setDefaultScreenFunct();
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

    display.setControlHide(listBox);
    padsBacklight.clearGlobalPads(1, 1, 1);
    songTimer.show();
}

void cProTrackerMode::update()
{
    if (!initialised || mode != kModeEdit)
        return;

    static bool wasPlaying = false;
    const bool playing = replayer.isPlaying();

    if (wasPlaying && !playing)
    {
        // playback ended (stop button or an F00 in the song): keep the cursor where it stopped
        editPos = replayer.position();
        editRow = replayer.row();
        lastViewKey = 0xFFFFFFFF;
    }
    wasPlaying = playing;

    uint32_t key;
    if (playing)
        key = 0x80000000u | ((uint32_t)replayer.position() << 16) | ((uint32_t)replayer.row() << 8) |
              (uint32_t)replayer.speed() | ((uint32_t)replayer.bpm() << 23);
    else
        key = ((uint32_t)editPos << 16) | ((uint32_t)editRow << 8) | (uint32_t)cursorX;

    if (key != lastViewKey)
    {
        lastViewKey = key;
        refreshPattern();
        refreshInfo();
    }
}

// ---------------------------------------------------------------------------------------------
// screen
// ---------------------------------------------------------------------------------------------
void cProTrackerMode::showDefaultScreen()
{
    snprintf(s_title, sizeof(s_title), "ProTracker");
    titleBar->show();
    titleBar->setTitle(s_title);

    display.setControlHide(listBox);
    display.setControlShow(cursorBar);
    display.setControlShow(gutterBox);
    for (int ch = 0; ch < protracker::kChannelCount; ch++)
        display.setControlShow(channelBox[ch]);

    refreshLabels();
    refreshAll();
    display.refreshControl(bgLabel);
    display.synchronizeRefresh();
}

void cProTrackerMode::setDefaultScreenFunct()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearAllPots();

    ip->setButtonObj(interfaceButtonPlay, buttonPress, functPlayAction);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDown);
    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functRight);
    ip->setButtonObj(interfaceButtonDelete, buttonPress, functDelete);
    ip->setButtonObj(interfaceButtonGame, buttonPress, functGame);
    ip->setButtonObj(interfaceButtonInstr, buttonPress, functInstr);

    for (uint8_t b = interfaceButton0; b <= interfaceButton7; b++)
        ip->setButtonObj(b, functActionButton);

    ip->setPotObj(interfacePot0, functEncoder);
    ip->setPadsGlobal(functPads);
}

void cProTrackerMode::setFileListScreenFunct()
{
    ip->setButtonObj(interfaceButtonMiniMiddle, buttonPress, functEnter);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDown);
}

void cProTrackerMode::refreshLabels()
{
    static const char* const kPage0[8] = { "Load", "Save", "New",   "Pos -",
                                           "Pos +", "Pat -", "Pat +", "More" };
    static const char* const kPage1[8] = { "Smp -", "Smp +", "Oct -", "Oct +",
                                           "Len -", "Len +", "Exit",  "More" };
    static const char* const kFiles[8] = { "Cancel", "", "", "", "", "", "", "Load" };
    static const char* const kSamples[8] = { "Done", "", "", "", "", "", "", "Done" };

    const char* const* names = mode == kModeFileList
                                 ? kFiles
                                 : (mode == kModeSampleList ? kSamples : (page == 0 ? kPage0 : kPage1));
    for (int i = 0; i < 8; i++)
    {
        display.setControlText(label[i], names[i]);
        display.refreshControl(label[i]);
    }
}

void cProTrackerMode::refreshAll()
{
    refreshPattern();
    refreshInfo();
}

void cProTrackerMode::showMessage(const char* message)
{
    snprintf(s_info, sizeof(s_info), "%s", message);
    display.setControlText(infoLabel, s_info);
    display.refreshControl(infoLabel);
    display.synchronizeRefresh();
}

void cProTrackerMode::refreshInfo()
{
    static const char* const kFieldNames[kFieldCount] = { "NOTE", "SAMPLE", "EFFECT" };

    const bool playing = replayer.isPlaying();
    const int pos = playing ? replayer.position() : editPos;
    const int row = playing ? replayer.row() : editRow;
    const int speed = playing ? replayer.speed() : 6;
    const int bpm = playing ? replayer.bpm() : 125;

    snprintf(s_info,
             sizeof(s_info),
             "%s%s  POS %02d/%02d PAT %02d ROW %02d  SPD %d BPM %d  SMP %02d OCT %d  CH%d %s",
             module.title[0] != '\0' ? module.title : "untitled",
             dirty ? "*" : "",
             pos,
             module.songLength,
             module.order[pos],
             row,
             speed,
             bpm,
             sample,
             octave + 1,
             cursorX / 3 + 1,
             kFieldNames[cursorX % 3]);

    display.setControlText(infoLabel, s_info);
    display.refreshControl(infoLabel);
}

void cProTrackerMode::refreshPattern()
{
    if (mode == kModeFileList)
    {
        // the list is a window of rowsVisible lines around the selection
        int first = fileSelected - rowsVisible / 2;
        if (first > fileCount - rowsVisible)
            first = fileCount - rowsVisible;
        if (first < 0)
            first = 0;

        int n = 0;
        s_list[0] = '\0';
        if (fileCount == 0)
            n += snprintf(s_list, sizeof(s_list), "No .mod files found in /%s on the SD card", kSongFolder);

        for (int i = first; i < fileCount && i < first + rowsVisible; i++)
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

    if (mode == kModeSampleList)
    {
        const int count = protracker::kSampleCount;
        int first = (sample - 1) - rowsVisible / 2;
        if (first > count - rowsVisible)
            first = count - rowsVisible;
        if (first < 0)
            first = 0;

        int n = 0;
        s_list[0] = '\0';
        for (int i = first; i < count && i < first + rowsVisible; i++)
        {
            const protracker::Sample& smp = module.samples[i];
            n += snprintf(s_list + n,
                          sizeof(s_list) - (size_t)n,
                          "%s %02d %-22s %5u\n",
                          i == sample - 1 ? ">" : " ",
                          i + 1,
                          smp.name,
                          (unsigned)smp.length);
            if (n >= (int)sizeof(s_list) - 4)
                break;
        }

        display.setControlText(listBox, s_list);
        display.refreshControl(listBox);
        return;
    }

    const bool playing = replayer.isPlaying();
    const int pos = playing ? replayer.position() : editPos;
    const int row = playing ? replayer.row() : editRow;

    constexpr int kGutterCap = (int)sizeof(s_gutter);
    constexpr int kChannelCap = (int)sizeof(s_channel[0]);
    constexpr int kCellChars = 11; // 10 characters of cell + newline

    const int lines = rowsVisible < kMaxVisibleRows ? rowsVisible : kMaxVisibleRows;

    int g = 0;
    int c[protracker::kChannelCount] = {};
    for (int line = 0; line < lines; line++)
    {
        const int r = row - lines / 2 + line;
        const bool blank = (r < 0 || r >= protracker::kRowCount);

        if (g + 4 < kGutterCap)
        {
            if (blank)
                s_gutter[g++] = '\n';
            else
                g += snprintf(s_gutter + g, (size_t)(kGutterCap - g), "%02d\n", r);
        }

        for (int ch = 0; ch < protracker::kChannelCount; ch++)
        {
            if (c[ch] + kCellChars >= kChannelCap)
                continue;

            if (!blank)
                c[ch] += formatCell(&s_channel[ch][c[ch]], *cellAt(pos, r, ch));
            s_channel[ch][c[ch]++] = '\n';
        }
    }
    s_gutter[g] = '\0';
    for (int ch = 0; ch < protracker::kChannelCount; ch++)
        s_channel[ch][c[ch]] = '\0';

    display.setControlText(gutterBox, s_gutter);
    display.refreshControl(gutterBox);
    for (int ch = 0; ch < protracker::kChannelCount; ch++)
    {
        display.setControlText(channelBox[ch], s_channel[ch]);
        display.refreshControl(channelBox[ch]);
    }

    // the bar sits under the channel the edit cursor is in
    display.setControlPosition(cursorBar,
                               62 + (cursorX / 3) * 182,
                               boxTop + 5 + (rowsVisible / 2) * lineStep - 2);
    display.refreshControl(cursorBar);
}

// ---------------------------------------------------------------------------------------------
// editing
// ---------------------------------------------------------------------------------------------
void cProTrackerMode::moveRow(int delta)
{
    if (replayer.isPlaying())
        return;

    editRow += delta;
    while (editRow >= protracker::kRowCount)
    {
        editRow -= protracker::kRowCount;
        editPos = (editPos + 1) % module.songLength;
    }
    while (editRow < 0)
    {
        editRow += protracker::kRowCount;
        editPos = (editPos + module.songLength - 1) % module.songLength;
    }
    hexIndex = 0;
}

void cProTrackerMode::moveOrder(int delta)
{
    if (replayer.isPlaying())
        return;

    editPos += delta;
    if (editPos < 0)
        editPos = 0;
    if (editPos >= module.songLength)
        editPos = module.songLength - 1;
    hexIndex = 0;
}

void cProTrackerMode::moveCursorX(int delta)
{
    cursorX += delta;
    const int count = protracker::kChannelCount * kFieldCount;
    if (cursorX < 0)
        cursorX = count - 1;
    if (cursorX >= count)
        cursorX = 0;
    hexIndex = 0;
}

void cProTrackerMode::changeSample(int delta)
{
    sample += delta;
    if (sample < 1)
        sample = 1;
    if (sample > protracker::kSampleCount)
        sample = protracker::kSampleCount;
}

void cProTrackerMode::changeOctave(int delta)
{
    octave += delta;
    if (octave < 0)
        octave = 0;
    if (octave > 2)
        octave = 2;
}

void cProTrackerMode::changePatternAtOrder(int delta)
{
    int value = module.order[editPos] + delta;
    if (value < 0)
        value = 0;
    if (value >= protracker::kMaxPatterns)
        value = protracker::kMaxPatterns - 1;
    module.order[editPos] = (uint8_t)value;
    dirty = true;
}

void cProTrackerMode::changeSongLength(int delta)
{
    int value = module.songLength + delta;
    if (value < 1)
        value = 1;
    if (value > protracker::kOrderCount)
        value = protracker::kOrderCount;
    module.songLength = (uint8_t)value;
    if (editPos >= module.songLength)
        editPos = module.songLength - 1;
    dirty = true;
}

void cProTrackerMode::insertOrder()
{
    if (module.songLength >= protracker::kOrderCount)
        return;

    for (int i = protracker::kOrderCount - 1; i > editPos; i--)
        module.order[i] = module.order[i - 1];
    module.songLength++;
    dirty = true;
}

void cProTrackerMode::deleteOrder()
{
    if (module.songLength <= 1)
        return;

    for (int i = editPos; i < protracker::kOrderCount - 1; i++)
        module.order[i] = module.order[i + 1];
    module.order[protracker::kOrderCount - 1] = 0;
    module.songLength--;
    if (editPos >= module.songLength)
        editPos = module.songLength - 1;
    dirty = true;
}

int cProTrackerMode::enterNote(int semitone)
{
    const int note = octave * 12 + semitone;
    if (note < 0 || note >= 36 || replayer.isPlaying())
        return -1;

    protracker::Note* n = cellAt(editPos, editRow, cursorX / 3);
    n->set((uint8_t)sample, (uint16_t)protracker::kPeriodTable[note], n->command(), n->param());
    dirty = true;
    moveRow(1);
    return note;
}

void cProTrackerMode::previewNote(int note)
{
    if (note < 0 || note >= 36 || replayer.isPlaying() || stream == nullptr)
        return;

    const int index = sample - 1;
    const int fineTune = module.samples[index].fineTune & 0x0F;
    stream->requestPreview(index, (uint16_t)protracker::kPeriodTable[fineTune * 37 + note]);
}

void cProTrackerMode::stopPreview()
{
    if (stream != nullptr)
        stream->requestPreviewStop();
}

void cProTrackerMode::enterHexDigit(int digit)
{
    if (replayer.isPlaying())
        return;

    protracker::Note* n = cellAt(editPos, editRow, cursorX / 3);
    const int field = cursorX % 3;

    if (field == kFieldSample)
    {
        int value = (hexIndex == 0) ? digit : (((n->sample() & 0xF) << 4) | digit);
        if (value > protracker::kSampleCount)
            value = protracker::kSampleCount;
        n->set((uint8_t)value, n->period(), n->command(), n->param());

        if (++hexIndex >= 2)
        {
            hexIndex = 0;
            moveRow(1);
        }
    }
    else if (field == kFieldEffect)
    {
        if (hexIndex == 0)
            n->set(n->sample(), n->period(), (uint8_t)digit, 0);
        else if (hexIndex == 1)
            n->set(n->sample(), n->period(), n->command(), (uint8_t)(digit << 4));
        else
            n->set(n->sample(), n->period(), n->command(), (uint8_t)((n->param() & 0xF0) | digit));

        if (++hexIndex >= 3)
        {
            hexIndex = 0;
            moveRow(1);
        }
    }
    dirty = true;
}

void cProTrackerMode::clearCell()
{
    if (replayer.isPlaying())
        return;

    cellAt(editPos, editRow, cursorX / 3)->set(0, 0, 0, 0);
    dirty = true;
    moveRow(1);
}

void cProTrackerMode::newSong()
{
    stream->pause();
    replayer.stop();
    module.clear();
    stream->resume();

    editPos = editRow = cursorX = 0;
    sample = 1;
    hexIndex = 0;
    dirty = false;
}

// ---------------------------------------------------------------------------------------------
// transport
// ---------------------------------------------------------------------------------------------
void cProTrackerMode::stopPlayback()
{
    stream->requestStop();
}

void cProTrackerMode::togglePlay(bool patternOnly)
{
    if (replayer.isPlaying())
    {
        stopPlayback();
        return;
    }

    stream->setEnabled(true);
    if (patternOnly)
        stream->requestPlayPattern(module.order[editPos], 0);
    else
        stream->requestPlay(editPos, 0);
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

    display.setControlHide(cursorBar);
    display.setControlHide(gutterBox);
    for (int ch = 0; ch < protracker::kChannelCount; ch++)
        display.setControlHide(channelBox[ch]);
    display.setControlShow(listBox);

    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    for (uint8_t b = interfaceButton0; b <= interfaceButton7; b++)
        ip->setButtonObj(b, functActionButton);
    setFileListScreenFunct();

    refreshLabels();
    refreshPattern();
    snprintf(s_info, sizeof(s_info), "LOAD MODULE  /%s  (%d files)", kSongFolder, fileCount);
    display.setControlText(infoLabel, s_info);
    display.refreshControl(infoLabel);
    display.synchronizeRefresh();
}

void cProTrackerMode::showSampleList()
{
    if (mode != kModeEdit)
        return;

    mode = kModeSampleList;
    hexIndex = 0;

    display.setControlHide(cursorBar);
    display.setControlHide(gutterBox);
    for (int ch = 0; ch < protracker::kChannelCount; ch++)
        display.setControlHide(channelBox[ch]);
    display.setControlShow(listBox);

    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    for (uint8_t b = interfaceButton0; b <= interfaceButton7; b++)
        ip->setButtonObj(b, functActionButton);
    setFileListScreenFunct(); // Up, Down and Enter work the same way

    refreshLabels();
    refreshPattern();
    snprintf(s_info, sizeof(s_info), "INSTRUMENTS  pads or encoder select, Instr closes");
    display.setControlText(infoLabel, s_info);
    display.refreshControl(infoLabel);
    display.synchronizeRefresh();
}

void cProTrackerMode::hideSampleList()
{
    stopPreview();
    hideFileList(); // returns to the editor
}

void cProTrackerMode::hideFileList()
{
    mode = kModeEdit;
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearButtonsRange(interfaceButtonMiniMiddle, interfaceButtonMiniMiddle);
    lastViewKey = 0xFFFFFFFF;
    showDefaultScreen();
    setDefaultScreenFunct();
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

    editPos = editRow = cursorX = 0;
    sample = 1;
    hexIndex = 0;
    dirty = false;

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

static void functGame()
{
    PT->eventFunct(eventSwitchToPreviousModule, PT, 0, 0);
}

static void functPlayAction()
{
    PT->togglePlay(PT->isShiftPressed());
    PT->lastViewKey = 0xFFFFFFFF;
}

static void functInstr()
{
    if (PT->mode == cProTrackerMode::kModeEdit)
        PT->showSampleList();
    else if (PT->mode == cProTrackerMode::kModeSampleList)
        PT->hideSampleList();
}

// The Pattern button always brings back the main ProTracker screen.
static void functPattern()
{
    if (PT->mode == cProTrackerMode::kModeSampleList)
        PT->hideSampleList();
    else if (PT->mode == cProTrackerMode::kModeFileList)
        PT->hideFileList();
    else if (PT->page != 0)
    {
        PT->page = 0;
        PT->refreshLabels();
    }
}

static void functUp()
{
    if (PT->mode == cProTrackerMode::kModeSampleList)
    {
        PT->changeSample(-1);
        PT->refreshPattern();
    }
    else if (PT->mode == cProTrackerMode::kModeFileList)
        PT->moveFileSelection(-1);
    else
    {
        PT->moveRow(PT->isShiftPressed() ? -16 : -1);
        PT->lastViewKey = 0xFFFFFFFF;
    }
}

static void functDown()
{
    if (PT->mode == cProTrackerMode::kModeSampleList)
    {
        PT->changeSample(1);
        PT->refreshPattern();
    }
    else if (PT->mode == cProTrackerMode::kModeFileList)
        PT->moveFileSelection(1);
    else
    {
        PT->moveRow(PT->isShiftPressed() ? 16 : 1);
        PT->lastViewKey = 0xFFFFFFFF;
    }
}

static void functLeft()
{
    PT->moveCursorX(-1);
    PT->lastViewKey = 0xFFFFFFFF;
}

static void functRight()
{
    PT->moveCursorX(1);
    PT->lastViewKey = 0xFFFFFFFF;
}

static void functDelete()
{
    PT->clearCell();
    PT->lastViewKey = 0xFFFFFFFF;
}

static void functEnter()
{
    if (PT->mode == cProTrackerMode::kModeFileList)
        PT->loadSelectedFile();
    else if (PT->mode == cProTrackerMode::kModeSampleList)
        PT->hideSampleList();
}

static void functEncoder(int16_t value)
{
    value = interfaceGlobals.invertValForList ? -value : value;
    const int step = value > 0 ? 1 : -1;

    if (PT->mode == cProTrackerMode::kModeSampleList)
    {
        PT->changeSample(step);
        PT->refreshPattern();
        return;
    }

    if (PT->mode == cProTrackerMode::kModeFileList)
    {
        PT->moveFileSelection(step);
        return;
    }

    PT->moveRow(PT->isShiftPressed() ? step * 16 : step);
    PT->lastViewKey = 0xFFFFFFFF;
}

static void functActionButton(uint8_t button, uint8_t state)
{
    if (state != buttonPress)
        return;

    if (PT->mode == cProTrackerMode::kModeSampleList)
    {
        if (button == 0 || button == 7)
            PT->hideSampleList();
        return;
    }

    if (PT->mode == cProTrackerMode::kModeFileList)
    {
        if (button == 0)
            PT->hideFileList();
        else if (button == 7)
            PT->loadSelectedFile();
        return;
    }

    const bool shift = PT->isShiftPressed();

    if (button == 7)
    {
        PT->page ^= 1;
        PT->refreshLabels();
        return;
    }

    if (PT->page == 0)
    {
        switch (button)
        {
            case 0: PT->showFileList(); return;
            case 1: PT->saveSong(); break;
            case 2: PT->newSong(); break;
            case 3: PT->moveOrder(-1); break;
            case 4: PT->moveOrder(1); break;
            case 5: PT->changePatternAtOrder(-1); break;
            case 6: PT->changePatternAtOrder(1); break;
            default: break;
        }
    }
    else
    {
        switch (button)
        {
            case 0: PT->changeSample(-1); break;
            case 1: PT->changeSample(1); break;
            case 2: PT->changeOctave(-1); break;
            case 3: PT->changeOctave(1); break;
            case 4:
                if (shift)
                    PT->deleteOrder();
                else
                    PT->changeSongLength(-1);
                break;
            case 5:
                if (shift)
                    PT->insertOrder();
                else
                    PT->changeSongLength(1);
                break;
            case 6: PT->eventFunct(eventSwitchToPreviousModule, PT, 0, 0); return;
            default: break;
        }
    }

    PT->lastViewKey = 0xFFFFFFFF;
}

// Pad number -> semitone above the lowest pad, using the same pad layout as the stock firmware.
static int padSemitone(uint8_t pad)
{
    if (pad >= 48)
        return -1;
    const int lowest = mtPadBoard.getNoteFromPad(36); // the pad that holds the lowest note
    return (int)mtPadBoard.getNoteFromPad((int8_t)pad) - lowest;
}

static void functPads(uint8_t pad, uint8_t state, [[maybe_unused]] int16_t velo)
{
    if (state == padRelease)
    {
        PT->stopPreview();
        return;
    }

    if (state != padPress)
        return;

    if (PT->mode == cProTrackerMode::kModeSampleList)
    {
        // like the stock instrument popup: a pad picks the instrument with that number
        if (pad < protracker::kSampleCount)
        {
            PT->sample = pad + 1;
            PT->refreshPattern();
            PT->previewNote(12); // C-2
        }
        return;
    }

    if (PT->mode != cProTrackerMode::kModeEdit)
        return;

    if (PT->cursorX % 3 == cProTrackerMode::kFieldNote)
    {
        const int semitone = padSemitone(pad);
        if (semitone >= 0)
        {
            const int note = PT->enterNote(semitone);
            if (note >= 0)
                PT->previewNote(note);
            else
                PT->previewNote(PT->octave * 12 + semitone); // song playing: ignored
        }
    }
    else if (pad < 16)
    {
        PT->enterHexDigit(pad);
    }

    PT->lastViewKey = 0xFFFFFFFF;
}

#endif // PLATFORM_IMX
