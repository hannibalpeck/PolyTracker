#ifndef INTERFACE_EXTRAPLAYER_H_
#define INTERFACE_EXTRAPLAYER_H_

#ifdef PLATFORM_IMX

#include "core/modulesBase.h"
#include "display.h"
#include "list.h"

#include "../../../extras-sdk/tracker_extra.h"

// Runs extras (.ptextra) from the SD card. The firmware only has this loader
// and the small host API in extras-sdk/tracker_extra.h; the effects live in the
// files.
//
// Config > Extras opens a list of /Extras/*.ptextra. Run starts the
// picked extra full screen; from then on only Shift does anything, and it
// returns to Config. Extras with PTEXTRA_FLAG_INPUT get all input instead and
// are left with Shift+Pattern.
class cExtrasPlayer : public ModuleBase
{
  public:
    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();
    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    cExtrasPlayer() {}
    ~cExtrasPlayer() {}

    // Switches from `caller` (the Config screen) to the extra list.
    void open(ModuleBase* caller);

    void moveList(int delta);
    void runSelected();
    void leave();
    // Power-off: runs the extra's extra_exit (so it can save) and leaves.
    void endForShutdown();

    // Relative paths in host->read_file resolve against this folder.
    char extraFolder[96] = "/Extras";

  private:
    void scanExtras();
    void showList();
    bool load(const char* path, const char** error);
    void beginExtra();
    void renderFrame();

    // ---- list screen
    hControl label[8] = {};
    hControl bgLabel = -1;
    hControl titleLabel = -1;
    hControl listControl = -1;
    hControl coverLabel = -1;
    strList list = {};
    int listCount = 0;
    int listPos = 0;

    // ---- loaded extra
    PtExtraHeader header{};
    PtExtraInitFunc initFunc = nullptr;
    PtExtraFrameFunc frameFunc = nullptr;
    PtExtraExitFunc exitFunc = nullptr;
    bool running = false;
    bool initFailed = false;
    uint32_t startMs = 0;
};

extern cExtrasPlayer extrasPlayer;

#endif // PLATFORM_IMX

#endif /* INTERFACE_EXTRAPLAYER_H_ */
