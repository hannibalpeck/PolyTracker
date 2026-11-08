#ifndef INTERFACE_PROTRACKERMODE_PTSONG_H_
#define INTERFACE_PROTRACKERMODE_PTSONG_H_

#ifdef PLATFORM_IMX

#include "core/modulesBase.h"
#include "display.h"
#include "elapsedMillis.h"
#include "songPlayerControl.h"

// The ProTracker song (arrangement) screen. It has the look of the stock Polyend song screen: the
// playlist with the pattern of every slot, a block matrix that shows which tracks of each pattern hold
// notes, and a tempo bar. The playlist is the order list of the .mod file, so a song has at most 128
// slots and patterns 0..99.
class cProTrackerSong : public ModuleBase
{
  public:
    static constexpr int kUndoDepth = 16;

    cProTrackerSong() {}
    ~cProTrackerSong() {}

    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();
    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    // ---- song editing
    int length() const;
    int selected() const;
    void selectSlot(int slot);
    void moveSlot(int delta);
    void changePattern(int delta);
    void addSlot(bool empty);
    void deleteSlot();
    void storeUndo();
    void undo();

    // ---- tempo: the first Fxx (>= 32) in the song, like in any ProTracker module
    int tempo();
    void changeTempo(int delta);

    // ---- sound
    void playPattern();
    void playSong();
    void stopPlaying();

    // ---- screen
    void bindButtons();
    void showDefaultScreen();
    void readSong();
    void refreshList();
    void refreshButtons();
    void refreshTempo();
    void refreshBlocks();
    void showMessage(const char* message);

    uint8_t selectedPlace = 1; // 1 playlist, 2 tempo (held)
    uint8_t playMode = 0;      // 0 stopped, 1 pattern, 2 song
    uint8_t shownPlayMode = 255;

    hControl label[8] = {};
    hControl bgLabel = -1;
    hControl frameControl = -1;
    hControl barControl = -1;
    hControl songPlayerControl = -1;

    strFrameData frameData;
    strLabelData doubleArrowRed;
    strLabelData labelDataRed;

    localList_t patternsList = {};
    player_data_t songPlayerData = {};

    elapsedMillis refreshTimer;
};

extern cProTrackerSong protrackerSong;

#endif // PLATFORM_IMX

#endif /* INTERFACE_PROTRACKERMODE_PTSONG_H_ */
