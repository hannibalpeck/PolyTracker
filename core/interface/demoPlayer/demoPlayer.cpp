#ifdef PLATFORM_IMX

#include "demoPlayer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FT812.h"
#include "FT81x.h"
#include "displayStructs.h"
#include "mtAudioEngine.h"
#include "mtPadsBacklight.h"
#include "mtStructs.h"

#include "core/interface.h"
#include "core/interfaceDefs.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "core/titleBar.h"
#include "protrackerMode/protrackerMode.h"

#include "Platform.h"
#include "TrackerImpl.h"
#include "cr_section_macros.h"

cDemoPlayer demoPlayer;
static cDemoPlayer* DP = &demoPlayer;

extern uint8_t externalRamScratchBuff[]; // 768 KB, executable external RAM (MPU region 8)

//==============================================================================================
// What a demo gets
//==============================================================================================

// Shared scratch buffer, split like the stock .ptfx loader did it:
// [0 .. 128 KB) code + bss, [128 KB .. 512 KB) working RAM. The SD wav
// preview stream also uses the start of this buffer, so it is stopped first.
static constexpr uint32_t kCodeBufferSize = 128 * 1024;
static constexpr uint32_t kScratchOffset = kCodeBufferSize;
static constexpr uint32_t kScratchSize = 512 * 1024 - kScratchOffset;

// FT812 RAM_G: fonts and icons sit below 150000, the controls' display lists
// take 150000 + 65 * 8192 = 682480. Nothing in PolyTracker uses the rest
// (the old JPEG and video-stream areas are unused here).
static constexpr uint32_t kDemoRamgBase = 700000;
static constexpr uint32_t kDemoRamgSize = 1040000 - kDemoRamgBase;
static_assert(controlsRamStartAddress + controlsCount * controlsRamAddressStep <= kDemoRamgBase,
              "demo RAM_G area overlaps the controls' display lists");

// Handles 1-5 are the GUI fonts, 0 is borrowed by the controls, 15 is the
// coprocessor's scratch handle and 16-31 are the ROM fonts.
static constexpr uint8_t kFirstDemoHandle = 8;
static constexpr uint8_t kDemoHandleCount = 7;

static constexpr uint32_t kExitAbsent = 0xFFFFFFFFu;

// The frame goes through the coprocessor's 4 KB command FIFO. The words are
// sent in bursts and the FIFO is drained between them; the chip's display
// list holds 2048 words, so a demo gets a little under that.
static constexpr uint32_t kBurstWords = 768;
static constexpr uint32_t kMaxFrameWords = 2000;

static uint32_t s_burstWords = 0;
static uint32_t s_frameWords = 0;
static bool s_frameOpen = false;

static Platform* platformPtr()
{
    return TrackerImpl::getInstance()->getPlatform();
}

static DisplayDriver* driver()
{
    return display.getDisplayDriver();
}

static void flushBurstIfFull(uint32_t nextWords)
{
    if (s_burstWords + nextWords > kBurstWords)
    {
        driver()->endCoProList();
        driver()->beginCoProList(); // waits until the FIFO has been read
        s_burstWords = 0;
    }
}

//==============================================================================================
// Host API
//==============================================================================================

static void hostDl(uint32_t word)
{
    if (!s_frameOpen || s_frameWords >= kMaxFrameWords)
        return;
    // Words go through the coprocessor here: 0xFFxxxxxx would be read as a
    // coprocessor command, which no display-list word ever is.
    if ((word >> 24) == 0xFF)
        return;

    flushBurstIfFull(1);
    driver()->WriteData32(word);
    driver()->IncCMDOffset(4);
    s_burstWords++;
    s_frameWords++;
}

static void hostText(int16_t x, int16_t y, uint8_t font, uint16_t options, const char* s)
{
    if (!s_frameOpen || s == nullptr)
        return;

    size_t len = strlen(s);
    if (len > 120)
        len = 120;
    // A glyph costs the chip up to 2 list words; keep the frame in budget.
    const uint32_t listWords = 4 + 2 * (uint32_t)len;
    if (s_frameWords + listWords > kMaxFrameWords)
        return;

    char text[121];
    memcpy(text, s, len);
    text[len] = '\0';

    flushBurstIfFull(4 + (uint32_t)(len + 4) / 4);
    API_CMD_TEXT(x, y, font, options, text);
    s_burstWords += 4 + (uint32_t)(len + 4) / 4;
    s_frameWords += listWords;
}

static void hostRamgWrite(uint32_t addr, const void* data, uint32_t size)
{
    if (data == nullptr || size == 0 || addr < kDemoRamgBase ||
        addr + size > kDemoRamgBase + kDemoRamgSize || addr + size < addr)
    {
        return;
    }
    // The driver's write loop counts with 16 bits: send at most 32 KB a time.
    const uint8_t* src = static_cast<const uint8_t*>(data);
    while (size > 0)
    {
        uint32_t chunk = size > 32768 ? 32768 : size;
        driver()->writeDataRAMG(src, chunk, addr);
        src += chunk;
        addr += chunk;
        size -= chunk;
    }
}

static void hostRamgRead(uint32_t addr, void* data, uint32_t size)
{
    if (data == nullptr || size == 0 || size > 32768)
        return;
    driver()->readDataRAMG(static_cast<uint8_t*>(data), size, addr);
}

static uint32_t hostMillis()
{
    return platformPtr()->getTimer()->millis();
}

static uint32_t hostMicros()
{
    return platformPtr()->getTimer()->micros();
}

static float hostSinf(float x) { return sinf(x); }
static float hostCosf(float x) { return cosf(x); }
static float hostSqrtf(float x) { return sqrtf(x); }
static float hostFabsf(float x) { return fabsf(x); }
static float hostFmodf(float x, float y) { return fmodf(x, y); }
static float hostAtan2f(float y, float x) { return atan2f(y, x); }
static float hostPowf(float x, float y) { return powf(x, y); }
static float hostFloorf(float x) { return floorf(x); }
static void* hostMemcpy(void* d, const void* s, size_t n) { return memcpy(d, s, n); }
static void* hostMemset(void* d, int v, size_t n) { return memset(d, v, n); }
static int hostRand() { return rand(); }

static int32_t hostReadFile(const char* path, void* buffer, uint32_t maxBytes)
{
    FileSystem* fs = platformPtr()->getFileSystem();
    if (fs == nullptr || path == nullptr || buffer == nullptr || maxBytes == 0)
        return -1;

    char fullPath[kMaxFilePathLen + 1];
    if (path[0] == '/')
        snprintf(fullPath, sizeof(fullPath), "%s", path);
    else
        snprintf(fullPath, sizeof(fullPath), "%s/%s", DP->demoFolder, path);

    auto [isOpen, fileHandle] = fs->openFile(fullPath, FileSystem::FileOpenMode::kRead);
    if (!isOpen)
        return -1;

    // 256-byte reads: FatFs copies through its sector buffer with the CPU
    // instead of DMA-ing into cached external RAM.
    uint8_t* dst = static_cast<uint8_t*>(buffer);
    uint32_t total = 0;
    while (total < maxBytes)
    {
        uint32_t chunk = maxBytes - total;
        if (chunk > 256)
            chunk = 256;
        int32_t n = fs->readFile(dst + total, chunk, fileHandle);
        if (n <= 0)
            break;
        total += static_cast<uint32_t>(n);
        if (static_cast<uint32_t>(n) < chunk)
            break;
    }
    fs->closeFile(fileHandle);
    return static_cast<int32_t>(total);
}

static uint8_t s_padLevel[48];

static void hostPadLight(uint8_t pad, uint8_t level)
{
    if (pad >= 48 || s_padLevel[pad] == level)
        return;
    s_padLevel[pad] = level;

    if (level == 0)
    {
        padsBacklight.setFrontLayer(0, 0, pad);
        return;
    }
    const uint32_t scaled = (uint32_t)mtConfig.values.padsLightFront * level / 255u;
    padsBacklight.setFrontLayer(1, scaled > 0 ? (uint8_t)scaled : 1, pad);
}

static uint8_t hostIsSongPlaying()
{
    return protrackerMode.isPlaying() ? 1 : 0;
}

static const PtDemoHostApi s_hostApi = {
    PTDEMO_API_VERSION,
    800,
    480,
    1, // Tracker+ (IMX)
    kFirstDemoHandle,
    kDemoHandleCount,
    48,
    kDemoRamgBase,
    kDemoRamgSize,
    hostDl,
    hostText,
    hostRamgWrite,
    hostRamgRead,
    hostMillis,
    hostMicros,
    hostSinf,
    hostCosf,
    hostSqrtf,
    hostFabsf,
    hostFmodf,
    hostAtan2f,
    hostPowf,
    hostFloorf,
    hostMemcpy,
    hostMemset,
    hostRand,
    hostReadFile,
    hostPadLight,
    hostIsSongPlaying,
};

//==============================================================================================
// Loading
//==============================================================================================

// Clean the D-cache to RAM over the new code and invalidate the whole
// I-cache, so the M7 fetches the bytes just written (as the .ptfx loader did).
static void syncInstructionCache(void* addr, uint32_t size)
{
    volatile uint32_t* const scbDccimvac = reinterpret_cast<volatile uint32_t*>(0xE000EF70);
    volatile uint32_t* const scbIciallu = reinterpret_cast<volatile uint32_t*>(0xE000EF50);

    uintptr_t p = reinterpret_cast<uintptr_t>(addr) & ~static_cast<uintptr_t>(0x1F);
    const uintptr_t end = reinterpret_cast<uintptr_t>(addr) + size;

    asm volatile("dsb 0xf" ::: "memory");
    while (p < end)
    {
        *scbDccimvac = static_cast<uint32_t>(p);
        p += 32;
    }
    asm volatile("dsb 0xf" ::: "memory");
    *scbIciallu = 0;
    asm volatile("dsb 0xf" ::: "memory");
    asm volatile("isb 0xf" ::: "memory");
}

bool cDemoPlayer::load(const char* path, const char** error)
{
    initFunc = nullptr;
    frameFunc = nullptr;
    exitFunc = nullptr;

    FileSystem* fs = platformPtr()->getFileSystem();
    if (fs == nullptr || path == nullptr)
    {
        *error = "No SD card";
        return false;
    }

    auto [isOpen, fileHandle] = fs->openFile(path, FileSystem::FileOpenMode::kRead);
    if (!isOpen)
    {
        *error = "File not found";
        return false;
    }

    alignas(4) PtDemoHeader h;
    if (fs->readFile(&h, sizeof(h), fileHandle) != static_cast<int32_t>(sizeof(h)) ||
        h.magic != PTDEMO_MAGIC)
    {
        fs->closeFile(fileHandle);
        *error = "Not a .ptdemo file";
        return false;
    }

    const char* problem = nullptr;
    const uint32_t total = h.code_size + h.bss_size;
    if (h.format_version != PTDEMO_FORMAT_VERSION)
        problem = "Unsupported demo version";
    else if (h.code_size == 0 || total < h.code_size || total > kCodeBufferSize)
        problem = "Demo code too big";
    else if (h.scratch_size > kScratchSize)
        problem = "Demo needs too much RAM";
    else if (h.code_offset < sizeof(PtDemoHeader) || h.init_offset >= h.code_size ||
             h.frame_offset >= h.code_size ||
             (h.exit_offset != kExitAbsent && h.exit_offset >= h.code_size))
        problem = "Corrupt demo file";
    else if (!fs->seekFile(h.code_offset, fileHandle))
        problem = "Corrupt demo file";

    if (problem != nullptr)
    {
        fs->closeFile(fileHandle);
        *error = problem;
        return false;
    }

    // The code buffer overlaps the SD wav preview stream.
    engine.getSdPlayer()->stop();

    uint8_t* codeBuf = externalRamScratchBuff;
    uint32_t got = 0;
    while (got < h.code_size)
    {
        uint32_t chunk = h.code_size - got;
        if (chunk > 256)
            chunk = 256;
        if (fs->readFile(codeBuf + got, chunk, fileHandle) != static_cast<int32_t>(chunk))
            break;
        got += chunk;
    }
    fs->closeFile(fileHandle);

    if (got != h.code_size)
    {
        *error = "Read error";
        return false;
    }

    if (h.bss_size > 0)
        memset(codeBuf + h.code_size, 0, h.bss_size);
    if (h.scratch_size > 0)
        memset(externalRamScratchBuff + kScratchOffset, 0, h.scratch_size);

    syncInstructionCache(codeBuf, total);

    // Thumb entry points: base + offset with bit 0 set.
    const uintptr_t base = reinterpret_cast<uintptr_t>(codeBuf);
    initFunc = reinterpret_cast<PtDemoInitFunc>((base + h.init_offset) | 1u);
    frameFunc = reinterpret_cast<PtDemoFrameFunc>((base + h.frame_offset) | 1u);
    exitFunc = h.exit_offset == kExitAbsent
                 ? nullptr
                 : reinterpret_cast<PtDemoExitFunc>((base + h.exit_offset) | 1u);
    header = h;
    return true;
}

//==============================================================================================
// The /Demos list
//==============================================================================================

static constexpr int kMaxDemos = 64;
__NOINIT(EXTERNAL_RAM) static char s_names[kMaxDemos * (kMaxFileNameLen + 1)];
static char* s_nameList[kMaxDemos];
static char s_emptyLine[] = "(no demos in /Demos)";
static char* s_emptyList[1] = { s_emptyLine };

static bool isPtdemo(const char* name)
{
    const size_t len = strlen(name);
    return len > 7 && strcasecmp(name + len - 7, ".ptdemo") == 0;
}

void cDemoPlayer::scanDemos()
{
    listCount = 0;
    FileSystem* fs = platformPtr()->getFileSystem();
    if (fs == nullptr)
        return;
    if (!fs->exists("/Demos"))
        fs->mkdir(0, "/Demos");

    int count = kMaxDemos;
    fs->listDirContents("/Demos",
                        FileSystem::DirContentType::kFiles,
                        [](const char* name) { return isPtdemo(name); },
                        s_names,
                        sizeof(s_names),
                        kMaxFileNameLen,
                        count,
                        s_nameList);
    listCount = count < 0 ? 0 : (count > kMaxDemos ? kMaxDemos : count);
    if (listPos >= listCount)
        listPos = 0;
}

void cDemoPlayer::initDisplayControls()
{
    // The same layout as the File screen's browser.
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
    titleLabel = display.createControl<cLabel>(&prop2);

    strControlProperties prop;
    list.linesCount = 13;
    list.start = 0;
    list.length = 0;
    prop.x = 1;
    prop.y = 29 + 32;
    prop.w = (800 / 8 * 3) - 3;
    prop.h = 394 - 32;
    prop.value = 0;
    prop.style = controlStyleBackground |
                 (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    prop.data = &list;
    prop.colors = display.getTheme().projectActiveListColors;
    listControl = display.createControl<cList>(&prop);

    strControlProperties cover;
    cover.value = 0;
    cover.data = nullptr;
    cover.text = nullptr;
    cover.colors = display.getTheme().blankSpaceCoverLabelColors;
    cover.style = controlStyleBackground |
                  (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    cover.x = 300 + 1;
    cover.w = 500 - 2;
    cover.y = 29;
    cover.h = 394;
    coverLabel = display.createControl<cLabel>(&cover);
}

void cDemoPlayer::showList()
{
    titleBar->setTitle("Demos");
    titleBar->show();

    list.start = listCount > 0 ? listPos : 0;
    list.length = listCount > 0 ? listCount : 1;
    list.linesCount = 13;
    list.selectionActive = 0;
    list.data = listCount > 0 ? s_nameList : s_emptyList;
    display.setControlData(listControl, &list);
    display.setControlValue(listControl, list.start);
    display.setControlShow(listControl);
    display.refreshControl(listControl);

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlText(label[i], "");
        display.setControlStyle(label[i],
                                controlStyleCenterX | controlStyleCenterY | controlStyleFont3 |
                                  controlStyleShow);
        display.setControlColors(label[i], display.getTheme().activeButtonLabelsColors);
    }
    display.setControlText(label[6], "Cancel");
    display.setControlText(label[7], "Run");
    if (listCount == 0)
        display.setControlColors(label[7], display.getTheme().inactiveButtonLabelsColors);
    for (uint8_t i = 0; i < 8; i++)
        display.refreshControl(label[i]);

    display.setControlText(titleLabel, "SD Card /Demos");
    display.setControlShow(titleLabel);
    display.refreshControl(titleLabel);

    display.setControlValue(bgLabel, 255);
    display.refreshControl(bgLabel);
    display.setControlShow(coverLabel);
    display.refreshControl(coverLabel);

    display.synchronizeRefresh();
}

void cDemoPlayer::moveList(int delta)
{
    if (listCount == 0 || running)
        return;
    if (delta > 5)
        delta = 5;
    if (delta < -5)
        delta = -5;
    listPos += delta;
    if (listPos < 0)
        listPos = 0;
    if (listPos > listCount - 1)
        listPos = listCount - 1;
    display.setControlValue(listControl, listPos);
    display.refreshControl(listControl);
}

//==============================================================================================
// Module
//==============================================================================================

static ModuleBase* s_caller = nullptr;

static void functEncoder(int16_t value)
{
    DP->moveList(interfaceGlobals.invertValForList ? -value : value);
}
static void functUp() { DP->moveList(-1); }
static void functDown() { DP->moveList(1); }
static void functRun() { DP->runSelected(); }
static void functMiniMiddle(uint8_t state)
{
    if (state == buttonPress)
        DP->runSelected();
}
static void functCancel() { DP->leave(); }
static void functShift() { DP->leave(); }

void cDemoPlayer::open(ModuleBase* caller)
{
    s_caller = caller;
    // Config becomes the previous module: Cancel and Shift go back to it.
    mtInterface.deactivateModule(caller);
    mtInterface.activateModule(this, 0);
}

void cDemoPlayer::leave()
{
    eventFunct(eventSwitchToPreviousModule, this, 0, 0);
}

void cDemoPlayer::start([[maybe_unused]] uint32_t options)
{
    moduleRefresh = 1;
    running = false;
    initFailed = false;
    songTimer.hide();

    scanDemos();
    showList();

    ip->clearAllButtons();
    ip->clearAllPots();
    ip->clearGlobalPads();
    ip->setButtonObj(interfaceButton6, buttonPress, functCancel);
    ip->setButtonObj(interfaceButton7, buttonPress, functRun);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDown);
    ip->setButtonObj(interfaceButtonMiniMiddle, functMiniMiddle);
    ip->setButtonObj(interfaceButtonConfig, buttonPress, functCancel);
    ip->setPotObj(interfacePot0, functEncoder);
}

void cDemoPlayer::runSelected()
{
    if (running || listCount == 0)
        return;

    char path[kMaxFilePathLen + 1];
    snprintf(path, sizeof(path), "/Demos/%s", s_nameList[listPos]);

    const char* error = nullptr;
    if (!load(path, &error))
    {
        mtPopups.show(0, "Can't run demo", error != nullptr ? error : "Load failed");
        return;
    }
    strcpy(demoFolder, "/Demos");
    beginDemo();
}

void cDemoPlayer::beginDemo()
{
    // From here on only Shift does anything (the power button is handled
    // outside the input table and keeps working).
    ip->clearAllButtons();
    ip->clearAllPots();
    ip->clearGlobalPads();
    ip->setButtonObj(interfaceButtonShift, buttonPress, functShift);

    // Take the screen from the GUI. Let the FIFO drain first so no half-sent
    // GUI frame is mixed with ours.
    display.disable();
    driver()->awaitCoProEmpty();

    memset(s_padLevel, 0, sizeof(s_padLevel));
    if (initFunc(&s_hostApi, externalRamScratchBuff + kScratchOffset, header.scratch_size) != 0)
    {
        initFailed = true;
        return;
    }
    startMs = hostMillis();
    running = true;
}

void cDemoPlayer::stop()
{
    moduleRefresh = 0;

    if (running && exitFunc != nullptr)
        exitFunc();
    running = false;
    initFailed = false;

    // The code buffer is shared: forget the entry points.
    initFunc = nullptr;
    frameFunc = nullptr;
    exitFunc = nullptr;

    padsBacklight.clearGlobalPads(1, 1, 1);
    memset(s_padLevel, 0, sizeof(s_padLevel));

    display.setControlHide(listControl);
    display.setControlHide(titleLabel);
    display.setControlHide(coverLabel);

    // Give the screen back; the next module's controls are drawn again.
    driver()->awaitCoProEmpty();
    display.enable();
    songTimer.show();
}

void cDemoPlayer::update()
{
    if (initFailed)
    {
        initFailed = false;
        leave();
        mtPopups.show(0, "Can't run demo", "Demo failed to start");
        return;
    }

    if (!running)
        return;

    // One frame at a time without blocking the main loop: while the chip is
    // still working through the last one, let the audio and SD code run.
    if (!driver()->isCoProEmpty())
        return;

    renderFrame();
}

void cDemoPlayer::renderFrame()
{
    driver()->beginCoProList();
    API_CMD_DLSTART();
    API_VERTEX_FORMAT(0);
    API_CLEAR_COLOR(0x000000);
    API_CLEAR(1, 1, 1);
    s_burstWords = 4;
    s_frameWords = 4;
    s_frameOpen = true;

    frameFunc(hostMillis() - startMs);

    s_frameOpen = false;
    flushBurstIfFull(2);
    API_DISPLAY();
    API_CMD_SWAP();
    driver()->endCoProList();
}

#endif // PLATFORM_IMX
