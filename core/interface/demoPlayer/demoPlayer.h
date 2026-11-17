#ifndef INTERFACE_DEMOPLAYER_H_
#define INTERFACE_DEMOPLAYER_H_

#ifdef PLATFORM_IMX

#include "core/modulesBase.h"
#include "display.h"
#include "list.h"

#include "../../../demo-sdk/tracker_demo.h"

// Runs demos (.ptdemo) from the SD card. The firmware only has this loader
// and the small host API in demo-sdk/tracker_demo.h; the effects live in the
// files.
//
// Config > Info > Demos opens a list of /Demos/*.ptdemo. Run starts the
// picked demo full screen; from then on only Shift does anything, and it
// returns to Config. Demos with PTDEMO_FLAG_INPUT get all input instead and
// are left with Shift+Pattern.
class cDemoPlayer : public ModuleBase
{
  public:
    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();
    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    cDemoPlayer() {}
    ~cDemoPlayer() {}

    // Switches from `caller` (the Config screen) to the demo list.
    void open(ModuleBase* caller);

    void moveList(int delta);
    void runSelected();
    void leave();
    // Power-off: runs the demo's demo_exit (so it can save) and leaves.
    void endForShutdown();

    // Relative paths in host->read_file resolve against this folder.
    char demoFolder[96] = "/Demos";

  private:
    void scanDemos();
    void showList();
    bool load(const char* path, const char** error);
    void beginDemo();
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

    // ---- loaded demo
    PtDemoHeader header{};
    PtDemoInitFunc initFunc = nullptr;
    PtDemoFrameFunc frameFunc = nullptr;
    PtDemoExitFunc exitFunc = nullptr;
    bool running = false;
    bool initFailed = false;
    uint32_t startMs = 0;
};

extern cDemoPlayer demoPlayer;

#endif // PLATFORM_IMX

#endif /* INTERFACE_DEMOPLAYER_H_ */
