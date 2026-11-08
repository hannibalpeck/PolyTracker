// ProTracker module data model and genuine Amiga .mod (M.K.) load/save.
//
// Pure C++ with no hardware dependencies, so it can be unit tested on a host machine.
//
// The file format handling and sanitising rules follow pt2-clone by Olav Sorensen
// (8bitbubsy), https://github.com/8bitbubsy/pt2-clone, BSD 3-Clause License.

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace protracker
{

constexpr int kSampleCount = 31;
constexpr int kChannelCount = 4;
constexpr int kRowCount = 64;
constexpr int kMaxPatterns = 100;
constexpr int kOrderCount = 128;

// Every sample owns a fixed slot, so a sample can be resized in place. 65534 bytes is the longest
// sample ProTracker itself handles. (The 16-bit "words" field of a .mod could express up to 131070;
// longer samples in a file are truncated on load, like pt2-clone does by default.)
constexpr uint32_t kSampleSlotBytes = 65536;
constexpr uint32_t kMaxSampleBytes = 65534;

constexpr uint32_t kPatternBytes = kRowCount * kChannelCount * 4;

// Memory the module needs, handed to ptModule::attach().
constexpr size_t kPoolBytes =
  (size_t)kMaxPatterns * kPatternBytes + (size_t)kSampleCount * kSampleSlotBytes;

enum class Result
{
    kOk = 0,
    kIoError,            // read/seek/write failed
    kBadFormat,          // not a ProTracker module
    kUnsupportedChannels, // 6CHN / 8CHN etc.
    kPoolTooSmall,
    kNotAttached,
};

// Minimal file abstraction so the same code runs on the device (SD card) and on a host.
struct FileIo
{
    void* ctx = nullptr;
    // return the number of bytes read (may be short at end of file)
    uint32_t (*read)(void* ctx, void* dst, uint32_t count) = nullptr;
    // return true if all bytes were written
    bool (*write)(void* ctx, const void* src, uint32_t count) = nullptr;
    // absolute seek, return true on success
    bool (*seek)(void* ctx, uint32_t offset) = nullptr;
};

// One pattern cell, stored exactly as in the file (4 bytes) so saving is lossless.
struct Note
{
    uint8_t b[4];

    uint8_t sample() const { return (uint8_t)((b[0] & 0xF0) | (b[2] >> 4)); }
    uint16_t period() const { return (uint16_t)(((b[0] & 0x0F) << 8) | b[1]); }
    uint8_t command() const { return b[2] & 0x0F; }
    uint8_t param() const { return b[3]; }

    void set(uint8_t sample, uint16_t period, uint8_t command, uint8_t param)
    {
        b[0] = (uint8_t)((sample & 0xF0) | ((period >> 8) & 0x0F));
        b[1] = (uint8_t)(period & 0xFF);
        b[2] = (uint8_t)(((sample << 4) & 0xF0) | (command & 0x0F));
        b[3] = param;
    }
};

struct Sample
{
    char name[23]; // 22 characters + terminator
    uint32_t length;     // bytes (always even)
    uint8_t fineTune;    // 0..15, 8..15 are -8..-1
    uint8_t volume;      // 0..64
    uint32_t loopStart;  // bytes
    uint32_t loopLength; // bytes, 2 = no loop
};

class Module
{
  public:
    char title[21];
    Sample samples[kSampleCount];
    uint8_t songLength; // number of orders, 1..128
    uint8_t order[kOrderCount];
    uint8_t initialTempo; // always 125 for plain M.K. files

    // Give the module its memory (kPoolBytes). The memory is cleared and then owned by the
    // caller; it has to stay valid for as long as the module (and a replayer) is in use.
    bool attach(uint8_t* pool, size_t poolBytes);
    bool isAttached() const { return pool_ != nullptr; }

    // A fresh empty song: one empty pattern, no samples.
    void clear();
    void clearSong(); // patterns, order list, title and tempo; the samples stay

    Note* pattern(int index) { return (Note*)(pool_ + (size_t)index * kPatternBytes); }
    const Note* pattern(int index) const
    {
        return (const Note*)(pool_ + (size_t)index * kPatternBytes);
    }
    int8_t* sampleData(int index)
    {
        return (int8_t*)(pool_ + (size_t)kMaxPatterns * kPatternBytes +
                         (size_t)index * kSampleSlotBytes);
    }
    const int8_t* sampleData(int index) const
    {
        return (const int8_t*)(pool_ + (size_t)kMaxPatterns * kPatternBytes +
                               (size_t)index * kSampleSlotBytes);
    }

    // Highest pattern number used in the order list, plus one (what a file has to contain).
    int patternCountInOrderList() const;

    Result load(const FileIo& io);
    Result save(const FileIo& io) const;

    // Sanitise a sample's fields the way the loader does (also usable after editing).
    void fixSample(int index);

  private:
    uint8_t* pool_ = nullptr;
};

const char* resultText(Result r);

} // namespace protracker
