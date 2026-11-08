// See ptModule.h for credits.

#include "ptModule.h"

#include <string.h>

namespace protracker
{

namespace
{
constexpr uint32_t kHeaderBytes = 1084;
constexpr uint32_t kSampleInfoOffset = 20;
constexpr uint32_t kSampleInfoBytes = 30;
constexpr uint32_t kSongLengthOffset = 950;
constexpr uint32_t kOrderOffset = 952;
constexpr uint32_t kTagOffset = 1080;

uint16_t be16(const uint8_t* p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

void putBe16(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

// Convert illegal characters to spaces and interior zeroes to spaces, like ProTracker/pt2-clone.
void sanitiseString(char* str, uint32_t maxLength)
{
    for (uint32_t i = 0; i < maxLength; i++)
    {
        char c = str[i];
        if ((c < ' ' || c > '~') && c != '\0')
            str[i] = ' ';
    }

    int32_t last = (int32_t)maxLength - 1;
    while (last >= 0 && str[last] == '\0')
        last--;

    for (int32_t j = 0; j < last; j++)
    {
        if (str[j] == '\0')
            str[j] = ' ';
    }
}

enum class Tag
{
    kFourChannel,
    kMultiChannel, // recognised but not supported
    kUnknown,
};

Tag classifyTag(const uint8_t* t)
{
    static const char* const kSupported[] = { "M.K.", "M!K!", "FLT4", "4CHN", "N.T." };
    for (const char* s : kSupported)
    {
        if (memcmp(t, s, 4) == 0)
            return Tag::kFourChannel;
    }

    static const char* const kMulti[] = { "6CHN", "8CHN", "FLT8", "OCTA", "OKTA", "CD81", "CD61" };
    for (const char* s : kMulti)
    {
        if (memcmp(t, s, 4) == 0)
            return Tag::kMultiChannel;
    }

    // "xxCH" with two digits
    if (t[2] == 'C' && t[3] == 'H' && t[0] >= '0' && t[0] <= '9' && t[1] >= '0' && t[1] <= '9')
        return Tag::kMultiChannel;

    return Tag::kUnknown;
}

} // namespace

const char* resultText(Result r)
{
    switch (r)
    {
        case Result::kOk: return "OK";
        case Result::kIoError: return "FILE I/O ERROR";
        case Result::kBadFormat: return "NOT A PROTRACKER MOD";
        case Result::kUnsupportedChannels: return "ONLY 4 CHANNELS";
        case Result::kPoolTooSmall: return "OUT OF MEMORY";
        case Result::kNotAttached: return "NO MEMORY ATTACHED";
    }
    return "ERROR";
}

bool Module::attach(uint8_t* pool, size_t poolBytes)
{
    if (pool == nullptr || poolBytes < kPoolBytes)
        return false;

    pool_ = pool;
    clear();
    return true;
}

void Module::clear()
{
    if (pool_ != nullptr)
        memset(pool_, 0, kPoolBytes);

    memset(title, 0, sizeof(title));
    memset(samples, 0, sizeof(samples));
    for (int i = 0; i < kSampleCount; i++)
        samples[i].loopLength = 2;

    memset(order, 0, sizeof(order));
    songLength = 1;
    initialTempo = 125;
}

void Module::clearSong()
{
    if (pool_ != nullptr)
        memset(pool_, 0, (size_t)kMaxPatterns * kPatternBytes);

    memset(title, 0, sizeof(title));
    memset(order, 0, sizeof(order));
    songLength = 1;
    initialTempo = 125;
}

int Module::patternCountInOrderList() const
{
    int highest = 0;
    for (int i = 0; i < kOrderCount; i++)
    {
        if (order[i] > highest)
            highest = order[i];
    }

    return highest + 1;
}

void Module::fixSample(int index)
{
    Sample& s = samples[index];

    sanitiseString(s.name, 22);
    s.name[22] = '\0';

    s.length &= ~1u;
    if (s.length > kMaxSampleBytes)
        s.length = kMaxSampleBytes;

    s.fineTune &= 0x0F;
    if (s.volume > 64)
        s.volume = 64;

    s.loopStart &= ~1u;
    s.loopLength &= ~1u;
    if (s.loopLength < 2)
        s.loopLength = 2;

    // a loop that cannot be represented gets disabled
    if (s.loopStart > kMaxSampleBytes || s.loopStart + s.loopLength > kMaxSampleBytes)
    {
        s.loopStart = 0;
        s.loopLength = 2;
    }

    // some modules are broken like this: grow the sample so that the loop fits inside it
    if (s.length > 0 && s.loopLength > 2 && s.loopStart + s.loopLength > s.length)
    {
        uint32_t overflow = (s.loopStart + s.loopLength) - s.length;
        if (s.length + overflow <= kMaxSampleBytes)
        {
            s.length += overflow;
        }
        else
        {
            s.loopStart = 0;
            s.loopLength = 2;
        }
    }

    // clear the first two bytes of non-looping samples to prevent a beep after playback
    if (s.length >= 2 && s.loopStart + s.loopLength <= 2)
    {
        int8_t* d = sampleData(index);
        d[0] = 0;
        d[1] = 0;
    }
}

Result Module::load(const FileIo& io)
{
    if (pool_ == nullptr)
        return Result::kNotAttached;
    if (io.read == nullptr || io.seek == nullptr)
        return Result::kIoError;

    uint8_t header[kHeaderBytes];
    if (!io.seek(io.ctx, 0))
        return Result::kIoError;
    if (io.read(io.ctx, header, kHeaderBytes) != kHeaderBytes)
        return Result::kBadFormat; // too short to be a module

    switch (classifyTag(&header[kTagOffset]))
    {
        case Tag::kFourChannel: break;
        case Tag::kMultiChannel: return Result::kUnsupportedChannels;
        case Tag::kUnknown: return Result::kBadFormat;
    }

    clear();

    memcpy(title, header, 20);
    title[20] = '\0';
    sanitiseString(title, 20);

    for (int i = 0; i < kSampleCount; i++)
    {
        const uint8_t* p = &header[kSampleInfoOffset + (uint32_t)i * kSampleInfoBytes];
        Sample& s = samples[i];

        memcpy(s.name, p, 22);
        s.name[22] = '\0';
        s.length = (uint32_t)be16(p + 22) * 2;
        s.fineTune = p[24] & 0x0F;
        s.volume = p[25];
        s.loopStart = (uint32_t)be16(p + 26) * 2;
        s.loopLength = (uint32_t)be16(p + 28) * 2;
    }

    songLength = header[kSongLengthOffset];
    if (songLength == 0)
        songLength = 1;
    if (songLength > kOrderCount)
        songLength = kOrderCount;

    memcpy(order, &header[kOrderOffset], kOrderCount);
    initialTempo = 125;

    const int filePatterns = patternCountInOrderList();

    // patterns
    for (int p = 0; p < filePatterns; p++)
    {
        if (p < kMaxPatterns)
        {
            if (io.read(io.ctx, pattern(p), kPatternBytes) != kPatternBytes)
                return Result::kBadFormat;
        }
        else
        {
            // more patterns than ProTracker supports: skip them
            if (!io.seek(io.ctx, kHeaderBytes + (uint32_t)(p + 1) * kPatternBytes))
                return Result::kIoError;
        }
    }

    // Sample data follows the patterns. The file stores each sample at its full declared length, so
    // keep the declared lengths to find where the next sample starts, even if we have to truncate a
    // sample that is longer than we can hold.
    uint32_t fileOffset = kHeaderBytes + (uint32_t)filePatterns * kPatternBytes;
    bool endOfFile = false;

    for (int i = 0; i < kSampleCount; i++)
    {
        Sample& s = samples[i];
        const uint32_t declared = s.length;

        if (s.length > kMaxSampleBytes)
            s.length = kMaxSampleBytes;

        if (!endOfFile && s.length > 0)
        {
            if (!io.seek(io.ctx, fileOffset))
            {
                endOfFile = true;
            }
            else
            {
                const uint32_t got = io.read(io.ctx, sampleData(i), s.length);
                if (got < s.length)
                {
                    // truncated file: keep what we have, the rest of the slot is already zero
                    endOfFile = true;
                    s.length = got & ~1u;
                }
            }
        }
        else if (endOfFile)
        {
            s.length = 0;
        }

        fileOffset += declared;
    }

    for (int i = 0; i < kSampleCount; i++)
        fixSample(i);

    return Result::kOk;
}

Result Module::save(const FileIo& io) const
{
    if (pool_ == nullptr)
        return Result::kNotAttached;
    if (io.write == nullptr)
        return Result::kIoError;

    uint8_t header[kHeaderBytes];
    memset(header, 0, sizeof(header));

    memcpy(header, title, 20);

    for (int i = 0; i < kSampleCount; i++)
    {
        const Sample& s = samples[i];
        uint8_t* p = &header[kSampleInfoOffset + (uint32_t)i * kSampleInfoBytes];

        memcpy(p, s.name, 22);
        putBe16(p + 22, (s.length & ~1u) >> 1);
        p[24] = s.fineTune & 0x0F;
        p[25] = s.volume > 64 ? 64 : s.volume;

        uint32_t loopStart = s.loopStart;
        uint32_t loopLength = s.loopLength;
        if (loopLength < 2)
            loopLength = 2;

        if (loopStart + loopLength <= 2 || loopStart + loopLength > s.length)
        {
            loopStart = 0;
            loopLength = 2;
        }

        putBe16(p + 26, loopStart >> 1);
        putBe16(p + 28, loopLength >> 1);
    }

    header[kSongLengthOffset] = songLength;
    header[kSongLengthOffset + 1] = 0x7F; // ProTracker writes 0x7F here
    memcpy(&header[kOrderOffset], order, kOrderCount);

    int numPatterns = patternCountInOrderList();
    if (numPatterns > kMaxPatterns)
        numPatterns = kMaxPatterns;

    memcpy(&header[kTagOffset], numPatterns <= 64 ? "M.K." : "M!K!", 4);

    if (!io.write(io.ctx, header, kHeaderBytes))
        return Result::kIoError;

    for (int p = 0; p < numPatterns; p++)
    {
        if (!io.write(io.ctx, pattern(p), kPatternBytes))
            return Result::kIoError;
    }

    for (int i = 0; i < kSampleCount; i++)
    {
        const Sample& s = samples[i];
        if (s.length == 0)
            continue;

        const int8_t* data = sampleData(i);
        uint32_t offset = 0;

        // non-looping samples get their first two bytes cleared (prevents a stuck beep)
        if (s.length >= 2 && s.loopStart + s.loopLength == 2)
        {
            static const uint8_t zeros[2] = { 0, 0 };
            if (!io.write(io.ctx, zeros, 2))
                return Result::kIoError;
            offset = 2;
        }

        if (s.length > offset && !io.write(io.ctx, data + offset, s.length - offset))
            return Result::kIoError;
    }

    return Result::kOk;
}

} // namespace protracker
