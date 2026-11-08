#ifndef INTERFACE_PROTRACKERMODE_H_
#define INTERFACE_PROTRACKERMODE_H_

#ifdef PLATFORM_IMX

#include "core/modulesBase.h"
#include "display.h"
#include "elapsedMillis.h"

#include "ptAudioStream.h"
#include "ptModule.h"
#include "ptReplayer.h"

// The ProTracker (Amiga, 4 channels, 64 rows, 31 samples) pattern screen. It uses the stock Polyend
// pattern grid, bottom buttons and popups, but edits a genuine .mod song that is played by the
// ProTracker replayer. Songs are loaded from and saved to /ProTracker on the SD card.
class cProTrackerMode : public ModuleBase
{
  public:
    enum Mode : uint8_t
    {
        kModeEdit,
    };

    enum Param : uint8_t
    {
        kParamNote,
        kParamInstr,
        kParamFx1,
        kParamFx2, // same as Fx1: a ProTracker step has a single effect
    };

    static constexpr int kUndoDepth = 16;

    cProTrackerMode() {}
    ~cProTrackerMode() {}

    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();
    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    // ---- song access
    int currentPattern() const { return module.order[editPos]; }
    protracker::Note* cell(int track, int row);
    int sampleIndex() const { return sample - 1; }

    // ---- cursor and selection
    void moveStep(int delta);
    void moveTrack(int delta);
    void moveCursorByStep();
    void startSelection();
    void updateSelection();
    void endSelection();
    void selectionRect(int& t0, int& r0, int& t1, int& r1);

    // ---- editing (all of these store an undo step first)
    int writeNote(int semitone); // returns the note written (0..35) or -1
    void writeInstrument(int instrument);
    void writeFxValue(int value);
    void writeFxType(int type);
    void writeEffect(int index); // an entry of the Fx list: command and a fitting value
    void recordNote(int note);   // live recording: the note goes to the playing row of the cursor track
    void changeValue(int delta);
    void clearSelection();
    void backspace();
    void insertRow();
    void copySelection();
    void pasteSelection();
    void copyPattern();
    void pastePattern();
    void duplicatePattern();
    void copyTrack();
    void pasteTrack();
    void storeUndo();
    void undo();
    void redo();

    // ---- parameters of the held buttons (Pattern, Position, Step jump)
    void changePattern(int delta);
    void setPattern(int value);
    void changePosition(int delta);
    void setPosition(int value);
    void changeEditStep(int delta);
    void setEditStep(int value);

    // ---- sound
    void previewNote(int note);
    void previewPeriod(int sampleNumber, int period);
    void stopPreview();
    void togglePlay(bool fromCursor);
    bool isPlaying() const { return replayer.isPlaying(); }
    void toggleMute(int track);

    // ---- popups opened by holding Note or Instr
    void setStepButtonState(uint8_t button, uint8_t state);
    void showNotePopup();
    void showInstrumentPopup();
    void showFxPopup();
    void applyPopup();
    void cancelPopups();

    // ---- files
    bool loadFile(const char* path, const char** error); // error: text when it returns false
    bool saveFile(const char* path, const char** error);
    bool exportWav(char* outPath, size_t outSize, const char** error);
    void newSong();

    // ---- screen
    void bindButtons();
    void showDefaultScreen();
    void refreshPattern();
    void refreshTitle();
    void refreshLabels();
    void refreshHeldValues();
    void refreshEditState();
    void activateLabelsBorder();
    void showMuteButtons(bool show);
    void showMessage(const char* message);
    bool isShiftPressed();
    bool isFxHeld();
    bool isHeld(int button);

    Mode mode = kModeEdit;
    uint8_t page = 0; // which set of bottom buttons is shown
    uint8_t editMode = 0; // 0 green (pads only play), 1 red (pads write)
    uint8_t editParam = kParamNote;
    int8_t selectedPlace = -1; // bottom button that is being held
    bool patternHeld = false;
    bool isSelectingNow = false;
    bool shiftMuteView = false;

    // The song itself lives in the pool, the objects here are small.
    protracker::Module module;
    protracker::Replayer replayer;
    ProTrackerStream* stream = nullptr;

    // cursor
    int editPos = 0;  // position in the order list
    int editStep = 1; // rows to move after writing
    int sample = 1;   // current sample, 1..31
    int octave = 0;   // pad 0 plays semitone octave * 12
    bool dirty = false;
    bool initialised = false;

    // controls
    hControl label[8] = {};
    hControl bgLabel = -1;
    hControl frameControl = -1;
    hControl patternControl = -1;
    hControl infoLabel = -1;

    static strTrackerPattern trackerPattern;
    strLabelData labelDataRed;
    strLabelData labelDataGreen;
    strLabelData labelDataMore;
    strFrameData frameData;

    // popups
    uint8_t stepButtonsState[3] = {}; // Note, Instr, Fx
    elapsedMillis stepButtonsTimer[3];
    bool popupActive = false;
    bool wasNoteEditBefore = false;

    // the file the song was loaded from or saved to (empty for a new song)
    char filePath[96] = "";

    elapsedMillis refreshTimer;
    int lastRow = -1;
};

extern cProTrackerMode protrackerMode;

// used by the stock popups (interfacePopups.cpp)
const char* protrackerSampleName(int index);
void protrackerPopupPad(uint8_t pad, uint8_t pressed);

#endif // PLATFORM_IMX

#endif /* INTERFACE_PROTRACKERMODE_H_ */
