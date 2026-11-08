// ProTracker 2.3D compatible replayer with a small Paula (Amiga audio chip) model.
//
// Pure C++ with no hardware dependencies. The replay logic (effects, tick processing, quirks and
// bugs of the original replayer) is ported from pt2-clone by Olav Sorensen (8bitbubsy),
// https://github.com/8bitbubsy/pt2-clone, BSD 3-Clause License.
//
// Usage:
//     Replayer r;
//     r.init(&module, 44117.647f);
//     r.play(0, 0);
//     r.render(left, right, frames);   // call from the audio update, any number of frames
//
// The replayer reads the module while it plays, so the module can be edited live.

#pragma once

#include "ptModule.h"

namespace protracker
{

class Replayer
{
  public:
    static constexpr int kMinBpm = 32;
    static constexpr int kMaxBpm = 255;

    void init(Module* module, float outputRate);
    void setModule(Module* module);

    // Song mode: play from an order position and row.
    void play(int position, int row);
    // Pattern mode: loop a single pattern.
    void playPattern(int pattern, int row);
    void stop();
    bool isPlaying() const { return playing_; }

    // Audition a sample outside the song, like pressing a key in the editor. Only sounds while the
    // song is not playing; starting playback ends it. A looping sample sustains until
    // previewStop(), others play to their end. `period` is a ProTracker period value.
    void previewStart(int sampleIndex, uint16_t period);
    void previewStop() { previewActive_ = false; }
    bool isPreviewing() const { return previewActive_; }

    // Mixes `frames` stereo samples into left/right (overwrites the buffers).
    void render(int16_t* left, int16_t* right, uint32_t frames);

    // Playback position
    // (the row/position/pattern that is currently sounding)
    int position() const { return curPos_; }
    int row() const { return curRow_; }
    int pattern() const { return curPattern_; }
    int speed() const { return speed_; }
    int bpm() const { return modBpm_; }
    uint32_t songLoops() const { return songLoops_; }

    // Settings
    void setStereoSeparation(int percent); // 0 = mono, 100 = full Amiga panning (default 20)
    void setInterpolation(bool linear) { linear_ = linear; }
    void setMuted(int channel, bool muted) { muted_[channel & 3] = muted; }
    bool isMuted(int channel) const { return muted_[channel & 3]; }
    bool ledFilter() const { return ledFilter_; }

    // 0..64, set when a note triggers and decays while playing (for VU meters)
    uint8_t channelVu(int channel) const { return vu_[channel & 3]; }
    uint16_t channelPeriod(int channel) const { return voice_[channel & 3].period; }

  private:
    // ---------------------------------------------------------------- Paula
    struct Voice
    {
        const int8_t* regPtr = nullptr; // written by the replayer, latched on DMA start / loop
        uint16_t regLenWords = 1;
        uint16_t period = 0;
        uint8_t volume = 0;

        bool active = false;
        const int8_t* curPtr = nullptr;
        uint32_t curLenBytes = 2;
        uint32_t pos = 0;
        uint32_t frac = 0; // Q24
        uint32_t delta = 0; // Q24 per output sample
    };

    void paulaSetPtr(int v, const int8_t* ptr);
    void paulaSetLen(int v, uint16_t words);
    void paulaSetPeriod(int v, uint16_t period);
    void paulaSetVolume(int v, uint8_t volume);
    void paulaDmaOn(int v);
    void paulaDmaOff(int v);
    void mix(int16_t* left, int16_t* right, uint32_t frames);
    void mixPreview(int16_t* left, int16_t* right, uint32_t frames);
    void addPreview(int16_t* left, int16_t* right, uint32_t frames);
    int32_t nextPreviewSample();

    // -------------------------------------------------------------- replayer
    struct Channel
    {
        int8_t* start = nullptr;
        int8_t* waveStart = nullptr;
        int8_t* loopStart = nullptr;
        int8_t volume = 0;
        uint8_t dmaBit = 0;
        int8_t tonePortDirec = 0;
        int8_t pattPos = 0;
        int8_t loopCount = 0;
        uint8_t waveControl = 0;
        uint8_t glissFunk = 0;
        uint8_t sampleOffset = 0;
        uint8_t tonePortSpeed = 0;
        uint8_t vibratoCmd = 0;
        uint8_t tremoloCmd = 0;
        uint8_t fineTune = 0;
        uint8_t funkOffset = 0;
        uint8_t sampleNum = 0;
        uint8_t vibratoPos = 0;
        uint8_t tremoloPos = 0;
        int16_t period = 0;
        int16_t note = 0;
        int16_t wantedPeriod = 0;
        uint16_t cmd = 0;
        uint16_t length = 0;
        uint16_t repLen = 0;
        uint8_t index = 0;
    };

    void resetChannels();
    void setSpeed(int speed);
    void setTempo(int bpm);
    void startPlayback(bool patternMode, int pattern, int position, int row);
    void tick();
    void nextPosition();
    void playVoice(Channel& ch);
    void setPeriod(Channel& ch);
    void setDma();
    void checkEffects(Channel& ch);
    void chkEfx2(Channel& ch);
    void checkMoreEffects(Channel& ch);
    void eCommands(Channel& ch);
    void doRetrig(Channel& ch);
    void retrigNote(Channel& ch);
    void arpeggio(Channel& ch);
    void portaUp(Channel& ch);
    void portaDown(Channel& ch);
    void setTonePorta(Channel& ch);
    void tonePortNoChange(Channel& ch);
    void tonePortamento(Channel& ch);
    void vibrato(Channel& ch);
    void vibrato2(Channel& ch);
    void tremolo(Channel& ch);
    void sampleOffset(Channel& ch);
    void volumeSlide(Channel& ch);
    void updateFunk(Channel& ch);
    void jumpLoop(Channel& ch);
    void turnOffVoices();
    void setVu(Channel& ch);

    Module* mod_ = nullptr;
    float outputRate_ = 44100.0f;

    Voice voice_[kChannelCount];
    Channel ch_[kChannelCount];
    uint8_t vu_[kChannelCount] = {};
    bool muted_[kChannelCount] = {};

    bool playing_ = false;
    bool patternMode_ = false;

    int speed_ = 6;
    int tick_ = 0;
    int row_ = 0;
    int modPos_ = 0;
    int modPattern_ = 0;
    int curPos_ = 0;
    int curRow_ = 0;
    int curPattern_ = 0;
    int modBpm_ = 125;
    int ciaSetBpm_ = -1;
    uint8_t dmaConTemp_ = 0;
    int pBreakPosition_ = 0;
    bool pBreakFlag_ = false;
    bool posJumpAssert_ = false;
    bool doStopSong_ = false;
    uint8_t pattDelTime_ = 0;
    uint8_t pattDelTime2_ = 0;
    uint8_t lowMask_ = 0xFF;
    uint32_t songLoops_ = 0;

    // mixing
    uint64_t samplesPerTickQ32_ = 0;
    uint64_t tickAccQ32_ = 0;
    uint32_t samplesToNextTick_ = 0;
    uint32_t clockOverRateQ24_ = 0;
    int weightL_[kChannelCount] = {};
    int weightR_[kChannelCount] = {};
    bool linear_ = true;

    // sample audition
    const int8_t* previewPtr_ = nullptr;
    uint32_t previewLen_ = 0;
    uint32_t previewLoopStart_ = 0;
    uint32_t previewLoopEnd_ = 0; // 0 = no loop
    uint32_t previewPos_ = 0;
    uint32_t previewFrac_ = 0;
    uint32_t previewDelta_ = 0;
    uint8_t previewVolume_ = 0;
    volatile bool previewActive_ = false;

    // "LED" filter (E0x): 2-pole Butterworth low-pass
    bool ledFilter_ = false;
    float ledB0_ = 0, ledB1_ = 0, ledB2_ = 0, ledA1_ = 0, ledA2_ = 0;
    float ledZ1_[2] = {};
    float ledZ2_[2] = {};
};

} // namespace protracker
