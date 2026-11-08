#ifndef PROTRACKER_PTSAMPLES_H_
#define PROTRACKER_PTSAMPLES_H_

#include <stdint.h>

#include "ptModule.h"

// Helpers for the 31 sample slots of a module: loading a WAV file into a slot (8-bit mono, like the
// Amiga) and the usual slot operations of the sample screens.
namespace protracker
{

struct WavInfo
{
    uint16_t format = 0;   // 1 = PCM, 3 = IEEE float
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint32_t rate = 0;
    uint32_t dataBytes = 0;
};

enum class WavResult
{
    kOk = 0,
    kTruncated, // loaded, but the file was longer than a slot holds
    kUnsupported,
    kIoError,
    kEmpty,
};

// Reads the sample data of a WAV file (the reader is positioned at the first data byte) and writes
// 8-bit mono into the slot: channels are averaged, nothing is resampled. A slot holds at most
// kMaxSampleBytes. The slot gets full volume, no finetune and no loop; the name is set by the caller.
WavResult wavToSlot(Module& module, int slot, const WavInfo& info, const FileIo& io);

bool slotActive(const Module& module, int slot);
void clearSlot(Module& module, int slot);
void copySlot(Module& module, int from, int to);
void setSlotName(Module& module, int slot, const char* name); // 22 characters, extension is not removed

// which samples are used by a note in any pattern of the order list
void usedSamples(const Module& module, bool used[kSampleCount]);

} // namespace protracker

#endif
