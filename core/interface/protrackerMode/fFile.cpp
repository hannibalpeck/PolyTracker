#ifdef PLATFORM_IMX

#include "protrackerMode/ptFile.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "Platform.h"
#include "cr_section_macros.h"
#include "mtKeyboardManager.h"
#include "util_math.h"

#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "protrackerMode/protrackerMode.h"

cProTrackerFile protrackerFile;
static cProTrackerFile* F = &protrackerFile;
static cProTrackerMode* PT = &protrackerMode;

static const char* kSongFolder = "/Mod";

__NOINIT(EXTERNAL_RAM) static char s_names[kMaxFilesListLength * kMaxFileNameLen];
static char* s_list[kMaxFilesListLength];
static char s_selection[kMaxFilesListLength];
static char s_songName[40];
static char s_path[kMaxFilePathLen + 1];
static char s_info[96];
static cProTrackerFile::Screen s_returnTo = cProTrackerFile::kScreenMain;

static void functSwitchModule(uint8_t button);
static void functNew();
static void functClearSong();
static void functOpen();
static void functSave();
static void functSaveAs();
static void functExportWav();
static void functEnter();
static void functCancel();
static void functBrowserAccept();
static void functDelete(uint8_t state);
static void functNewFolder();
static void functLeft();
static void functRight();
static void functUp();
static void functDown();
static void functEncoder(int16_t value);
static void functMiniMiddle(uint8_t state);
static void functPads(uint8_t pad, uint8_t state, int16_t velo);
static void functKeyConfirm();
static void functAutoName();
static void functConfirmYes();
static void functConfirmNo();
static void functConfirmCancel();

// ---------------------------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------------------------
static bool hasModExtension(const char* name)
{
    const size_t len = strlen(name);
    return len > 4 && strcasecmp(name + len - 4, ".mod") == 0;
}

static void resetLabels()
{
    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlValue(F->label[i], 0);
        display.setControlText(F->label[i], "");
        display.setControlText2(F->label[i], "");
        display.setControlColors(F->label[i], display.getTheme().activeButtonLabelsColors);
        display.setRemoveControlStyle(F->label[i], controlStyleBackground);
        display.setControlStyle(F->label[i],
                                controlStyleCenterX | controlStyleCenterY | controlStyleFont3 |
                                  controlStyleShow);
    }
}

static void refreshLabelsNow()
{
    for (uint8_t i = 0; i < 8; i++)
        display.refreshControl(F->label[i]);
}

// the name of the song as shown on the title bar and offered as a file name
static void songName(char* out, size_t size)
{
    const char* base = nullptr;
    if (PT->filePath[0] != '\0')
    {
        base = strrchr(PT->filePath, '/');
        base = base != nullptr ? base + 1 : PT->filePath;
    }
    else if (PT->module.title[0] != '\0')
    {
        base = PT->module.title;
    }
    else
    {
        base = "untitled";
    }

    snprintf(out, size, "%s", base);
    const size_t len = strlen(out);
    if (len > 4 && strcasecmp(out + len - 4, ".mod") == 0)
        out[len - 4] = '\0';
}

void cProTrackerFile::setSongNameOnTitle()
{
    songName(s_songName, sizeof(s_songName));
    titleBar->showRightLabel(true);
    titleBar->setRightLabel(s_songName);
}

void cProTrackerFile::message(const char* text)
{
    mtPopups.show(0, text);
}

// ---------------------------------------------------------------------------------------------
// life cycle
// ---------------------------------------------------------------------------------------------
void cProTrackerFile::start(uint32_t)
{
    moduleRefresh = 1;

    // the song keeps playing nothing here: the audio belongs to the pattern and song screens
    PT->stream->setEnabled(false);
    songTimer.hide();

    pending = kNothing;
    pendingAfterSave = false;

    showMain();
    bindMain();
}

void cProTrackerFile::stop()
{
    moduleRefresh = 0;

    keyboardManager.deactivateKeyboard();
    mtPopups.hideInfoPopup();

    display.setControlHide(fileListControl);
    display.setControlHide(browserLabelControl);
    display.setControlHide(blankSpaceCover);
    songTimer.show();
}

// ---------------------------------------------------------------------------------------------
// screens
// ---------------------------------------------------------------------------------------------
void cProTrackerFile::showMain()
{
    screen = kScreenMain;
    keyboardManager.deactivateKeyboard();
    mtPopups.hideInfoPopup();

    titleBar->show(true);
    titleBar->setTitle("File");
    setSongNameOnTitle();

    display.setControlHide(fileListControl);
    display.setControlHide(browserLabelControl);

    resetLabels();
    display.setControlText(label[0], "New");
    display.setControlText2(label[0], "Song");
    display.setControlText(label[1], "Open");
    display.setControlText(label[2], "Clear");
    display.setControlText2(label[2], "Song");
    display.setControlText(label[3], "Save");
    display.setControlText(label[4], "Save As");
    display.setControlText(label[6], "Export");
    display.setControlText2(label[6], "WAV");
    for (uint8_t i = 0; i < 8; i++)
        display.setControlValue(label[i], i == 0 || i == 1 || i == 2 || i == 3 || i == 4 || i == 6 ? 1 : 0);
    refreshLabelsNow();

    display.setControlValue(bgLabel, 255);
    display.refreshControl(bgLabel);

    display.setControlSize(blankSpaceCover, 800, -1);
    display.setControlPosition(blankSpaceCover, 0 + 1, -1);
    display.setControlShow(blankSpaceCover);
    display.refreshControl(blankSpaceCover);

    display.synchronizeRefresh();
}

void cProTrackerFile::showBrowser(bool reloadList)
{
    titleBar->setTitle(screen == kScreenSave ? "File - Save Song" : "File - Open Song");

    projectList.start = fileBrowserData.browserListPosition;
    projectList.length = fileBrowserData.browserListLength;
    projectList.linesCount = 13;
    projectList.selectionActive = 1;
    projectList.selectTab = (uint8_t*)fileBrowserData.selectionList;
    projectList.data = fileBrowserData.browserList;

    if (reloadList)
        display.setControlData(fileListControl, &projectList);
    display.setControlColors(fileListControl, display.getTheme().projectActiveListColors);
    display.setControlShow(fileListControl);
    display.refreshControl(fileListControl);

    resetLabels();
    display.setControlText(label[0], "Enter");
    display.setControlText(label[2], "Delete");
    if (screen == kScreenSave)
        display.setControlText(label[3], "New Folder");
    display.setControlText(label[6], "Cancel");
    display.setControlText(label[7], screen == kScreenSave ? "Save" : "Open");
    refreshBrowserLabels();
    refreshLabelsNow();

    display.setControlText(browserLabelControl, "SD Card");
    display.setControlShow(browserLabelControl);
    display.refreshControl(browserLabelControl);

    display.setControlValue(bgLabel, 255);
    display.refreshControl(bgLabel);

    display.setControlSize(blankSpaceCover, 500, -1);
    display.setControlPosition(blankSpaceCover, 300 + 1, -1);
    display.setControlShow(blankSpaceCover);
    display.refreshControl(blankSpaceCover);

    display.synchronizeRefresh();
}

void cProTrackerFile::refreshBrowserLabels()
{
    const bool onFile = fileBrowser.isPositionOnSelectionList();
    const uint32_t* active = display.getTheme().activeButtonLabelsColors;
    const uint32_t* inactive = display.getTheme().inactiveButtonLabelsColors;

    display.setControlColors(label[0], onFile ? inactive : active);
    display.setControlColors(label[2], onFile ? active : inactive);
    display.setControlColors(label[7], (screen == kScreenSave || onFile) ? active : inactive);

    display.refreshControl(label[0]);
    display.refreshControl(label[2]);
    display.refreshControl(label[7]);
}

void cProTrackerFile::refreshBrowser()
{
    refreshBrowserLabels();
    display.setControlValue(fileListControl, fileBrowserData.browserListPosition);
    display.refreshControl(fileListControl);
}

void cProTrackerFile::showKeyboard(const char* confirmText)
{
    screen = kScreenKeyboard;
    mtPopups.hideInfoPopup();

    display.setControlHide(fileListControl);
    display.setControlHide(browserLabelControl);
    display.setControlHide(blankSpaceCover);

    resetLabels();
    display.setControlText(label[0], "Enter");
    display.setControlText(label[1], "Auto Name");
    display.setControlText(label[6], "Cancel");
    display.setControlText(label[7], confirmText);
    refreshLabelsNow();

    display.setControlValue(bgLabel, 255);
    display.refreshControl(bgLabel);
    display.synchronizeRefresh();
}

// A question with Cancel / confirm (two buttons) or Cancel / Don't Save / Save (three).
void cProTrackerFile::showConfirm(const char* line1,
                                  const char* line2,
                                  const char* confirmText,
                                  bool threeButtons)
{
    s_returnTo = screen;
    screen = kScreenConfirm;

    resetLabels();
    if (threeButtons)
    {
        display.setControlText(label[5], "Cancel");
        display.setControlText(label[6], "Don't Save");
        display.setControlText(label[7], confirmText);
    }
    else
    {
        display.setControlText(label[6], "Cancel");
        display.setControlText(label[7], confirmText);
    }

    for (uint8_t i = 0; i < 8; i++)
    {
        const bool used = threeButtons ? i >= 5 : i >= 6;
        if (used)
        {
            display.setControlColors(label[i], display.getTheme().highlightedButtonLabelsColors);
            display.setAddControlStyle(label[i], controlStyleBackground);
        }
    }
    refreshLabelsNow();

    mtPopups.showInfoPopup(line1, line2);

    display.setControlValue(bgLabel, 255);
    display.refreshControl(bgLabel);
    display.synchronizeRefresh();
}

// ---------------------------------------------------------------------------------------------
// button bindings
// ---------------------------------------------------------------------------------------------
static void bindModuleButtons()
{
    F->ip->setButtonObj(interfaceButtonPattern, buttonPress, functSwitchModule);
    F->ip->setButtonObj(interfaceButtonSong, buttonPress, functSwitchModule);
    F->ip->setButtonObj(interfaceButtonConfig, buttonPress, functSwitchModule);
    F->ip->setButtonObj(interfaceButtonMaster, buttonPress, functSwitchModule);
    F->ip->setButtonObj(interfaceButtonSamplePlay, buttonPress, functSwitchModule);
    F->ip->setButtonObj(interfaceButtonSampleEdit, buttonPress, functSwitchModule);
    F->ip->setButtonObj(interfaceButtonSampleRec, buttonPress, functSwitchModule);
    F->ip->setButtonObj(interfaceButtonSampleLoad, buttonPress, functSwitchModule);
    F->ip->setButtonObj(interfaceButtonParams, buttonPress, functSwitchModule);
}

void cProTrackerFile::bindMain()
{
    ip->clearAllButtons();
    ip->clearAllPots();
    ip->clearGlobalPads();

    ip->setButtonObj(interfaceButton0, buttonPress, functNew);
    ip->setButtonObj(interfaceButton1, buttonPress, functOpen);
    ip->setButtonObj(interfaceButton2, buttonPress, functClearSong);
    ip->setButtonObj(interfaceButton3, buttonPress, functSave);
    ip->setButtonObj(interfaceButton4, buttonPress, functSaveAs);
    ip->setButtonObj(interfaceButton6, buttonPress, functExportWav);
    bindModuleButtons();
}

void cProTrackerFile::bindBrowser()
{
    ip->clearAllButtons();
    ip->clearAllPots();
    ip->clearGlobalPads();

    ip->setButtonObj(interfaceButton0, buttonPress, functEnter);
    ip->setButtonObj(interfaceButton2, buttonPress, []() { F->actDeleteSelected(); });
    if (screen == kScreenSave)
        ip->setButtonObj(interfaceButton3, buttonPress, functNewFolder);
    ip->setButtonObj(interfaceButton6, buttonPress, functCancel);
    ip->setButtonObj(interfaceButton7, buttonPress, functBrowserAccept);

    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functRight);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDown);
    ip->setButtonObj(interfaceButtonDelete, functDelete);
    ip->setButtonObj(interfaceButtonMiniMiddle, functMiniMiddle);
    ip->setPotObj(interfacePot0, functEncoder);
    bindModuleButtons();
}

void cProTrackerFile::bindKeyboard()
{
    ip->clearAllButtons();
    ip->clearAllPots();
    ip->clearGlobalPads();

    ip->setButtonObj(interfaceButton0, buttonPress, functKeyConfirm);
    ip->setButtonObj(interfaceButton1, buttonPress, functAutoName);
    ip->setButtonObj(interfaceButton6, buttonPress, functCancel);
    ip->setButtonObj(interfaceButton7, buttonPress, []() { F->actKeyboardConfirm(); });
    ip->setButtonObj(interfaceButtonInsert, buttonPress, functKeyConfirm);
    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functRight);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDown);
    ip->setButtonObj(interfaceButtonDelete, functDelete);
    ip->setPadsGlobal(functPads);
}

void cProTrackerFile::bindConfirm()
{
    ip->clearAllButtons();
    ip->clearAllPots();
    ip->clearGlobalPads();

    ip->setButtonObj(interfaceButton5, buttonPress, functConfirmCancel);
    ip->setButtonObj(interfaceButton6, buttonPress, functConfirmNo);
    ip->setButtonObj(interfaceButton7, buttonPress, functConfirmYes);
}

// ---------------------------------------------------------------------------------------------
// browser
// ---------------------------------------------------------------------------------------------
void cProTrackerFile::openBrowser(Screen target)
{
    FileSystem* fs = platform->getFileSystem();
    if (!fs->isSdCardInserted())
    {
        message("No SD card");
        return;
    }

    if (!fs->exists(kSongFolder))
        fs->mkdir(0, kSongFolder);

    if (!browserReady)
    {
        fileBrowserData.mode = kModBrowser;
        fileBrowserData.defaultPath = kSongFolder;
        snprintf(fileBrowserData.currentPath, sizeof(fileBrowserData.currentPath), "%s", kSongFolder);
        fileBrowserData.browserListPosition = 0;
        browserReady = true;
    }

    fileBrowserData.maxBrowserListLen = kMaxFilesListLength;
    fileBrowserData.maxFileNameLen = kMaxFileNameLen;
    fileBrowserData.fileNamesBufferSize = sizeof(s_names);
    fileBrowserData.fileNamesBuffer = s_names;
    fileBrowserData.selectionEnabled = true;
    fileBrowserData.browserList = s_list;
    fileBrowserData.selectionList = s_selection;

    fileBrowser.start(fs, &fileBrowserData);

    screen = target;
    showBrowser();
    bindBrowser();
}

void cProTrackerFile::closeBrowser()
{
    showMain();
    bindMain();
}

// Enter goes into the selected folder (or up with "/..")
bool cProTrackerFile::browserEnter()
{
    if (fileBrowser.isPositionOnSelectionList())
        return false;

    fileBrowser.browse();
    showBrowser();
    return true;
}

void cProTrackerFile::moveList(int delta)
{
    delta = clamp<int>(delta, -5, 5);
    fileBrowserData.browserListPosition = clamp<int>(
      fileBrowserData.browserListPosition + delta, 0, fileBrowserData.browserListLength - 1);
    refreshBrowser();
}

// ---------------------------------------------------------------------------------------------
// actions
// ---------------------------------------------------------------------------------------------
static void goToPatternScreen()
{
    uint8_t button = interfaceButtonPattern;
    F->eventFunct(eventSwitchModule, F, &button, 0);
}

static void doNewSong()
{
    PT->newSong();
    F->message("New song");
    F->showMain();
    F->bindMain();
}

static void doLoadSelected()
{
    FileBrowser& b = F->fileBrowser;
    b.getSelectedItemWithPath(s_path, sizeof(s_path));

    const char* error = nullptr;
    if (PT->loadFile(s_path, &error))
    {
        goToPatternScreen();
    }
    else
    {
        F->message(error != nullptr ? error : "Cannot load this file");
        F->showBrowser(false);
        F->bindBrowser();
    }
}

static void doSaveTo(const char* path)
{
    const char* error = nullptr;
    if (PT->saveFile(path, &error))
    {
        F->message("Song saved");
        if (F->pending != cProTrackerFile::kNothing && F->pendingAfterSave)
        {
            F->pendingAfterSave = false;
            F->runPending();
            return;
        }
        F->pending = cProTrackerFile::kNothing;
        F->showMain();
        F->bindMain();
    }
    else
    {
        F->message(error != nullptr ? error : "Cannot save the song");
    }
}

void cProTrackerFile::runPending()
{
    const Pending what = pending;
    pending = kNothing;

    switch (what)
    {
        case kNewSong: doNewSong(); break;
        case kLoadFile: doLoadSelected(); break;
        default: break;
    }
}

void cProTrackerFile::actNew()
{
    if (PT->dirty)
    {
        pending = kNewSong;
        songName(s_info, sizeof(s_info));
        static char quoted[64];
        snprintf(quoted, sizeof(quoted), "\"%s\"?", s_info);
        showConfirm("Do you want to save the changes to ", quoted, "Save", true);
        bindConfirm();
        return;
    }
    doNewSong();
}

// Clear Song: patterns and order list go, the samples stay
void cProTrackerFile::actClearSong()
{
    pending = kClearSong;
    showConfirm("Clear all patterns and the order list?", "The samples are kept.", "Clear", false);
    bindConfirm();
}

void cProTrackerFile::actOpen()
{
    openBrowser(kScreenOpen);
}

void cProTrackerFile::actSave()
{
    if (PT->filePath[0] == '\0')
    {
        actSaveAs();
        return;
    }
    pending = kNothing;
    doSaveTo(PT->filePath);
}

void cProTrackerFile::actSaveAs()
{
    openBrowser(kScreenSave);
}

void cProTrackerFile::actLoadSelected()
{
    if (!fileBrowser.isPositionOnSelectionList())
        return;

    if (!hasModExtension(fileBrowser.getSelectedItemName()))
    {
        message("Only .mod files can be opened");
        return;
    }

    if (PT->dirty)
    {
        pending = kLoadFile;
        songName(s_info, sizeof(s_info));
        static char quoted[64];
        snprintf(quoted, sizeof(quoted), "\"%s\"?", s_info);
        showConfirm("Do you want to save the changes to ", quoted, "Save", true);
        bindConfirm();
        return;
    }

    doLoadSelected();
}

void cProTrackerFile::actDeleteSelected()
{
    if (!fileBrowser.isPositionOnSelectionList())
        return;

    pending = kDeleteFile;
    static char quoted[96];
    snprintf(quoted, sizeof(quoted), "\"%s\"?", fileBrowser.getSelectedItemName());
    showConfirm("Do you want to delete file:", quoted, "Delete", false);
    bindConfirm();
}

// Save: the name was typed (or a folder name for New Folder)
void cProTrackerFile::actKeyboardConfirm()
{
    char name[MAX_NAME_LENGTH];
    snprintf(name, sizeof(name), "%s", keyboardManager.getName());
    size_t len = strlen(name);
    while (len > 0 && name[len - 1] == ' ')
        name[--len] = '\0';
    if (len == 0)
        return;

    if (keyboardUse == kNameFolder)
    {
        fileBrowser.createFolderInCurrentPath(name);
        keyboardManager.deactivateKeyboard();
        fileBrowser.refresh();
        fileBrowser.selectSpecificFolderName(name);
        screen = kScreenSave;
        showBrowser();
        bindBrowser();
        return;
    }

    if (len < 4 || strcasecmp(name + len - 4, ".mod") != 0)
        snprintf(name + len, sizeof(name) - len, ".mod");

    snprintf(s_path, sizeof(s_path), "%s/%s", fileBrowser.getCurrentPath(), name);

    FileSystem* fs = platform->getFileSystem();
    if (fs->exists(s_path) && strcmp(s_path, PT->filePath) != 0)
    {
        keyboardManager.deactivateKeyboard();
        const Pending keep = pending;
        pending = kOverwrite;
        pendingAfterSave = keep != kNothing && keep != kOverwrite && keep != kDeleteFile;
        static char quoted[64];
        snprintf(quoted, sizeof(quoted), "\"%s\"?", name);
        screen = kScreenSave;
        showConfirm("Do you want overwrite ", quoted, "Overwrite", false);
        bindConfirm();
        return;
    }

    keyboardManager.deactivateKeyboard();
    doSaveTo(s_path);
}

void cProTrackerFile::actConfirmYes()
{
    switch (pending)
    {
        case kOverwrite:
            mtPopups.hideInfoPopup();
            doSaveTo(s_path);
            break;

        case kDeleteFile:
        {
            mtPopups.hideInfoPopup();
            pending = kNothing;
            char path[kMaxFilePathLen + 1];
            fileBrowser.getSelectedItemWithPath(path, sizeof(path));
            platform->getFileSystem()->remove(path);
            fileBrowser.refresh();
            fileBrowserData.browserListPosition = clamp<int>(
              fileBrowserData.browserListPosition, 0, fileBrowserData.browserListLength - 1);
            screen = s_returnTo;
            showBrowser();
            bindBrowser();
            break;
        }

        case kClearSong:
            mtPopups.hideInfoPopup();
            pending = kNothing;
            PT->clearSong();
            message("Song cleared, samples kept");
            showMain();
            bindMain();
            break;

        case kNewSong:
        case kLoadFile:
        {
            // "Save": save to the current file, or ask for a name first
            mtPopups.hideInfoPopup();
            pendingAfterSave = true;
            if (PT->filePath[0] != '\0')
            {
                doSaveTo(PT->filePath);
            }
            else
            {
                openBrowser(kScreenSave);
            }
            break;
        }

        default: break;
    }
}

// "Don't Save": go on without saving
void cProTrackerFile::actConfirmNo()
{
    mtPopups.hideInfoPopup();

    if (pending == kNewSong || pending == kLoadFile)
    {
        PT->dirty = false;
        runPending();
        return;
    }

    actConfirmCancel();
}

void cProTrackerFile::actConfirmCancel()
{
    mtPopups.hideInfoPopup();
    pending = kNothing;
    pendingAfterSave = false;

    if (s_returnTo == kScreenOpen || s_returnTo == kScreenSave)
    {
        screen = s_returnTo;
        showBrowser(false);
        bindBrowser();
    }
    else
    {
        showMain();
        bindMain();
    }
}

// ---------------------------------------------------------------------------------------------
// input
// ---------------------------------------------------------------------------------------------
static void functSwitchModule(uint8_t button)
{
    F->eventFunct(eventSwitchModule, F, &button, 0);
}

static void functClearSong()
{
    F->actClearSong();
}

static void functNew()
{
    F->actNew();
}

static void functOpen()
{
    F->actOpen();
}

static void functSave()
{
    F->actSave();
}

// Export WAV: the "please wait" text is drawn first, then update() renders in slices so that the
// screen keeps refreshing and the percentage moves.
void cProTrackerFile::actExportWav()
{
    exportStage = 1;
    exportTimer = 0;
    exportShown = -1;
    ip->clearAllButtons();
    mtPopups.showInfoPopup("Please wait while exporting...", "0%");
    display.synchronizeRefresh();
}

void cProTrackerFile::finishExport(const char* text)
{
    exportStage = 0;
    mtPopups.hideInfoPopup();
    bindMain();
    setSongNameOnTitle();
    message(text);
}

void cProTrackerFile::update()
{
    if (exportStage == 0)
        return;

    static char path[kMaxFilePathLen + 1];
    static char percentText[8];
    const char* error = nullptr;

    if (exportStage == 1)
    {
        if (exportTimer < 250) // let the text reach the screen before the work starts
            return;
        if (!PT->exportBegin(path, sizeof(path), &error))
        {
            finishExport(error != nullptr ? error : "Cannot export the WAV file");
            return;
        }
        exportStage = 2;
        return;
    }

    const int result = PT->exportStep(40, &error);
    if (result == 0)
    {
        if (PT->exportPercent() != exportShown)
        {
            exportShown = PT->exportPercent();
            snprintf(percentText, sizeof(percentText), "%d%%", exportShown);
            mtPopups.showInfoPopup("Please wait while exporting...", percentText);
        }
        return;
    }

    finishExport(result > 0 ? "WAV exported to /Mod/Export"
                            : (error != nullptr ? error : "Cannot export the WAV file"));
}

static void functExportWav()
{
    F->actExportWav();
}

static void functSaveAs()
{
    F->actSaveAs();
}

static void functEnter()
{
    F->browserEnter();
}

static void functCancel()
{
    if (F->screen == cProTrackerFile::kScreenKeyboard)
    {
        F->keyboardManager.deactivateKeyboard();
        F->screen = cProTrackerFile::kScreenSave;
        F->pending = cProTrackerFile::kNothing;
        F->pendingAfterSave = false;
        F->showBrowser(false);
        F->bindBrowser();
        return;
    }

    F->pending = cProTrackerFile::kNothing;
    F->pendingAfterSave = false;
    F->closeBrowser();
}

// Open: load the selected song. Save: ask for a name.
static void functBrowserAccept()
{
    if (F->screen == cProTrackerFile::kScreenOpen)
    {
        F->actLoadSelected();
        return;
    }

    F->keyboardUse = cProTrackerFile::kNameFile;
    char name[40];
    songName(name, sizeof(name));
    F->keyboardManager.fillName(name);
    F->keyboardManager.activateKeyboard(28);
    F->showKeyboard("Save");
    F->bindKeyboard();
}

static void functNewFolder()
{
    F->keyboardUse = cProTrackerFile::kNameFolder;
    F->keyboardManager.fillName("New folder");
    F->keyboardManager.activateKeyboard(28);
    F->showKeyboard("Create");
    F->bindKeyboard();
}

static void functDelete(uint8_t state)
{
    if (F->keyboardManager.getState())
    {
        if (state == buttonPress || state == buttonHold)
            F->keyboardManager.makeBackspace();
        return;
    }

    if (state == buttonPress)
        F->actDeleteSelected();
}

static void functLeft()
{
    if (F->keyboardManager.getState())
    {
        F->keyboardManager.makeMove('a');
        return;
    }
    F->fileBrowserData.browserListPosition = 0;
    F->refreshBrowser();
}

static void functRight()
{
    if (F->keyboardManager.getState())
        F->keyboardManager.makeMove('d');
}

static void functUp()
{
    if (F->keyboardManager.getState())
    {
        F->keyboardManager.makeMove('w');
        return;
    }
    F->moveList(-1);
}

static void functDown()
{
    if (F->keyboardManager.getState())
    {
        F->keyboardManager.makeMove('s');
        return;
    }
    F->moveList(1);
}

static void functEncoder(int16_t value)
{
    F->moveList(interfaceGlobals.invertValForList ? -value : value);
}

static void functMiniMiddle(uint8_t state)
{
    if (state != buttonPress)
        return;

    if (!F->browserEnter())
        functBrowserAccept();
}

static void functPads(uint8_t pad, uint8_t state, int16_t)
{
    if (F->keyboardManager.getState())
        F->keyboardManager.onPadChange(pad, state);
}

static void functKeyConfirm()
{
    if (F->keyboardManager.getState())
        F->keyboardManager.confirmKey();
}

static void functAutoName()
{
    F->keyboardManager.setRandomName();
}

static void functConfirmYes()
{
    F->actConfirmYes();
}

static void functConfirmNo()
{
    F->actConfirmNo();
}

static void functConfirmCancel()
{
    F->actConfirmCancel();
}

#endif // PLATFORM_IMX
