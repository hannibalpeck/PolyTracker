#include "ampLogarythmicValues.h"
#include "mtMasterEnhancer.h"
#include "util_math.h"
#include <cmath>
#include <masterParams/masterParams.h>

static const uint16_t framePlace[4] = { 0 + 1, 31, 800 / 8 - 3, 391 };

void cMasterParams::initDisplayControls()
{
    for (uint8_t i = 0; i < 5; i++)
    {
        levelBarColors[i] = display.getTheme().activeBarColors[i];
    }

    strControlProperties prop;
    prop.style = 0;
    prop.value = 0;
    prop.data = &frameData;
    frameControl = display.createControl<cFrame>(&prop);

    strControlProperties prop2;

    prop2.value = 1;
    prop2.colors = display.getTheme().activeLabelsColors;
    prop2.style = (controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
    prop2.x = 800 / 16;
    prop2.w = 800 / 8 - 6;
    prop2.y = 424;
    prop2.h = 55;
    label = display.createControl<cLabel>(&prop2);

    prop2.text = nullptr;
    prop2.colors = nullptr;
    prop2.value = 255;
    prop2.style = controlStyleNoTransparency | controlStyleShow;
    prop2.x = 0;
    prop2.w = 800;
    prop2.y = 425;
    prop2.h = 54;
    bgLabel = display.createControl<cBgLabel>(&prop2);

    barData.changingColor = 1;
    prop2.data = &barData;
    prop2.colors = levelBarColors;
    prop2.value = 0;
    prop2.x = 2;
    prop2.y = 29;
    prop2.w = 800 / 8 - 3;
    prop2.style = controlStyleValue_0_100 | controlStyleBackground |
                  (display.getTheme().barShadow > 0 ? controlStyleGradient : 0) |
                  (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    prop2.h = 394;
    barControl = display.createControl<cBar>(&prop2);

    prop2.value = 0;
    prop2.data = nullptr;
    prop2.text = nullptr;
    prop2.colors = display.getTheme().blankSpaceCoverLabelColors;
    prop2.style = controlStyleBackground |
                  (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    prop2.x = (800 / 8) * 1 + 2;
    prop2.w = (800 / 8) * 7 - 4;
    prop2.y = 29;
    prop2.h = 394;
    blankSpaceCover = display.createControl<cLabel>(&prop2);
}

void cMasterParams::showMasterScreen()
{
    barData.value = display.getTheme().white;

    titleBar->show();
    titleBar->setTitle("Master");

    display.setControlText(label, "Volume");
    display.setControlColors(label, display.getTheme().activeLabelsColors);
    display.setControlValue(label, 1);
    display.setControlStyle2(label, controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
    display.setControlPosition(label, 800 / 16, -1);
    display.setControlSize(label, 800 / 8 - 6, -1);
    display.setControlShow(label);
    display.refreshControl(label);

    display.setControlColors(barControl, display.getTheme().activeBarColors);
    display.setControlPosition(barControl, 2, 29);
    display.setControlSize(barControl, 800 / 8 - 3, 394);
    display.setControlShow(barControl);
    display.refreshControl(barControl);

    display.refreshControl(bgLabel);
    display.setControlValue(bgLabel, 255);

    showVolume();

    frameData.placesCount = 1;
    frameData.startPlace = 0;
    frameData.places[0] = &framePlace[0];

    display.setControlValue(frameControl, 0);
    display.setControlShow(frameControl);
    display.refreshControl(frameControl);

    // empty space over everything right of the volume bar
    display.setControlPosition(blankSpaceCover, (800 / 8) * 1 + 2, -1);
    display.setControlSize(blankSpaceCover, (800 / 8) * 7 - 4, -1);
    display.setControlShow(blankSpaceCover);
    display.refreshControl(blankSpaceCover);

    display.synchronizeRefresh();

    lightUpPadBoard();
}

void cMasterParams::showVolume()
{
    float masterVolumeInDecybels = map((float)(mtConfig.audioCodecConfig.volume / 100.0f),
                                       0.0f,
                                       1.0f,
                                       MIN_ANALOG_VOLUME_IN_DB,
                                       MAX_ANALOG_VOLUME_IN_DB);

    if (mtConfig.audioCodecConfig.volume == MASTER_VOLUME_DEFAULT)
    {
        masterVolumeInDecybels = 0;
    }

    sprintf(volumeVal, "%0.2f dB", masterVolumeInDecybels);

    display.setControlValue(barControl, mtConfig.audioCodecConfig.volume);
    display.refreshControl(barControl);

    display.setControlText2(label, volumeVal);
    display.refreshControl(label);
}
