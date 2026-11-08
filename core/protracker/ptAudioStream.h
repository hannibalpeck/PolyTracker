// Audio library glue: renders the ProTracker replayer into the firmware's audio graph.
//
// The replayer runs inside the audio update (interrupt level). The UI thread never touches the
// replayer's transport directly: it posts requests here and the audio update applies them, so a
// "play" or "stop" can never land in the middle of a tick.

#pragma once

#ifdef PLATFORM_IMX

#include "Audio.h"
#include "ptReplayer.h"

class ProTrackerStream : public AudioStream
{
  public:
    ProTrackerStream()
      : AudioStream(0, nullptr)
    {
    }

    void attach(protracker::Replayer* replayer) { replayer_ = replayer; }

    // While not enabled the stream costs (almost) no CPU and produces no audio.
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

    // transport requests, applied on the next audio update
    void requestPlay(int position, int row);
    void requestPlayPattern(int pattern, int row);
    void requestStop();

    // Ask the audio update to leave the module alone, and wait (briefly) until it has. Needed
    // before replacing the module's contents (load / new). Call resume() when done.
    void pause();
    void resume();

  private:
    enum Command : uint8_t
    {
        kNone,
        kPlay,
        kPlayPattern,
        kStop,
    };

    virtual void update(void) override;

    protracker::Replayer* replayer_ = nullptr;
    volatile bool enabled_ = false;
    volatile bool pauseRequested_ = false;
    volatile bool paused_ = false;

    volatile int cmdArgA_ = 0;
    volatile int cmdArgB_ = 0;
    volatile Command command_ = kNone;
};

#endif // PLATFORM_IMX
