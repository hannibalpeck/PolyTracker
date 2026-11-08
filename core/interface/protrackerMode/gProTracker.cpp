#ifdef PLATFORM_IMX

#include "protrackerMode/protrackerMode.h"

#include "displayStructs.h"

// Controls of the pattern screen, created the same way as the stock pattern editor does: the pattern
// grid, the frame that marks a held bottom button, the eight bottom labels and their background.

static const uint16_t framesPlaces[8][4] = {
    { 0 + 1, 423, 800 / 8 - 1, 55 },
    { (800 / 8) * 1 + 1, 423, 800 / 8 - 1, 55 },
    { (800 / 8) * 2 + 1, 423, 800 / 8 - 1, 55 },
    { (800 / 8) * 3 + 1, 423, 800 / 8 - 1, 55 },
    { (800 / 8) * 4 + 1, 423, 800 / 8 - 1, 55 },
    { (800 / 8) * 5 + 1, 423, 800 / 8 - 1, 55 },
    { (800 / 8) * 6 + 1, 423, 800 / 8 - 1, 55 },
    { (800 / 8) * 7 + 1, 423, 800 / 8 - 1, 55 },
};

strTrackerPattern cProTrackerMode::trackerPattern;

void cProTrackerMode::initDisplayControls()
{
    strControlProperties prop;

    frameData.placesCount = 8;
    frameData.startPlace = 0;
    for (int i = 0; i < 8; i++)
        frameData.places[i] = &framesPlaces[i][0];
    prop.x = 0;
    prop.y = 0;
    prop.w = 0;
    prop.h = 0;
    prop.style = 0;
    prop.value = 0;
    prop.data = &frameData;
    frameControl = display.createControl<cFrame>(&prop);

    strControlProperties prop2;
    for (uint8_t i = 0; i < 8; i++)
    {
        prop2.text = (char*)"";
        prop2.colors = display.getTheme().activeLabelsColors;
        prop2.style = (controlStyleCenterX | controlStyleFont3 | controlStyleCenterY);
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

    labelDataRed.line.color = display.getTheme().red;
    labelDataRed.line.x1 = -40;
    labelDataRed.line.y1 = 50;
    labelDataRed.line.x2 = 40;
    labelDataRed.line.y2 = 50;

    labelDataGreen.line.color = display.getTheme().green;
    labelDataGreen.line.x1 = -40;
    labelDataGreen.line.y1 = 50;
    labelDataGreen.line.x2 = 40;
    labelDataGreen.line.y2 = 50;

    labelDataMore.dotIndicator.count = 2;
    labelDataMore.dotIndicator.current = 0;

    // the pattern grid (four tracks, fifteen rows, cursor in the middle row)
    const uint32_t style =
      controlStyleFont4 | (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0) |
      (display.getTheme().patternEditorPlayheadShadow > 0 ? controlStyleGradient : 0);

    strControlProperties grid;
    grid.style = style;
    grid.x = 0;
    grid.y = 28;
    grid.w = 800;
    grid.h = 395;
    grid.colors = display.getTheme().patternTrackerColors;
    grid.data = &trackerPattern;
    grid.value = 0; // 4 tracks with note, instrument and effect
    patternControl = display.createControl<cTracker>(&grid);

    // the file list (shown instead of the grid while browsing)
    strControlProperties box;
    box.colors = display.getTheme().configItemEditorTextLabelColors;
    box.text = nullptr;
    box.data = nullptr;
    box.style = controlStyleFont2; // hidden until needed
    box.value = 4;
    box.x = 20;
    box.y = 58;
    box.w = 760;
    box.h = (kVisibleFileRows + 1) * 26;
    listBox = display.createControl<cTextBox>(&box);

    // one line of text under the title bar for messages and the file list header
    strControlProperties info;
    info.colors = display.getTheme().activeLabelsColors;
    info.text = (char*)"";
    info.style = controlStyleFont1; // hidden until needed
    info.x = 8;
    info.y = 30;
    info.w = 784;
    info.h = 24;
    info.value = 0;
    infoLabel = display.createControl<cLabel>(&info);
}

#endif // PLATFORM_IMX
