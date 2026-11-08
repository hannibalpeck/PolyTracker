#ifndef INTERFACE_PROTRACKERMODE_H_
#define INTERFACE_PROTRACKERMODE_H_

#ifdef PLATFORM_IMX

#include "core/modulesBase.h"

#include "ptAudioStream.h"
#include "ptModule.h"
#include "ptReplayer.h"

// A ProTracker (Amiga, 4 channels, 64 rows, 31 samples) editor and player that loads and saves
// genuine .mod files from /ProTracker on the SD card. Opened with the Game button.
class cProTrackerMode : public ModuleBase
{
  public:
    enum Field : uint8_t
    {
        kFieldNote,
        kFieldSample,
        kFieldEffect,
        kFieldCount,
    };

    enum Mode : uint8_t
    {
        kModeEdit,
        kModeFileList,
    };

    static constexpr int kMaxFiles = 64;
    static constexpr int kFileNameLen = 40;
    static constexpr int kMaxVisibleRows = 21;

    cProTrackerMode() {}
    ~cProTrackerMode() {}

    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();
    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    // ---- editing (called from the input handlers)
    void moveRow(int delta);
    void moveOrder(int delta);
    void moveCursorX(int delta);
    void changeSample(int delta);
    void changeOctave(int delta);
    void changePatternAtOrder(int delta);
    void changeSongLength(int delta);
    void insertOrder();
    void deleteOrder();
    void enterNote(int semitone);
    void enterHexDigit(int digit);
    void clearCell();
    void newSong();

    // ---- transport
    void togglePlay(bool patternOnly);
    void stopPlayback();
    bool isPlaying() const { return replayer.isPlaying(); }
    bool isShiftPressed();

    // ---- files
    void showFileList();
    void hideFileList();
    void moveFileSelection(int delta);
    void loadSelectedFile();
    void saveSong();

    // ---- screen
    void showDefaultScreen();
    void setDefaultScreenFunct();
    void setFileListScreenFunct();
    void refreshAll();
    void refreshInfo();
    void refreshPattern();
    void refreshLabels();
    void showMessage(const char* message);

    Mode mode = kModeEdit;
    uint8_t page = 0; // which set of function labels is shown

    // The song itself lives in the pool, the objects here are small.
    protracker::Module module;
    protracker::Replayer replayer;
    ProTrackerStream* stream = nullptr;

    // cursor
    int editPos = 0; // position in the order list
    int editRow = 0;
    int cursorX = 0; // 0..11 = channel * 3 + field
    int sample = 1;  // current sample, 1..31
    int octave = 0;  // pad 0 plays semitone octave * 12 (0..2)
    int hexIndex = 0;
    bool dirty = false;
    bool initialised = false;

    // controls
    hControl label[8] = {};
    hControl bgLabel = -1;
    hControl infoLabel = -1;
    hControl cursorBar = -1;
    hControl gutterBox = -1;
    hControl channelBox[protracker::kChannelCount] = {};
    hControl listBox = -1;

    int rowsVisible = 15;
    int lineStep = 22;
    int boxTop = 58;

    // file list
    int fileCount = 0;
    int fileSelected = 0;
    char* fileList[kMaxFiles] = {};

    uint32_t lastViewKey = 0xFFFFFFFF;
    uint8_t lastVu[protracker::kChannelCount] = {};
};

extern cProTrackerMode protrackerMode;

#endif // PLATFORM_IMX

#endif /* INTERFACE_PROTRACKERMODE_H_ */
