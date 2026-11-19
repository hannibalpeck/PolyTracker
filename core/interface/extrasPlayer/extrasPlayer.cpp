#ifdef PLATFORM_IMX

#include "extrasPlayer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FT812.h"
#include "FT81x.h"
#include "displayStructs.h"
#include "AudioOutput.h"
#include "mtAudioEngine.h"
#include "ptAudioStream.h"
#include "ptModule.h"
#include "ptReplayer.h"
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

cExtrasPlayer extrasPlayer;
static cExtrasPlayer* DP = &extrasPlayer;

extern uint8_t externalRamScratchBuff[]; // 768 KB, executable external RAM (MPU region 8)

//==============================================================================================
// What an extra gets
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
static constexpr uint32_t kExtraRamgBase = 700000;
static constexpr uint32_t kExtraRamgSize = 1040000 - kExtraRamgBase;
static_assert(controlsRamStartAddress + controlsCount * controlsRamAddressStep <= kExtraRamgBase,
              "extra RAM_G area overlaps the controls' display lists");

// Handles 1-5 are the GUI fonts, 0 is borrowed by the controls, 15 is the
// coprocessor's scratch handle and 16-31 are the ROM fonts.
static constexpr uint8_t kFirstExtraHandle = 8;
static constexpr uint8_t kExtraHandleCount = 7;

static constexpr uint32_t kExitAbsent = 0xFFFFFFFFu;

// The frame goes through the coprocessor's 4 KB command FIFO. The words are
// sent in bursts and the FIFO is drained between them; the chip's display
// list holds 2048 words, so an extra gets a little under that.
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
// Extra music: a second ProTracker module and replayer
//==============================================================================================

// Its own memory (mtStructs.h), so the song being edited is never touched.
__NOINIT(EXTERNAL_RAM) static uint8_t s_extraPool[EXTRA_MOD_POOL_SIZE];
static_assert(EXTRA_MOD_POOL_SIZE >= protracker::kPoolBytes, "EXTRA_MOD_POOL_SIZE is too small");

static protracker::Module s_extraModule;
static protracker::Replayer s_extraReplayer;
static bool s_musicReady = false;  // module memory attached, replayer initialised
static bool s_hasModule = false;   // a module is loaded
static bool s_musicActive = false; // the stream plays the extra's replayer
static protracker::Replayer* s_prevReplayer = nullptr;
static bool s_prevStreamEnabled = false;

static ProTrackerStream* ptStream()
{
    return engine.getAudioOutput()->getProTrackerStream();
}

static void initMusic()
{
    if (s_musicReady)
        return;
    s_extraModule.attach(s_extraPool, sizeof(s_extraPool));
    s_extraReplayer.init(&s_extraModule, AUDIO_SAMPLE_RATE_EXACT);
    s_musicReady = true;
}

// Module::load reads through this: a window [base, base + size) of an open file.
struct ModWindow
{
    FileSystem* fs;
    FileSystem::FileHandle handle;
    uint32_t base;
    uint32_t size;
    uint32_t pos;
};

static uint32_t modRead(void* ctx, void* dst, uint32_t count)
{
    ModWindow* w = static_cast<ModWindow*>(ctx);
    if (w->pos >= w->size)
        return 0;
    if (count > w->size - w->pos)
        count = w->size - w->pos;
    uint32_t total = 0;
    while (total < count)
    {
        int32_t got = w->fs->readFile(static_cast<uint8_t*>(dst) + total, count - total, w->handle);
        if (got <= 0)
            break;
        total += static_cast<uint32_t>(got);
    }
    w->pos += total;
    return total;
}

static bool modSeek(void* ctx, uint32_t offset)
{
    ModWindow* w = static_cast<ModWindow*>(ctx);
    if (offset > w->size || !w->fs->seekFile(w->base + offset, w->handle))
        return false;
    w->pos = offset;
    return true;
}

// Loads a module from part of a file. size 0 = to the end of the file. The extra's replayer must
// not be playing it meanwhile (the caller pauses the stream when it is attached).
static bool loadModule(const char* path, uint32_t base, uint32_t size)
{
    initMusic();
    s_hasModule = false;

    FileSystem* fs = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
    if (fs == nullptr)
        return false;
    auto [isOpen, handle] = fs->openFile(path, FileSystem::FileOpenMode::kRead);
    if (!isOpen)
        return false;

    ModWindow window = { fs, handle, base, size == 0 ? 0xFFFFFFFFu - base : size, 0 };
    protracker::FileIo io;
    io.ctx = &window;
    io.read = modRead;
    io.seek = modSeek;

    s_extraReplayer.stop();
    const protracker::Result result = s_extraModule.load(io);
    fs->closeFile(handle);

    if (result != protracker::Result::kOk)
    {
        s_extraModule.clear();
        return false;
    }
    s_hasModule = true;
    return true;
}

// The stream plays the extra's replayer while an extra runs, and the song's replayer again after.
static void startMusicOutput()
{
    initMusic();
    ProTrackerStream* stream = ptStream();
    s_prevReplayer = stream->attached();
    s_prevStreamEnabled = stream->isEnabled();
    stream->switchTo(&s_extraReplayer);
    // switchTo() drops a stop still pending for the song, so stop it here; it is detached now.
    if (s_prevReplayer != nullptr && s_prevReplayer != &s_extraReplayer)
    {
        s_prevReplayer->stop();
        s_prevReplayer->previewStop();
    }
    stream->setEnabled(true);
    s_musicActive = true;
}

static void stopMusicOutput()
{
    if (!s_musicActive)
        return;
    ProTrackerStream* stream = ptStream();
    stream->switchTo(s_prevReplayer);
    stream->setEnabled(s_prevStreamEnabled);
    s_extraReplayer.stop(); // detached now, safe to touch
    s_extraReplayer.previewStop();
    s_musicActive = false;
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

// Inside extra_frame the coprocessor command stream is open. RAM_G writes and
// SD card work in the middle of a frame close the current burst first and
// open a new one after, the same way a full burst is flushed.
static void pauseFrameStream()
{
    if (s_frameOpen)
    {
        driver()->endCoProList();
    }
}

static void resumeFrameStream()
{
    if (s_frameOpen)
    {
        driver()->beginCoProList();
        s_burstWords = 0;
    }
}

static void hostRamgWrite(uint32_t addr, const void* data, uint32_t size)
{
    if (data == nullptr || size == 0 || addr < kExtraRamgBase ||
        addr + size > kExtraRamgBase + kExtraRamgSize || addr + size < addr)
    {
        return;
    }
    pauseFrameStream();
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
    resumeFrameStream();
}

static void hostRamgRead(uint32_t addr, void* data, uint32_t size)
{
    if (data == nullptr || size == 0 || size > 32768)
        return;
    pauseFrameStream();
    driver()->readDataRAMG(static_cast<uint8_t*>(data), size, addr);
    resumeFrameStream();
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

// A path without a leading '/' is relative to the extra's folder.
static void extraPath(const char* path, char* out, size_t outSize)
{
    if (path[0] == '/')
        snprintf(out, outSize, "%s", path);
    else
        snprintf(out, outSize, "%s/%s", DP->extraFolder, path);
}

static int32_t readFileImpl(const char* path, void* buffer, uint32_t maxBytes);

static int32_t hostReadFile(const char* path, void* buffer, uint32_t maxBytes)
{
    pauseFrameStream();
    const int32_t result = readFileImpl(path, buffer, maxBytes);
    resumeFrameStream();
    return result;
}

static int32_t readFileImpl(const char* path, void* buffer, uint32_t maxBytes)
{
    FileSystem* fs = platformPtr()->getFileSystem();
    if (fs == nullptr || path == nullptr || buffer == nullptr || maxBytes == 0)
        return -1;

    char fullPath[kMaxFilePathLen + 1];
    extraPath(path, fullPath, sizeof(fullPath));

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

static void hostModState(PtExtraModState* out)
{
    if (out == nullptr)
        return;
    memset(out, 0, sizeof(*out));
    if (!s_hasModule)
        return;
    const protracker::Replayer& r = s_extraReplayer;
    out->playing = r.isPlaying() ? 1 : 0;
    out->position = (uint8_t)r.position();
    out->pattern = (uint8_t)r.pattern();
    out->row = (uint8_t)r.row();
    out->tick = (uint8_t)r.currentTick();
    out->speed = (uint8_t)r.speed();
    out->bpm = (uint16_t)r.bpm();
    for (int c = 0; c < 4; c++)
    {
        out->vu[c] = r.channelVu(c);
        out->hits[c] = r.channelHits(c);
    }
    out->loops = r.songLoops();
}

static uint8_t hostModPlay(uint8_t position)
{
    if (!s_hasModule || !s_musicActive)
        return 0;
    if (position >= s_extraModule.songLength)
        position = 0;
    ptStream()->requestPlay(position, 0);
    return 1;
}

static void hostModStop()
{
    if (s_musicActive)
        ptStream()->requestStop();
}

static int32_t hostModLoad(const char* path)
{
    if (path == nullptr)
        return -1;
    char fullPath[kMaxFilePathLen + 1];
    if (path[0] == '/')
        snprintf(fullPath, sizeof(fullPath), "%s", path);
    else
        snprintf(fullPath, sizeof(fullPath), "%s/%s", DP->extraFolder, path);

    ProTrackerStream* stream = ptStream();
    if (s_musicActive)
        stream->pause();
    const bool ok = loadModule(fullPath, 0, 0);
    if (s_musicActive)
        stream->resume();
    return ok ? 0 : -1;
}

// ---- API version 3: input and saving

static constexpr uint32_t kInputQueueSize = 64;
static PtExtraInput s_inputQueue[kInputQueueSize];
static volatile uint32_t s_inputHead = 0;
static volatile uint32_t s_inputTail = 0;
static bool s_inputExtra = false; // the running extra has PTEXTRA_FLAG_INPUT
static bool s_shiftDown = false;

static void pushInput(uint8_t type, uint8_t index, int16_t value)
{
    const uint32_t next = (s_inputHead + 1) % kInputQueueSize;
    if (next == s_inputTail)
        return; // full: drop
    s_inputQueue[s_inputHead] = PtExtraInput{ type, index, value };
    s_inputHead = next;
}

static int32_t hostPollInput(PtExtraInput* out)
{
    if (out == nullptr || s_inputTail == s_inputHead)
        return 0;
    *out = s_inputQueue[s_inputTail];
    s_inputTail = (s_inputTail + 1) % kInputQueueSize;
    return 1;
}

static int32_t writeFileImpl(const char* path, const void* data, uint32_t size)
{
    FileSystem* fs = platformPtr()->getFileSystem();
    if (fs == nullptr || path == nullptr || (data == nullptr && size > 0))
        return -1;

    char fullPath[kMaxFilePathLen + 1];
    extraPath(path, fullPath, sizeof(fullPath));

    auto [isOpen, fileHandle] = fs->openFile(fullPath, FileSystem::FileOpenMode::kWriteCreateNew);
    if (!isOpen)
        return -1;

    // Small writes from a stack buffer: the source may be in cached external RAM.
    const uint8_t* src = static_cast<const uint8_t*>(data);
    uint32_t total = 0;
    uint8_t chunkBuf[256];
    while (total < size)
    {
        uint32_t chunk = size - total;
        if (chunk > sizeof(chunkBuf))
            chunk = sizeof(chunkBuf);
        memcpy(chunkBuf, src + total, chunk);
        const int32_t n = fs->writeFile(chunkBuf, chunk, fileHandle);
        if (n != static_cast<int32_t>(chunk))
        {
            fs->closeFile(fileHandle);
            return -1;
        }
        total += chunk;
    }
    fs->closeFile(fileHandle);
    return static_cast<int32_t>(total);
}

static int32_t hostWriteFile(const char* path, const void* data, uint32_t size)
{
    pauseFrameStream();
    const int32_t result = writeFileImpl(path, data, size);
    resumeFrameStream();
    return result;
}

static int32_t listDirImpl(const char* path, char* names, uint32_t namesSize, uint32_t maxNames)
{
    FileSystem* fs = platformPtr()->getFileSystem();
    if (fs == nullptr || path == nullptr || names == nullptr || namesSize == 0 || maxNames == 0)
        return -1;

    char fullPath[kMaxFilePathLen + 1];
    extraPath(path, fullPath, sizeof(fullPath));
    if (!fs->exists(fullPath))
        return -1;

    static constexpr int kMaxList = 128;
    static char* list[kMaxList];
    int count = maxNames > kMaxList ? kMaxList : static_cast<int>(maxNames);
    fs->listDirContents(fullPath,
                        FileSystem::DirContentType::kFiles,
                        nullptr,
                        names,
                        static_cast<int>(namesSize),
                        kMaxFileNameLen,
                        count,
                        list);
    if (count < 0)
        return -1;

    // listDirContents packs the names into `names` in order; make sure they
    // sit back to back from the start, as the API promises.
    uint32_t used = 0;
    static char tmp[kMaxFileNameLen + 1];
    for (int i = 0; i < count; i++)
    {
        const size_t len = strlen(list[i]) + 1;
        if (used + len > namesSize || len > sizeof(tmp))
        {
            count = i;
            break;
        }
        memcpy(tmp, list[i], len);
        memmove(names + used, tmp, len);
        used += len;
    }
    return count;
}

static int32_t hostListDir(const char* path, char* names, uint32_t namesSize, uint32_t maxNames)
{
    pauseFrameStream();
    const int32_t result = listDirImpl(path, names, namesSize, maxNames);
    resumeFrameStream();
    return result;
}

static int32_t hostFileExists(const char* path)
{
    FileSystem* fs = platformPtr()->getFileSystem();
    if (fs == nullptr || path == nullptr)
        return 0;
    char fullPath[kMaxFilePathLen + 1];
    extraPath(path, fullPath, sizeof(fullPath));
    pauseFrameStream();
    const bool exists = fs->exists(fullPath);
    resumeFrameStream();
    return exists ? 1 : 0;
}

static int32_t hostMakeDir(const char* path)
{
    FileSystem* fs = platformPtr()->getFileSystem();
    if (fs == nullptr || path == nullptr)
        return -1;
    char fullPath[kMaxFilePathLen + 1];
    extraPath(path, fullPath, sizeof(fullPath));
    pauseFrameStream();
    int32_t result = 0;
    if (!fs->exists(fullPath) && !fs->mkdir(0, fullPath))
        result = -1;
    resumeFrameStream();
    return result;
}

static const PtExtraHostApi s_hostApi = {
    PTEXTRA_API_VERSION,
    800,
    480,
    1, // Tracker+ (IMX)
    kFirstExtraHandle,
    kExtraHandleCount,
    48,
    kExtraRamgBase,
    kExtraRamgSize,
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
    hostModState,
    hostModPlay,
    hostModStop,
    hostModLoad,
    hostPollInput,
    hostWriteFile,
    hostListDir,
    hostFileExists,
    hostMakeDir,
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

bool cExtrasPlayer::load(const char* path, const char** error)
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

    alignas(4) PtExtraHeader h;
    if (fs->readFile(&h, sizeof(h), fileHandle) != static_cast<int32_t>(sizeof(h)) ||
        h.magic != PTEXTRA_MAGIC)
    {
        fs->closeFile(fileHandle);
        *error = "Not a .ptextra file";
        return false;
    }

    const char* problem = nullptr;
    const uint32_t total = h.code_size + h.bss_size;
    if (h.format_version != PTEXTRA_FORMAT_VERSION)
        problem = "Unsupported extra version";
    else if (h.code_size == 0 || total < h.code_size || total > kCodeBufferSize)
        problem = "Extra code too big";
    else if (h.required_api > PTEXTRA_API_VERSION)
        problem = "Needs newer firmware";
    else if (h.scratch_size > kScratchSize)
        problem = "Extra needs too much RAM";
    else if (h.mod_size != 0 && h.mod_offset < h.code_offset + h.code_size)
        problem = "Corrupt extra file";
    else if (h.code_offset < sizeof(PtExtraHeader) || h.init_offset >= h.code_size ||
             h.frame_offset >= h.code_size ||
             (h.exit_offset != kExitAbsent && h.exit_offset >= h.code_size))
        problem = "Corrupt extra file";
    else if (!fs->seekFile(h.code_offset, fileHandle))
        problem = "Corrupt extra file";

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

    // The extra's music, if it carries any. Nothing plays it yet.
    s_hasModule = false;
    if (h.mod_size != 0 && !loadModule(path, h.mod_offset, h.mod_size))
    {
        *error = "Bad module in extra";
        return false;
    }

    // Thumb entry points: base + offset with bit 0 set.
    const uintptr_t base = reinterpret_cast<uintptr_t>(codeBuf);
    initFunc = reinterpret_cast<PtExtraInitFunc>((base + h.init_offset) | 1u);
    frameFunc = reinterpret_cast<PtExtraFrameFunc>((base + h.frame_offset) | 1u);
    exitFunc = h.exit_offset == kExitAbsent
                 ? nullptr
                 : reinterpret_cast<PtExtraExitFunc>((base + h.exit_offset) | 1u);
    header = h;
    return true;
}

//==============================================================================================
// The /Extras list
//==============================================================================================

static constexpr int kMaxExtras = 64;
__NOINIT(EXTERNAL_RAM) static char s_names[kMaxExtras * (kMaxFileNameLen + 1)];
static char* s_nameList[kMaxExtras];
static char s_emptyLine[] = "(no extras in /Extras)";
static char* s_emptyList[1] = { s_emptyLine };

static bool isPtextra(const char* name)
{
    const size_t len = strlen(name);
    return len > 8 && strcasecmp(name + len - 8, ".ptextra") == 0;
}

void cExtrasPlayer::scanExtras()
{
    listCount = 0;
    FileSystem* fs = platformPtr()->getFileSystem();
    if (fs == nullptr)
        return;
    if (!fs->exists("/Extras"))
        fs->mkdir(0, "/Extras");

    int count = kMaxExtras;
    fs->listDirContents("/Extras",
                        FileSystem::DirContentType::kFiles,
                        [](const char* name) { return isPtextra(name); },
                        s_names,
                        sizeof(s_names),
                        kMaxFileNameLen,
                        count,
                        s_nameList);
    listCount = count < 0 ? 0 : (count > kMaxExtras ? kMaxExtras : count);
    if (listPos >= listCount)
        listPos = 0;
}

void cExtrasPlayer::initDisplayControls()
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

void cExtrasPlayer::showList()
{
    titleBar->setTitle("Extras");
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

    display.setControlText(titleLabel, "SD Card /Extras");
    display.setControlShow(titleLabel);
    display.refreshControl(titleLabel);

    display.setControlValue(bgLabel, 255);
    display.refreshControl(bgLabel);
    display.setControlShow(coverLabel);
    display.refreshControl(coverLabel);

    display.synchronizeRefresh();
}

void cExtrasPlayer::moveList(int delta)
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

// Input extras get everything; Shift+Pattern leaves.
static void functExtraButton(uint8_t button, uint8_t state)
{
    if (state != buttonPress && state != buttonRelease)
        return;
    if (button == interfaceButtonShift)
        s_shiftDown = state == buttonPress;
    if (button == interfaceButtonPattern && state == buttonPress && s_shiftDown)
    {
        DP->leave();
        return;
    }
    pushInput(state == buttonPress ? PTD_INPUT_BUTTON_PRESS : PTD_INPUT_BUTTON_RELEASE, button, 0);
}

static void functExtraPads(uint8_t pad, uint8_t state, [[maybe_unused]] int16_t velo)
{
    if (pad >= 48)
        return;
    if (state == buttonPress)
        pushInput(PTD_INPUT_PAD_PRESS, pad, 0);
    else if (state == buttonRelease)
        pushInput(PTD_INPUT_PAD_RELEASE, pad, 0);
}

static void functExtraEncoder(int16_t value)
{
    pushInput(PTD_INPUT_JOG, 0, value);
}

void cExtrasPlayer::open(ModuleBase* caller)
{
    s_caller = caller;
    // Config becomes the previous module: Cancel and Shift go back to it.
    mtInterface.deactivateModule(caller);
    mtInterface.activateModule(this, 0);
}

void cExtrasPlayer::leave()
{
    eventFunct(eventSwitchToPreviousModule, this, 0, 0);
}

void cExtrasPlayer::start([[maybe_unused]] uint32_t options)
{
    moduleRefresh = 1;
    running = false;
    initFailed = false;
    songTimer.hide();

    scanExtras();
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

void cExtrasPlayer::runSelected()
{
    if (running || listCount == 0)
        return;

    char path[kMaxFilePathLen + 1];
    snprintf(path, sizeof(path), "/Extras/%s", s_nameList[listPos]);

    const char* error = nullptr;
    if (!load(path, &error))
    {
        mtPopups.show(0, "Can't run extra", error != nullptr ? error : "Load failed");
        return;
    }
    strcpy(extraFolder, "/Extras");

    // An extra brings its own music, if any: the song stops and stays stopped.
    protrackerMode.stopPreview();
    if (protrackerMode.isPlaying())
        protrackerMode.togglePlay(false);

    beginExtra();
}

void cExtrasPlayer::beginExtra()
{
    // From here on only Shift does anything (the power button is handled
    // outside the input table and keeps working). Input extras get every
    // button, pad and the jog wheel instead, and are left with Shift+Pattern.
    ip->clearAllButtons();
    ip->clearAllPots();
    ip->clearGlobalPads();
    s_inputExtra = (header.flags & PTEXTRA_FLAG_INPUT) != 0;
    s_inputHead = 0;
    s_inputTail = 0;
    s_shiftDown = false;
    if (s_inputExtra)
    {
        for (uint8_t b = 0; b < interfaceButtonsCount; b++)
        {
            ip->setButtonObj(b, functExtraButton);
        }
        ip->setPadsGlobal(functExtraPads);
        ip->setPotObj(interfacePot0, functExtraEncoder, false);
    }
    else
    {
        ip->setButtonObj(interfaceButtonShift, buttonPress, functShift);
    }

    // Take the screen from the GUI. Let the FIFO drain first so no half-sent
    // GUI frame is mixed with ours.
    display.disable();
    driver()->awaitCoProEmpty();

    memset(s_padLevel, 0, sizeof(s_padLevel));
    startMusicOutput();
    if (initFunc(&s_hostApi, externalRamScratchBuff + kScratchOffset, header.scratch_size) != 0)
    {
        initFailed = true;
        return;
    }
    // The music starts with the first frame, so time 0 and position 0 line up.
    if (s_hasModule)
        ptStream()->requestPlay(0, 0);
    startMs = hostMillis();
    running = true;
}

void cExtrasPlayer::stop()
{
    moduleRefresh = 0;

    if (running && exitFunc != nullptr)
        exitFunc();
    running = false;
    initFailed = false;
    s_inputExtra = false;

    // The song's replayer gets the output back; the tracker song stays stopped.
    stopMusicOutput();
    s_hasModule = false;

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

void cExtrasPlayer::endForShutdown()
{
    // The unit is switching off: let the extra save its work, then give the
    // screen back so the shutdown screen shows.
    if (!running)
        return;
    if (exitFunc != nullptr)
        exitFunc();
    exitFunc = nullptr;
    running = false;
    stopMusicOutput();
    driver()->awaitCoProEmpty();
    display.enable();
    leave();
}

void cExtrasPlayer::update()
{
    if (initFailed)
    {
        initFailed = false;
        leave();
        mtPopups.show(0, "Can't run extra", "Extra failed to start");
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

void cExtrasPlayer::renderFrame()
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
