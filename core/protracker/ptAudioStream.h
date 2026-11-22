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
    // Switch to another replayer while audio is running: the audio update is paused around the
    // switch and a request still pending for the old replayer is dropped.
    void switchTo(protracker::Replayer* replayer);
    protracker::Replayer* attached() const { return replayer_; }

    // While not enabled the stream costs (almost) no CPU and produces no audio.
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

    // transport requests, applied on the next audio update
    void requestPlay(int position, int row);
    void requestPlayPattern(int pattern, int row);
    void requestSwitchPattern(int pattern);
    void requestStop();
    // audition a sample (0 based index) at a period, see Replayer::previewStart
    void requestPreview(int sampleIndex, uint16_t period);
    void requestPreviewStop();
    // audition 8 bit data outside the song (sample editor), see Replayer::previewStartData
    void requestPreviewData(const int8_t* data, uint32_t length, uint16_t period, uint8_t volume);
    // how far the preview has played, 0..1000 (0 when it isn't playing)
    uint32_t previewPosition() const { return replayer_ != nullptr ? replayer_->previewPosition() : 0; }
    uint32_t previewLength() const { return replayer_ != nullptr ? replayer_->previewLength() : 0; }
    bool isPreviewing() const { return replayer_ != nullptr && replayer_->isPreviewing(); }

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
        kSwitchPattern,
        kStop,
        kPreview,
        kPreviewStop,
        kPreviewData,
    };

    virtual void update(void) override;

    protracker::Replayer* replayer_ = nullptr;
    volatile bool enabled_ = false;
    volatile bool pauseRequested_ = false;
    volatile bool paused_ = false;

    volatile int cmdArgA_ = 0;
    volatile int cmdArgB_ = 0;
    volatile int cmdArgC_ = 0;
    const int8_t* volatile cmdPtr_ = nullptr;
    volatile Command command_ = kNone;
};

#endif // PLATFORM_IMX
