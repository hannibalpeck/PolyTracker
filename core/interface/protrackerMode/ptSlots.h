#ifndef INTERFACE_PROTRACKERMODE_PTSLOTS_H_
#define INTERFACE_PROTRACKERMODE_PTSLOTS_H_

#ifdef PLATFORM_IMX

#include "protrackerMode/protrackerMode.h"
#include "ptSamples.h"

// The 31 sample slots of the song, for the sample screens.
inline bool ptActive(int slot)
{
    return protracker::slotActive(protrackerMode.module, slot);
}

inline char* ptName(int slot)
{
    return protrackerMode.module.samples[slot].name;
}

#endif // PLATFORM_IMX

#endif
