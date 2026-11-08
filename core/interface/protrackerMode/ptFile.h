#ifndef INTERFACE_PROTRACKERMODE_PTFILE_H_
#define INTERFACE_PROTRACKERMODE_PTFILE_H_

#ifdef PLATFORM_IMX

#include "FileBrowser.h"
#include "config.h"
#include "core/modulesBase.h"
#include "display.h"
#include "list.h"

// The File screen. It has the layout of the stock Polyend file screen (New / Open / Save / Save As,
// the SD card browser with a folder list on the left and the on-screen keyboard to name a file) but
// works on a single ProTracker .mod song. The File button opens it.
class cProTrackerFile : public ModuleBase
{
  public:
    enum Screen : uint8_t
    {
        kScreenMain,
        kScreenOpen,
        kScreenSave,
        kScreenKeyboard,
        kScreenConfirm,
    };

    enum KeyboardUse : uint8_t
    {
        kNameFile,
        kNameFolder,
    };

    enum Pending : uint8_t
    {
        kNothing,
        kNewSong,
        kOpenBrowser,
        kLoadFile,
        kOverwrite,
        kDeleteFile,
    };

    cProTrackerFile() {}
    ~cProTrackerFile() {}

    virtual void update() {}
    virtual void start(uint32_t options);
    virtual void stop();
    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    // ---- screens
    void showMain();
    void showBrowser(bool reloadList = true);
    void refreshBrowser();
    void refreshBrowserLabels();
    void showKeyboard(const char* confirmText);
    void showConfirm(const char* line1, const char* line2, const char* confirmText, bool threeButtons);
    void bindMain();
    void bindBrowser();
    void bindKeyboard();
    void bindConfirm();

    // ---- actions
    void openBrowser(Screen screen);
    void closeBrowser();
    bool browserEnter();
    void moveList(int delta);
    void goBackToMain();
    void actNew();
    void actOpen();
    void actSave();
    void actSaveAs();
    void actLoadSelected();
    void actDeleteSelected();
    void actKeyboardConfirm();
    void actConfirmYes();
    void actConfirmNo();
    void actConfirmCancel();
    void runPending();
    void message(const char* text);
    void setSongNameOnTitle();

    Screen screen = kScreenMain;
    KeyboardUse keyboardUse = kNameFile;
    Pending pending = kNothing;
    bool pendingAfterSave = false;

    hControl label[8] = {};
    hControl bgLabel = -1;
    hControl browserLabelControl = -1;
    hControl fileListControl = -1;
    hControl blankSpaceCover = -1;

    strList projectList = {};
    FileBrowserData fileBrowserData = {};
    FileBrowser fileBrowser;
    bool browserReady = false;
};

extern cProTrackerFile protrackerFile;

#endif // PLATFORM_IMX

#endif /* INTERFACE_PROTRACKERMODE_PTFILE_H_ */
