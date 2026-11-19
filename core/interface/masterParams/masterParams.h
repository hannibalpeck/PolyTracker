#ifndef INTERFACE_MASTERPARAMS_H_
#define INTERFACE_MASTERPARAMS_H_

#include "core/modulesBase.h"
#include "mtKeyboardManager.h"

#include "mtStructs.h"

// Master screen: only the master (headphone/line out) volume.
class cMasterParams : public ModuleBase
{

  public:
    virtual void update() {}
    virtual void start(uint32_t options);
    virtual void stop();

    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    cMasterParams() {}
    virtual ~cMasterParams() {}

    void showMasterScreen();
    void showVolume();
    void switchToMaster();
    void setMasterScreenFunct();
    void lightUpPadBoard();

    uint8_t sourceModuleButton = 0;
    bool showMasterVolumeOnHold = false;

    strFrameData frameData;

    uint32_t levelBarColors[5] = { 0, 0, 0, 0, 0 };
    strBarData barData;

    hControl label;
    hControl bgLabel;
    hControl barControl;
    hControl blankSpaceCover;
    hControl frameControl;

    char volumeVal[12];
};

extern cMasterParams masterParams;

#endif /* INTERFACE_MASTERPARAMS_H_ */
