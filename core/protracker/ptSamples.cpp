#include "ptSamples.h"

#include <string.h>

namespace protracker
{

namespace
{
constexpr uint32_t kChunkBytes = 4096;

int32_t readSigned(const uint8_t* p, const WavInfo& info)
{
    // returns the value scaled to 16 bits signed
    if (info.format == 3)
    {
        float f;
        memcpy(&f, p, 4);
        if (f > 1.0f)
            f = 1.0f;
        if (f < -1.0f)
            f = -1.0f;
        return (int32_t)(f * 32767.0f);
    }

    switch (info.bits)
    {
        case 8: return ((int32_t)p[0] - 128) << 8;
        case 16: return (int16_t)(p[0] | (p[1] << 8));
        case 24: return (int16_t)(p[1] | (p[2] << 8));
        default: return (int16_t)(p[2] | (p[3] << 8)); // 32 bit
    }
}
} // namespace

WavResult wavToSlot(Module& module, int slot, const WavInfo& info, const FileIo& io)
{
    if (slot < 0 || slot >= kSampleCount || io.read == nullptr)
        return WavResult::kUnsupported;

    const bool pcm = info.format == 1 && (info.bits == 8 || info.bits == 16 || info.bits == 24 ||
                                          info.bits == 32);
    const bool flt = info.format == 3 && info.bits == 32;
    if ((!pcm && !flt) || info.channels < 1 || info.channels > 8)
        return WavResult::kUnsupported;

    const uint32_t bytesPerSample = info.bits / 8;
    const uint32_t frameBytes = bytesPerSample * info.channels;
    const uint32_t frames = info.dataBytes / frameBytes;
    if (frames == 0)
        return WavResult::kEmpty;

    uint32_t wanted = frames;
    bool truncated = false;
    if (wanted > kMaxSampleBytes)
    {
        wanted = kMaxSampleBytes;
        truncated = true;
    }

    int8_t* dst = module.sampleData(slot);
    uint8_t chunk[kChunkBytes];
    const uint32_t framesPerChunk = kChunkBytes / frameBytes;
    if (framesPerChunk == 0)
        return WavResult::kUnsupported;

    uint32_t done = 0;
    while (done < wanted)
    {
        uint32_t n = wanted - done;
        if (n > framesPerChunk)
            n = framesPerChunk;

        if (io.read(io.ctx, chunk, n * frameBytes) != n * frameBytes)
            return WavResult::kIoError;

        for (uint32_t f = 0; f < n; f++)
        {
            int32_t sum = 0;
            for (uint32_t c = 0; c < info.channels; c++)
                sum += readSigned(chunk + f * frameBytes + c * bytesPerSample, info);
            sum /= (int32_t)info.channels;
            dst[done + f] = (int8_t)(sum >> 8);
        }
        done += n;
    }

    uint32_t length = wanted & ~1u; // sample lengths are even
    if (length < 2)
        length = 2;
    if (wanted < length)
        dst[wanted] = 0;

    Sample& s = module.samples[slot];
    s.length = length;
    s.fineTune = 0;
    s.volume = 64;
    s.loopStart = 0;
    s.loopLength = 2;
    module.fixSample(slot);

    return truncated ? WavResult::kTruncated : WavResult::kOk;
}

bool slotActive(const Module& module, int slot)
{
    return slot >= 0 && slot < kSampleCount && module.samples[slot].length > 2;
}

void clearSlot(Module& module, int slot)
{
    if (slot < 0 || slot >= kSampleCount)
        return;

    memset(&module.samples[slot], 0, sizeof(module.samples[slot]));
    module.samples[slot].loopLength = 2;
    memset(module.sampleData(slot), 0, kSampleSlotBytes);
}

void copySlot(Module& module, int from, int to)
{
    if (from < 0 || from >= kSampleCount || to < 0 || to >= kSampleCount || from == to)
        return;

    module.samples[to] = module.samples[from];
    memcpy(module.sampleData(to), module.sampleData(from), kSampleSlotBytes);
}

void setSlotName(Module& module, int slot, const char* name)
{
    if (slot < 0 || slot >= kSampleCount)
        return;

    memset(module.samples[slot].name, 0, sizeof(module.samples[slot].name));
    strncpy(module.samples[slot].name, name, sizeof(module.samples[slot].name) - 1);
}

void usedSamples(const Module& module, bool used[kSampleCount])
{
    for (int i = 0; i < kSampleCount; i++)
        used[i] = false;

    for (int pos = 0; pos < module.songLength; pos++)
    {
        const Note* p = module.pattern(module.order[pos]);
        for (int i = 0; i < kRowCount * kChannelCount; i++)
        {
            const int s = p[i].sample();
            if (s >= 1 && s <= kSampleCount)
                used[s - 1] = true;
        }
    }
}

} // namespace protracker
