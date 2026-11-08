#ifdef PLATFORM_IMX

#include "protrackerMode/ptSong.h"

#include "Platform.h"
#include "displayStructs.h"

// Controls of the song screen, created the way the stock song editor does it.

static const uint16_t framesPlaces[3][4] = {
    { (800 / 8) * 0 + 1, 31, 500 - 3, 391 },
    { (800 / 8) * 5 + 1, 31, 800 / 4 - 3, 391 },
    { (800 / 8) * 7 + 1, 31, 800 / 8 - 3, 391 },
};

void cProTrackerSong::initDisplayControls()
{
    strControlProperties prop;
    frameData.placesCount = 3;
    frameData.startPlace = 2;
    frameData.places[0] = &framesPlaces[0][0];
    frameData.places[1] = &framesPlaces[1][0];
    frameData.places[2] = &framesPlaces[2][0];

    prop.style = 0;
    prop.value = 0;
    prop.colors = nullptr;
    prop.data = &frameData;
    frameControl = display.createControl<cFrame>(&prop);

    strControlProperties prop1;
    prop1.y = 29;
    prop1.w = 800 / 8 - 3;
    prop1.style = controlStyleValue_0_100 | controlStyleBackground |
                  (display.getTheme().barShadow > 0 ? controlStyleGradient : 0) |
                  (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    prop1.h = 394;
    prop1.x = (800 / 8) * 7 + 1;
    barControl = display.createControl<cBar>(&prop1);

    doubleArrowRed.bitmaps[0].bitmapIndex = displayDoubleArrowL;
    doubleArrowRed.bitmaps[0].xValue = (800 / 8) * 5 + (800 / 16) - 10;
    doubleArrowRed.bitmaps[0].yValue = 445;
    doubleArrowRed.bitmaps[1].bitmapIndex = displayDoubleArrowR;
    doubleArrowRed.bitmaps[1].xValue = (800 / 8) * 6 + (800 / 16) + 10;
    doubleArrowRed.bitmaps[1].yValue = 445;
    doubleArrowRed.line.color = display.getTheme().red;
    doubleArrowRed.line.x1 = -90;
    doubleArrowRed.line.y1 = 50;
    doubleArrowRed.line.x2 = 90;
    doubleArrowRed.line.y2 = 50;

    labelDataRed.line.color = display.getTheme().red;
    labelDataRed.line.x1 = -40;
    labelDataRed.line.y1 = 50;
    labelDataRed.line.x2 = 40;
    labelDataRed.line.y2 = 50;

    strControlProperties prop2;
    prop2.text = (char*)"";
    prop2.colors = display.getTheme().activeButtonLabelsColors;
    prop2.w = 800 / 8 - 6;
    prop2.y = 424;
    prop2.h = 55;
    prop2.value = 1;

    for (uint8_t i = 0; i < 8; i++)
    {
        prop2.style = controlStyleCenterX | controlStyleCenterY | controlStyleFont3 |
                      ((display.getTheme().patternEditorBottomButtonsLines && (i >= 2 && i <= 5)
                          ? controlStyleSpecialFx
                          : 0));
        prop2.x = (800 / 8) * i + (800 / 16);
        if (i == 5)
            prop2.x = (800 / 8) * i + (800 / 8);

        label[i] = display.createControl<cLabel>(&prop2);
    }

    prop2.value = 191;
    prop2.text = nullptr;
    prop2.colors = nullptr;
    prop2.style = controlStyleNoTransparency;
    prop2.x = 0;
    prop2.w = 800;
    prop2.y = 425;
    prop2.h = 54;
    bgLabel = display.createControl<cBgLabel>(&prop2);

    prop.data = nullptr;
    prop.x = 0 + 1;
    prop.y = 29;
    prop.w = ((800 / 8) * 7) - 3;
    prop.h = 394;
    prop.style = controlStyleBackground | controlStyleVerticalText |
                 (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    songPlayerControl = display.createControl<cSongPlayer>(&prop);
}

#endif // PLATFORM_IMX
