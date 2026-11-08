#ifdef PLATFORM_IMX

#include "protrackerMode/protrackerMode.h"

#include "displayStructs.h"

// Layout: title bar (0..29), info line, then the 4 channels as text boxes (one box per channel, so
// the columns line up regardless of font metrics), then the 8 function labels at the bottom.
static constexpr int kInfoY = 32;
static constexpr int kGutterX = 4;
static constexpr int kGutterW = 52;
static constexpr int kChannelX0 = 62;
static constexpr int kChannelW = 182;
static constexpr int kLabelsY = 424;

void cProTrackerMode::initDisplayControls()
{
    const strFont* font = &fonts[((controlStyleFont2 >> 8) & 15) - 1];
    lineStep = font->height + 4;
    boxTop = 58;

    // keep the pattern area above the function labels and always use an odd number of lines
    rowsVisible = (kLabelsY - 6 - boxTop) / lineStep - 1;
    if (rowsVisible > kMaxVisibleRows)
        rowsVisible = kMaxVisibleRows;
    if ((rowsVisible & 1) == 0)
        rowsVisible--;

    strControlProperties prop;

    // cursor bar first, so that the text is drawn on top of it
    prop.colors = display.getTheme().activeLabelsColors;
    prop.text = (char*)"";
    prop.style = controlStyleShow | controlStyleBackground;
    prop.x = kChannelX0;
    prop.y = boxTop + 5 + (rowsVisible / 2) * lineStep - 2;
    prop.w = kChannelW;
    prop.h = lineStep;
    prop.value = 0;
    cursorBar = display.createControl<cLabel>(&prop);

    // gutter (row numbers) and the four channels
    strControlProperties box;
    box.colors = display.getTheme().configItemEditorTextLabelColors;
    box.text = nullptr;
    box.data = nullptr;
    box.style = controlStyleShow | controlStyleFont2;
    box.value = 4; // extra pixels between lines
    box.y = boxTop;
    box.h = (rowsVisible + 1) * lineStep;

    box.x = kGutterX;
    box.w = kGutterW;
    gutterBox = display.createControl<cTextBox>(&box);

    for (int ch = 0; ch < protracker::kChannelCount; ch++)
    {
        box.x = kChannelX0 + ch * kChannelW;
        box.w = kChannelW - 4;
        channelBox[ch] = display.createControl<cTextBox>(&box);
    }

    // file list (shown instead of the pattern while browsing)
    box.x = 20;
    box.w = 760;
    box.style = controlStyleFont2; // hidden until needed
    listBox = display.createControl<cTextBox>(&box);

    // info line
    strControlProperties info;
    info.colors = display.getTheme().activeLabelsColors;
    info.text = (char*)"";
    info.style = controlStyleShow | controlStyleFont1;
    info.x = 8;
    info.y = kInfoY - 6;
    info.w = 784;
    info.h = 24;
    info.value = 0;
    infoLabel = display.createControl<cLabel>(&info);

    // function labels
    for (uint8_t i = 0; i < 8; i++)
    {
        prop.colors = display.getTheme().activeLabelsColors;
        prop.text = (char*)"";
        prop.style =
          (controlStyleShow | controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
        prop.x = (800 / 8) * i + (800 / 16);
        prop.y = kLabelsY;
        prop.w = 800 / 8 - 6;
        prop.h = 55;
        prop.value = 0;
        label[i] = display.createControl<cLabel>(&prop);
    }

    strControlProperties bg;
    bg.text = nullptr;
    bg.colors = nullptr;
    bg.value = 255;
    bg.style = controlStyleNoTransparency | controlStyleShow;
    bg.x = 0;
    bg.w = 800;
    bg.y = kLabelsY + 1;
    bg.h = 54;
    bgLabel = display.createControl<cBgLabel>(&bg);
}

#endif // PLATFORM_IMX
