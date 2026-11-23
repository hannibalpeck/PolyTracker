#ifdef PLATFORM_IMX

#include "protrackerMode/ptSong.h"

#include <stdio.h>
#include <string.h>

#include "Platform.h"
#include "mtSequencer.h"

#include "core/songTimer.h"
#include "protrackerMode/protrackerMode.h"

cProTrackerSong protrackerSong;
static cProTrackerSong* SE = &protrackerSong;
static cProTrackerMode* PT = &protrackerMode;

constexpr int kTracks = protracker::kChannelCount;
constexpr int kRows = protracker::kRowCount;

// data the stock song control reads
// The control reads bitmask[pattern - 1]; with .mod pattern numbers from 0 it is given a pointer to
// element 2, so pattern p lives at s_blocks[p + 1] and pattern 0 is reached through index -1.
static uint8_t s_blocks[protracker::kMaxPatterns + 2];
static uint8_t s_midiBlocks[protracker::kMaxPatterns + 2];
static uint8_t s_mute[kTracks];
static uint8_t s_midiMute[1];
static char s_trackNames[kTracks][21];
static char s_midiNames[1][2];
static char s_tempo[16];
static char s_message[48];

struct SongSnapshot
{
    uint8_t length;
    uint8_t pos;
    uint8_t order[protracker::kOrderCount];
};
static SongSnapshot s_undo[cProTrackerSong::kUndoDepth];
static int s_undoCount = 0;

static void functSwitchModule(uint8_t button);
static void functPlayButton();
static void functActionButton(uint8_t button, uint8_t state);
static void functUp();
static void functDown();
static void functLeft();
static void functRight();
static void functEncoder(int16_t value);
static void functShift(uint8_t state);

// ---------------------------------------------------------------------------------------------
// song access
// ---------------------------------------------------------------------------------------------
int cProTrackerSong::length() const
{
    return PT->module.songLength;
}

int cProTrackerSong::selected() const
{
    return PT->editPos;
}

void cProTrackerSong::selectSlot(int slot)
{
    if (slot < 0)
        slot = 0;
    if (slot >= length())
        slot = length() - 1;
    PT->editPos = slot;
}

void cProTrackerSong::moveSlot(int delta)
{
    selectSlot(selected() + delta);
}

void cProTrackerSong::storeUndo()
{
    if (s_undoCount == kUndoDepth)
    {
        memmove(&s_undo[0], &s_undo[1], sizeof(s_undo[0]) * (kUndoDepth - 1));
        s_undoCount--;
    }
    SongSnapshot& s = s_undo[s_undoCount++];
    s.length = PT->module.songLength;
    s.pos = (uint8_t)PT->editPos;
    memcpy(s.order, PT->module.order, sizeof(s.order));
    PT->dirty = true;
}

void cProTrackerSong::undo()
{
    if (s_undoCount == 0)
        return;

    const SongSnapshot& s = s_undo[--s_undoCount];
    PT->module.songLength = s.length;
    PT->editPos = s.pos;
    memcpy(PT->module.order, s.order, sizeof(s.order));
    PT->dirty = true;
}

void cProTrackerSong::changePattern(int delta)
{
    storeUndo();
    int value = PT->module.order[selected()] + delta;
    if (value < 0)
        value = 0;
    if (value >= protracker::kMaxPatterns)
        value = protracker::kMaxPatterns - 1;
    PT->module.order[selected()] = (uint8_t)value;
}

// A new slot goes after the selected one. It repeats that pattern, or (Shift) it is a new empty
// pattern: the number after the highest one in the list, which no slot refers to yet.
void cProTrackerSong::addSlot(bool empty)
{
    if (length() >= protracker::kOrderCount)
    {
        showMessage("SONG IS FULL (128 SLOTS)");
        return;
    }

    int pattern = PT->module.order[selected()];
    if (empty)
    {
        int highest = 0;
        for (int i = 0; i < length(); i++)
        {
            if (PT->module.order[i] > highest)
                highest = PT->module.order[i];
        }
        if (highest + 1 >= protracker::kMaxPatterns)
        {
            showMessage("NO FREE PATTERN LEFT (MAX 100)");
            return;
        }
        pattern = highest + 1;
    }

    storeUndo();

    if (empty)
        memset(PT->module.pattern(pattern), 0, protracker::kPatternBytes);

    const int at = selected() + 1;
    for (int i = length(); i > at; i--)
        PT->module.order[i] = PT->module.order[i - 1];
    PT->module.order[at] = (uint8_t)pattern;
    PT->module.songLength++;
    PT->editPos = at;
}

void cProTrackerSong::deleteSlot()
{
    if (length() <= 1)
    {
        showMessage("A SONG NEEDS AT LEAST ONE SLOT");
        return;
    }

    storeUndo();
    for (int i = selected(); i < length() - 1; i++)
        PT->module.order[i] = PT->module.order[i + 1];
    PT->module.order[length() - 1] = 0;
    PT->module.songLength--;
    selectSlot(selected());
}

// ---------------------------------------------------------------------------------------------
// tempo
// ---------------------------------------------------------------------------------------------
static protracker::Note* findTempoCell()
{
    for (int slot = 0; slot < SE->length(); slot++)
    {
        protracker::Note* p = PT->module.pattern(PT->module.order[slot]);
        for (int row = 0; row < kRows; row++)
        {
            for (int t = 0; t < kTracks; t++)
            {
                protracker::Note& n = p[row * kTracks + t];
                if (n.command() == 0xF && n.param() >= 32)
                    return &n;
            }
        }
    }
    return nullptr;
}

int cProTrackerSong::tempo()
{
    const protracker::Note* n = findTempoCell();
    return n != nullptr ? n->param() : 125;
}

void cProTrackerSong::changeTempo(int delta)
{
    int bpm = tempo() + delta;
    if (bpm < 32)
        bpm = 32;
    if (bpm > 255)
        bpm = 255;

    protracker::Note* n = findTempoCell();
    if (n == nullptr)
    {
        // no tempo command yet: put one on the first free effect of the first row
        protracker::Note* first = PT->module.pattern(PT->module.order[0]);
        for (int t = 0; t < kTracks && n == nullptr; t++)
        {
            if (first[t].command() == 0 && first[t].param() == 0)
                n = &first[t];
        }
        if (n == nullptr)
        {
            showMessage("NO FREE EFFECT ON THE FIRST ROW");
            return;
        }
    }

    n->set(n->sample(), n->period(), 0xF, (uint8_t)bpm);
    PT->dirty = true;
}

// ---------------------------------------------------------------------------------------------
// sound
// ---------------------------------------------------------------------------------------------
void cProTrackerSong::playPattern()
{
    PT->stopPreview();
    if (playMode == 2)
        PT->editPos = PT->replayer.position(); // loop the pattern that is playing now
    PT->stream->requestPlayPattern(PT->currentPattern(), 0);
    playMode = 1;
}

void cProTrackerSong::playSong()
{
    PT->stopPreview();
    PT->stream->requestPlay(selected(), 0);
    playMode = 2;
}

void cProTrackerSong::stopPlaying()
{
    PT->stream->requestStop();
    playMode = 0;
}

// ---------------------------------------------------------------------------------------------
// screen
// ---------------------------------------------------------------------------------------------
void cProTrackerSong::start(uint32_t)
{
    moduleRefresh = 1;
    PT->viewPattern = -1; // the order list is edited here, the pattern screen follows it again

    // the song lives in the pattern screen's module; this screen only edits its order list
    PT->stream->setEnabled(true);
    sequencer.stop();
    songTimer.hide();

    selectedPlace = 1;
    playMode = PT->replayer.isPlaying() ? 2 : 0;
    shownPlayMode = 255;
    s_undoCount = 0;
    refreshTimer = 0;

    for (int t = 0; t < kTracks; t++)
        snprintf(s_trackNames[t], sizeof(s_trackNames[t]), "Track %d", t + 1);
    s_midiNames[0][0] = '\0';

    bindButtons();
    showDefaultScreen();
}

void cProTrackerSong::stop()
{
    moduleRefresh = 0;

    PT->stream->requestPreviewStop(); // the song keeps playing on the other screens

    display.setControlHide(frameControl);
    display.setControlHide(barControl);
    display.setControlHide(songPlayerControl);
    songTimer.show();
}

void cProTrackerSong::showMessage(const char* message)
{
    snprintf(s_message, sizeof(s_message), "%s", message);
    titleBar->setRightLabel(s_message);
}

// which tracks of every pattern hold something (the coloured blocks of the matrix)
void cProTrackerSong::refreshBlocks()
{
    memset(s_blocks, 0, sizeof(s_blocks));
    for (int p = 0; p < protracker::kMaxPatterns; p++)
    {
        const protracker::Note* n = PT->module.pattern(p);
        uint8_t bits = 0;
        for (int row = 0; row < kRows; row++)
        {
            for (int t = 0; t < kTracks; t++)
            {
                const protracker::Note& c = n[row * kTracks + t];
                if (c.period() != 0 || c.sample() != 0 || c.command() != 0 || c.param() != 0)
                    bits |= (uint8_t)(1 << t);
            }
        }
        s_blocks[p + 1] = bits;
    }
}

// Fills the data of the stock song control.
void cProTrackerSong::readSong()
{
    refreshBlocks();

    for (int t = 0; t < kTracks; t++)
        s_mute[t] = PT->replayer.isMuted(t) ? 1 : 0;

    patternsList.start = selected();
    patternsList.length = (uint16_t)length();
    patternsList.linesCount = 12;
    patternsList.data = PT->module.order;
    patternsList.names = nullptr;

    songPlayerData.songLength = (uint8_t)length();
    songPlayerData.songData = PT->module.order;
    songPlayerData.isEditMode = &PT->editMode;
    songPlayerData.isListActive = 1;
    songPlayerData.selection.isActive = 0;
    songPlayerData.list = &patternsList;
    songPlayerData.normalTracksCount = kTracks;
    songPlayerData.midiTracksCount = 0;
    songPlayerData.normalPatternsBitmask = &s_blocks[2];
    songPlayerData.midiPatternsBitmask = &s_midiBlocks[2];
    songPlayerData.normalTracksMute = s_mute;
    songPlayerData.midiTracksMute = s_midiMute;
    songPlayerData.normalTracksNames = &s_trackNames[0][0];
    songPlayerData.midiTracksNames = &s_midiNames[0][0];
    songPlayerData.normalTracksNamesBuffLen = sizeof(s_trackNames[0]);
    songPlayerData.midiTracksNamesBuffLen = sizeof(s_midiNames[0]);
    songPlayerData.progress.isPlaying = 0;
    songPlayerData.progress.trackPreview = 255;
    songPlayerData.progress.patternLength = kRows;

    display.setControlData(songPlayerControl, &songPlayerData);
    display.setControlValue(songPlayerControl, selected());
}

void cProTrackerSong::refreshList()
{
    patternsList.start = selected();
    patternsList.length = (uint16_t)length();
    songPlayerData.songLength = (uint8_t)length();
    display.setControlValue(songPlayerControl, selected());
    display.refreshControl(songPlayerControl);

    // the title bar stays empty here (it held a text from a local buffer, which showed as garbage)
    titleBar->setTitle("");
    titleBar->setRightLabel("");
}

void cProTrackerSong::refreshTempo()
{
    snprintf(s_tempo, sizeof(s_tempo), "%d BPM", tempo());
    display.setControlText(label[7], "Tempo");
    display.setControlText2(label[7], s_tempo);
    display.refreshControl(label[7]);

    display.setControlShow(barControl);
    display.setControlValue(barControl, (tempo() * 100) / 255);
    display.refreshControl(barControl);
}

void cProTrackerSong::refreshButtons()
{
    if (playMode == shownPlayMode)
        return;
    shownPlayMode = playMode;

    const char* t0 = "Play";
    const char* u0 = "Pattern";
    const char* t1 = "Play";
    const char* u1 = "Song";

    if (playMode == 2)
    {
        t0 = "Loop";
        t1 = "Stop";
        u1 = "";
    }
    else if (playMode == 1)
    {
        t0 = "Continue";
        u0 = "Song";
        t1 = "Stop";
        u1 = "";
    }

    display.setControlText(label[0], t0);
    display.setControlText2(label[0], u0);
    display.setControlText(label[1], t1);
    display.setControlText2(label[1], u1);
    display.refreshControl(label[0]);
    display.refreshControl(label[1]);
}

void cProTrackerSong::showDefaultScreen()
{
    titleBar->show(true);

    display.setControlStyle2(label[7], controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
    display.setControlData(label[2], &labelDataRed);
    display.setControlData(label[3], &labelDataRed);
    display.setControlData(label[4], &labelDataRed);
    display.setControlData(label[5], &doubleArrowRed);
    display.setAddControlStyle(label[5], controlStyleShowBitmap);

    display.setControlText(label[2], "Undo");
    display.setControlText(label[3], "Add");
    display.setControlText2(label[3], "Slot");
    display.setControlText(label[4], "Delete");
    display.setControlText2(label[4], "Slot");
    display.setControlText(label[5], "Pattern");
    display.setControlText2(label[5], "");
    display.setControlText(label[6], "");
    display.setControlText2(label[6], "");
    display.setControlColors(label[7], display.getTheme().activeLabelsColors);

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlValue(label[i], 1);
        display.setControlShow(label[i]);
        display.refreshControl(label[i]);
    }
    display.setControlValue(label[2], 0);
    display.setControlValue(label[5], 0);

    display.setControlShow(bgLabel);
    display.refreshControl(bgLabel);

    readSong();
    display.setControlShow(songPlayerControl);
    display.refreshControl(songPlayerControl);

    shownPlayMode = 255;
    refreshButtons();
    refreshTempo();
    refreshList();

    display.setControlHide(frameControl);
    display.refreshControl(frameControl);
    display.synchronizeRefresh();
}

void cProTrackerSong::update()
{
    if (refreshTimer < 40)
        return;
    refreshTimer = 0;

    const bool playing = PT->replayer.isPlaying();
    if (!playing && playMode != 0)
        playMode = 0;

    songPlayerData.progress.isPlaying = playing ? 1 : 0;
    if (playing)
    {
        songPlayerData.progress.currentSongPosition = (uint8_t)PT->replayer.position();
        songPlayerData.progress.positionInPattern = (uint8_t)PT->replayer.row();
        patternsList.icon.useSpecialIcons = 1;
        patternsList.icon.iconPositionInList = (int8_t)PT->replayer.position();
        patternsList.icon.iconNum = displayPlayIcon + (playMode == 1 ? 1 : 0);
    }
    else
    {
        patternsList.icon.useSpecialIcons = 0;
    }

    for (int t = 0; t < kTracks; t++)
        s_mute[t] = PT->replayer.isMuted(t) ? 1 : 0;

    refreshButtons();
    display.refreshControl(songPlayerControl);
    display.synchronizeRefresh();
}

// ---------------------------------------------------------------------------------------------
// input
// ---------------------------------------------------------------------------------------------
void cProTrackerSong::bindButtons()
{
    ip->clearAllButtons();
    ip->clearAllPots();
    ip->clearGlobalPads();

    for (uint8_t b = interfaceButton0; b <= interfaceButton7; b++)
        ip->setButtonObj(b, functActionButton);

    ip->setButtonObj(interfaceButtonPlay, buttonPress, functPlayButton);
    ip->setButtonObj(interfaceButtonShift, functShift);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDown);
    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functRight);
    ip->setPotObj(interfacePot0, functEncoder);

    ip->setButtonObj(interfaceButtonFile, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonPattern, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonConfig, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonMaster, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSamplePlay, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleEdit, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleRec, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleLoad, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonParams, buttonPress, functSwitchModule);
}

// Play starts the song at the selected position, or stops it
static void functPlayButton()
{
    if (SE->playMode != 0 || PT->replayer.isPlaying())
        SE->stopPlaying();
    else
        SE->playSong();
}

static void functSwitchModule(uint8_t button)
{
    SE->eventFunct(eventSwitchModule, SE, &button, 0);
}

static void refreshAll()
{
    SE->refreshList();
    SE->refreshTempo();
    SE->readSong();
    display.refreshControl(SE->songPlayerControl);
    display.synchronizeRefresh();
}

static void functShift(uint8_t state)
{
    if (state == buttonHold)
        return;

    display.setControlText2(SE->label[3], state == buttonPress ? "Empty Slot" : "Slot");
    display.refreshControl(SE->label[3]);
    display.synchronizeRefresh();
}

static void functActionButton(uint8_t button, uint8_t state)
{
    if (state == buttonHold)
        return; // a long press is still the same press

    // Tempo is held while the encoder or the arrows change it
    if (button == 7)
    {
        SE->selectedPlace = state == buttonPress ? 2 : 1;
        if (state == buttonPress)
        {
            display.setControlValue(SE->frameControl, 2);
            display.setControlShow(SE->frameControl);
        }
        else
        {
            display.setControlHide(SE->frameControl);
        }
        display.refreshControl(SE->frameControl);
        display.synchronizeRefresh();
        return;
    }

    if (state != buttonPress)
        return;

    const bool shift = SE->buttons->isButtonPressed(interfaceButtonShift);

    switch (button)
    {
        case 0: // Play Pattern / Loop Pattern / Continue Song
            if (SE->playMode == 1)
                SE->playSong();
            else
                SE->playPattern();
            break;
        case 1: // Play Song / Stop
            if (SE->playMode != 0)
                SE->stopPlaying();
            else
                SE->playSong();
            break;
        case 2: SE->undo(); break;
        case 3: SE->addSlot(shift); break;
        case 4: SE->deleteSlot(); break;
        case 5: SE->changePattern(shift ? -10 : -1); break;
        case 6: SE->changePattern(shift ? 10 : 1); break;
        default: break;
    }

    SE->shownPlayMode = 255;
    SE->refreshButtons();
    refreshAll();
}

static void functUp()
{
    if (SE->selectedPlace == 2)
        SE->changeTempo(SE->buttons->isButtonPressed(interfaceButtonShift) ? 10 : 1);
    else
        SE->moveSlot(-1);
    refreshAll();
}

static void functDown()
{
    if (SE->selectedPlace == 2)
        SE->changeTempo(SE->buttons->isButtonPressed(interfaceButtonShift) ? -10 : -1);
    else
        SE->moveSlot(1);
    refreshAll();
}

static void functLeft()
{
    SE->changePattern(-1);
    refreshAll();
}

static void functRight()
{
    SE->changePattern(1);
    refreshAll();
}

// encoder: moves through the slots; with Shift it changes the pattern of the slot; Tempo held: tempo
static void functEncoder(int16_t value)
{
    value = interfaceGlobals.invertValForList ? -value : value;

    if (SE->selectedPlace == 2)
        SE->changeTempo(value);
    else if (SE->buttons->isButtonPressed(interfaceButtonShift))
        SE->changePattern(value);
    else
        SE->moveSlot(value > 0 ? 1 : -1);

    refreshAll();
}

#endif // PLATFORM_IMX
