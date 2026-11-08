#ifdef PLATFORM_IMX

#include "protrackerMode/ptFile.h"

#include "displayStructs.h"

// Controls of the file screen, created like the stock file screen does it.

void cProTrackerFile::initDisplayControls()
{
    strControlProperties prop2;
    for (uint8_t i = 0; i < 8; i++)
    {
        prop2.value = 1;
        prop2.colors = display.getTheme().activeButtonLabelsColors;
        prop2.style = controlStyleCenterX | controlStyleCenterY | controlStyleFont3;
        prop2.x = (800 / 8) * i + (800 / 16);
        prop2.w = 800 / 8 - 6;
        prop2.y = 424;
        prop2.h = 55;
        label[i] = display.createControl<cLabel>(&prop2);
    }

    prop2.text = nullptr;
    prop2.colors = nullptr;
    prop2.value = 255;
    prop2.style = controlStyleNoTransparency | controlStyleShow;
    prop2.x = 0;
    prop2.w = 800;
    prop2.y = 425;
    prop2.h = 54;
    bgLabel = display.createControl<cBgLabel>(&prop2);

    prop2.value = 0;
    prop2.colors = display.getTheme().topBarLabelColors;
    prop2.style =
      controlStyleBackground | controlStyleCenterY | controlStyleCenterX | controlStyleFont4;
    prop2.x = (800 / 16) * 3;
    prop2.w = (800 / 8) * 3 - 6;
    prop2.y = 32;
    prop2.h = 26;
    browserLabelControl = display.createControl<cLabel>(&prop2);

    strControlProperties prop;
    projectList.linesCount = 13;
    projectList.start = 0;
    projectList.length = 0;
    prop.x = 1;
    prop.y = 29 + 32;
    prop.w = (800 / 8 * 3) - 3;
    prop.h = 394 - 32;
    prop.value = 0;
    prop.style = controlStyleBackground |
                 (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    prop.data = &projectList;
    prop.colors = display.getTheme().projectActiveListColors;
    fileListControl = display.createControl<cList>(&prop);

    strControlProperties cover;
    cover.value = 0;
    cover.data = nullptr;
    cover.text = nullptr;
    cover.colors = display.getTheme().blankSpaceCoverLabelColors;
    cover.style = controlStyleBackground |
                  (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    cover.x = (800 / 8) * 7 + 1;
    cover.w = 800 / 8 - 2;
    cover.y = 29;
    cover.h = 394;
    blankSpaceCover = display.createControl<cLabel>(&cover);
}

#endif // PLATFORM_IMX
