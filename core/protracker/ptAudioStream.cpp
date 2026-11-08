#ifdef PLATFORM_IMX

#include "ptAudioStream.h"

#include "elapsedMillis.h"

void ProTrackerStream::requestPlay(int position, int row)
{
    cmdArgA_ = position;
    cmdArgB_ = row;
    command_ = kPlay; // written last, the audio update reads it first
}

void ProTrackerStream::requestPlayPattern(int pattern, int row)
{
    cmdArgA_ = pattern;
    cmdArgB_ = row;
    command_ = kPlayPattern;
}

void ProTrackerStream::requestStop()
{
    command_ = kStop;
}

void ProTrackerStream::requestPreview(int sampleIndex, uint16_t period)
{
    cmdArgA_ = sampleIndex;
    cmdArgB_ = period;
    command_ = kPreview;
}

void ProTrackerStream::requestPreviewStop()
{
    command_ = kPreviewStop;
}

void ProTrackerStream::requestPreviewData(const int8_t* data,
                                          uint32_t length,
                                          uint16_t period,
                                          uint8_t volume)
{
    cmdPtr_ = data;
    cmdArgA_ = (int)length;
    cmdArgB_ = period;
    cmdArgC_ = volume;
    command_ = kPreviewData;
}

void ProTrackerStream::pause()
{
    pauseRequested_ = true;

    // The audio update is interrupt driven, so it keeps running while we wait here. If it isn't
    // running at all (stream not started yet) we simply time out; nobody else is touching the
    // module in that case.
    elapsedMillis waited;
    while (!paused_ && waited < 50)
    {
    }
}

void ProTrackerStream::resume()
{
    pauseRequested_ = false;
}

void ProTrackerStream::update(void)
{
    if (pauseRequested_)
    {
        paused_ = true;
        return;
    }
    paused_ = false;

    if (replayer_ == nullptr)
        return;

    const Command command = command_;
    if (command != kNone)
    {
        command_ = kNone;
        switch (command)
        {
            case kPlay: replayer_->play(cmdArgA_, cmdArgB_); break;
            case kPlayPattern: replayer_->playPattern(cmdArgA_, cmdArgB_); break;
            case kStop:
                replayer_->stop();
                replayer_->previewStop();
                break;
            case kPreview: replayer_->previewStart(cmdArgA_, (uint16_t)cmdArgB_); break;
            case kPreviewStop: replayer_->previewStop(); break;
            case kPreviewData:
                replayer_->previewStartData(cmdPtr_, (uint32_t)cmdArgA_, (uint16_t)cmdArgB_, (uint8_t)cmdArgC_);
                break;
            default: break;
        }
    }

    if (!enabled_ || (!replayer_->isPlaying() && !replayer_->isPreviewing()))
        return;

    audio_block_t* left = allocate();
    audio_block_t* right = allocate();
    if (left == nullptr || right == nullptr)
    {
        if (left != nullptr)
            release(left);
        if (right != nullptr)
            release(right);
        return;
    }

    replayer_->render(left->data, right->data, AUDIO_BLOCK_SAMPLES);

    transmit(left, 0);
    transmit(right, 1);
    release(left);
    release(right);
}

#endif // PLATFORM_IMX
