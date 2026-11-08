// See ptReplayer.h for credits.

#include "ptReplayer.h"

#include "ptTables.h"

#include <math.h>
#include <string.h>

namespace protracker
{

namespace
{
constexpr float kPaulaClockPal = 3546895.0f;
constexpr float kLedFilterHz = 3275.5f;

// used for voices that have no sample data
int8_t gNullSample[4] = { 0, 0, 0, 0 };

inline int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}
} // namespace

// ============================================================================================
//  Paula
// ============================================================================================

void Replayer::paulaSetPtr(int v, const int8_t* ptr)
{
    voice_[v].regPtr = ptr;
}

void Replayer::paulaSetLen(int v, uint16_t words)
{
    voice_[v].regLenWords = words;
}

void Replayer::paulaSetPeriod(int v, uint16_t period)
{
    Voice& vo = voice_[v];
    vo.period = period;

    // Paula can't fetch data faster than period 113, and period 0 behaves like 65536
    uint32_t real = period;
    if (real == 0)
        real = 65536;
    else if (real < 113)
        real = 113;

    vo.delta = clockOverRateQ24_ / real;
}

void Replayer::paulaSetVolume(int v, uint8_t volume)
{
    voice_[v].volume = volume > 64 ? 64 : volume;
}

void Replayer::paulaDmaOn(int v)
{
    Voice& vo = voice_[v];

    vo.curPtr = vo.regPtr != nullptr ? vo.regPtr : gNullSample;
    vo.curLenBytes = vo.regPtr != nullptr ? (uint32_t)vo.regLenWords * 2 : 2;
    if (vo.regLenWords == 0)
    {
        // a length of zero means 65536 words on real hardware, ProTracker avoids it
        vo.curPtr = gNullSample;
        vo.curLenBytes = 2;
    }

    vo.pos = 0;
    vo.frac = 0;
    vo.active = true;
}

void Replayer::paulaDmaOff(int v)
{
    voice_[v].active = false;
}

void Replayer::mix(int16_t* left, int16_t* right, uint32_t frames)
{
    for (uint32_t f = 0; f < frames; f++)
    {
        int32_t accL = 0;
        int32_t accR = 0;

        for (int v = 0; v < kChannelCount; v++)
        {
            Voice& vo = voice_[v];
            if (!vo.active)
                continue;

            if (!muted_[v] && vo.volume != 0)
            {
                int32_t s = (int32_t)vo.curPtr[vo.pos] * 256;

                if (linear_)
                {
                    int32_t next;
                    uint32_t nextPos = vo.pos + 1;
                    if (nextPos >= vo.curLenBytes)
                    {
                        // the sample is about to loop (or reload), so the next value comes from there
                        next = vo.regPtr != nullptr && vo.regLenWords != 0 ? vo.regPtr[0] : 0;
                    }
                    else
                    {
                        next = vo.curPtr[nextPos];
                    }

                    s += (next - vo.curPtr[vo.pos]) * (int32_t)(vo.frac >> 16);
                }

                s *= vo.volume;
                accL += s * weightL_[v];
                accR += s * weightR_[v];
            }

            // advance the voice
            vo.frac += vo.delta;
            if (vo.frac >= (1u << 24))
            {
                vo.pos += vo.frac >> 24;
                vo.frac &= (1u << 24) - 1;

                while (vo.pos >= vo.curLenBytes)
                {
                    vo.pos -= vo.curLenBytes;

                    // reload from the (loop) registers
                    if (vo.regPtr != nullptr && vo.regLenWords != 0)
                    {
                        vo.curPtr = vo.regPtr;
                        vo.curLenBytes = (uint32_t)vo.regLenWords * 2;
                    }
                    else
                    {
                        vo.curPtr = gNullSample;
                        vo.curLenBytes = 2;
                    }
                }
            }
        }

        // accumulators hold sample(Q8) * volume(64) * weight(Q7)
        int32_t outL = accL >> 15;
        int32_t outR = accR >> 15;

        if (ledFilter_)
        {
            float x[2] = { (float)outL, (float)outR };
            for (int c = 0; c < 2; c++)
            {
                float y = ledB0_ * x[c] + ledZ1_[c];
                ledZ1_[c] = ledB1_ * x[c] - ledA1_ * y + ledZ2_[c];
                ledZ2_[c] = ledB2_ * x[c] - ledA2_ * y;
                x[c] = y;
            }
            outL = (int32_t)lrintf(x[0]);
            outR = (int32_t)lrintf(x[1]);
        }

        left[f] = (int16_t)clampInt(outL, -32768, 32767);
        right[f] = (int16_t)clampInt(outR, -32768, 32767);
    }
}

// ============================================================================================
//  Setup and transport
// ============================================================================================

void Replayer::init(Module* module, float outputRate)
{
    mod_ = module;
    outputRate_ = outputRate;
    clockOverRateQ24_ = (uint32_t)(kPaulaClockPal / outputRate * 16777216.0f);

    // LED filter coefficients (RBJ biquad low-pass, Q = 1/sqrt(2))
    const float w0 = 2.0f * 3.14159265358979f * kLedFilterHz / outputRate;
    const float alpha = sinf(w0) / (2.0f * 0.70710678f);
    const float cosw = cosf(w0);
    const float a0 = 1.0f + alpha;
    ledB0_ = ((1.0f - cosw) / 2.0f) / a0;
    ledB1_ = (1.0f - cosw) / a0;
    ledB2_ = ledB0_;
    ledA1_ = (-2.0f * cosw) / a0;
    ledA2_ = (1.0f - alpha) / a0;

    for (int i = 0; i < kChannelCount; i++)
    {
        voice_[i] = Voice();
        paulaSetPeriod(i, 0);
    }

    setStereoSeparation(20);
    resetChannels();
    playing_ = false;
}

void Replayer::setModule(Module* module)
{
    stop();
    mod_ = module;
    resetChannels();
}

void Replayer::setStereoSeparation(int percent)
{
    percent = clampInt(percent, 0, 100);

    // Amiga: channels 0 and 3 are left, 1 and 2 are right
    const int strong = (128 * (100 + percent) + 100) / 200;
    const int weak = 128 - strong;

    for (int v = 0; v < kChannelCount; v++)
    {
        const bool leftChannel = (v == 0 || v == 3);
        weightL_[v] = leftChannel ? strong : weak;
        weightR_[v] = leftChannel ? weak : strong;
    }
}

void Replayer::resetChannels()
{
    for (int i = 0; i < kChannelCount; i++)
    {
        ch_[i] = Channel();
        ch_[i].index = (uint8_t)i;
        ch_[i].dmaBit = (uint8_t)(1 << i);
        vu_[i] = 0;
    }
}

void Replayer::setSpeed(int speed)
{
    speed_ = speed;
    tick_ = 0;
}

void Replayer::setTempo(int bpm)
{
    if (bpm < kMinBpm || bpm > kMaxBpm)
        return;

    modBpm_ = bpm;

    // samples per tick = rate * 2.5 / bpm, in Q32
    const double spt = (double)outputRate_ * 2.5 / (double)bpm;
    samplesPerTickQ32_ = (uint64_t)(spt * 4294967296.0);
}

void Replayer::turnOffVoices()
{
    for (int i = 0; i < kChannelCount; i++)
    {
        paulaDmaOff(i);
        paulaSetVolume(i, 0);
        vu_[i] = 0;
    }
}

void Replayer::stop()
{
    playing_ = false;
    pattDelTime_ = pattDelTime2_ = 0;
    doStopSong_ = false;
    turnOffVoices();
}

void Replayer::play(int position, int row)
{
    startPlayback(false, -1, position, row);
}

void Replayer::playPattern(int pattern, int row)
{
    startPlayback(true, pattern, 0, row);
}

void Replayer::startPlayback(bool patternMode, int pattern, int position, int row)
{
    if (mod_ == nullptr || !mod_->isAttached())
        return;

    resetChannels();
    turnOffVoices();

    pattDelTime_ = pattDelTime2_ = 0;
    pBreakPosition_ = 0;
    pBreakFlag_ = false;
    posJumpAssert_ = false;
    doStopSong_ = false;
    lowMask_ = 0xFF;
    ciaSetBpm_ = -1;
    songLoops_ = 0;
    ledFilter_ = false;
    ledZ1_[0] = ledZ1_[1] = ledZ2_[0] = ledZ2_[1] = 0.0f;

    patternMode_ = patternMode;
    row_ = (row >= 0 && row < kRowCount) ? row : 0;

    if (!patternMode)
    {
        modPos_ = (position >= 0 && position < mod_->songLength) ? position : 0;
        modPattern_ = mod_->order[modPos_];
    }
    else
    {
        modPos_ = 0;
        modPattern_ = pattern;
    }
    modPattern_ = clampInt(modPattern_, 0, kMaxPatterns - 1);

    setTempo(mod_->initialTempo);
    setSpeed(6);
    tick_ = speed_ - 1; // so that the first tick reads a new row

    tickAccQ32_ = 0;
    samplesToNextTick_ = 0;
    playing_ = true;
}

// ============================================================================================
//  Rendering
// ============================================================================================

void Replayer::render(int16_t* left, int16_t* right, uint32_t frames)
{
    while (frames > 0)
    {
        if (!playing_)
        {
            memset(left, 0, frames * sizeof(int16_t));
            memset(right, 0, frames * sizeof(int16_t));
            return;
        }

        if (samplesToNextTick_ == 0)
        {
            tick();
            if (!playing_)
                continue; // stopped by F00, the next loop iteration fills with silence

            tickAccQ32_ += samplesPerTickQ32_;
            samplesToNextTick_ = (uint32_t)(tickAccQ32_ >> 32);
            tickAccQ32_ &= 0xFFFFFFFFull;
            if (samplesToNextTick_ == 0)
                samplesToNextTick_ = 1;
        }

        uint32_t n = frames < samplesToNextTick_ ? frames : samplesToNextTick_;
        mix(left, right, n);

        left += n;
        right += n;
        frames -= n;
        samplesToNextTick_ -= n;
    }
}

// ============================================================================================
//  Tick processing (ported from the ProTracker 2.3D replayer as implemented in pt2-clone)
// ============================================================================================

void Replayer::setVu(Channel& ch)
{
    vu_[ch.index] = (uint8_t)(ch.volume > 0 ? ch.volume : 0);
}

void Replayer::tick()
{
    // quirk: CIA BPM changes are delayed by one tick in ProTracker
    if (ciaSetBpm_ != -1)
    {
        setTempo(ciaSetBpm_);
        ciaSetBpm_ = -1;
    }

    for (int i = 0; i < kChannelCount; i++)
    {
        if (vu_[i] > 0)
            vu_[i]--;
    }

    tick_++;

    bool readNewNote = false;
    if (tick_ >= speed_)
    {
        tick_ = 0;
        readNewNote = true;
    }

    if (readNewNote) // tick 0
    {
        if (pattDelTime2_ == 0) // no pattern delay, time to read note data
        {
            dmaConTemp_ = 0;

            curPos_ = modPos_;
            curRow_ = row_;
            curPattern_ = modPattern_;

            for (int i = 0; i < kChannelCount; i++)
            {
                playVoice(ch_[i]);
                paulaSetVolume(i, (uint8_t)ch_[i].volume);
            }

            setDma();
        }
        else // pattern delay is on-going
        {
            for (int i = 0; i < kChannelCount; i++)
                checkEffects(ch_[i]);
        }

        // increase row
        row_++;

        if (pattDelTime_ > 0)
        {
            pattDelTime2_ = pattDelTime_;
            pattDelTime_ = 0;
        }

        // undo row increase if pattern delay is on-going
        if (pattDelTime2_ > 0)
        {
            pattDelTime2_--;
            if (pattDelTime2_ > 0)
                row_--;
        }

        if (pBreakFlag_)
        {
            row_ = pBreakPosition_;
            pBreakPosition_ = 0;
            pBreakFlag_ = false;
        }

        if (row_ >= kRowCount || posJumpAssert_)
            nextPosition();
    }
    else // tick > 0 (handle effects)
    {
        for (int i = 0; i < kChannelCount; i++)
            checkEffects(ch_[i]);

        if (posJumpAssert_)
            nextPosition();
    }

    // command F00 = stop song
    if (doStopSong_)
    {
        doStopSong_ = false;
        stop();
    }
}

void Replayer::nextPosition()
{
    row_ = pBreakPosition_;
    pBreakPosition_ = 0;
    posJumpAssert_ = false;

    if (!patternMode_)
    {
        modPos_ = (modPos_ + 1) & 127;
        if (modPos_ >= mod_->songLength)
        {
            modPos_ = 0;
            songLoops_++;
        }

        modPattern_ = mod_->order[modPos_];
        if (modPattern_ > kMaxPatterns - 1)
            modPattern_ = kMaxPatterns - 1;
    }
}

void Replayer::setDma()
{
    for (int i = 0; i < kChannelCount; i++)
    {
        if (muted_[i])
            dmaConTemp_ &= (uint8_t)~(1 << i);
    }

    // start DMAs for selected voices
    for (int i = 0; i < kChannelCount; i++)
    {
        if (dmaConTemp_ & (1 << i))
        {
            paulaDmaOn(i);
            setVu(ch_[i]);
        }
    }

    // set new voice data ptr and length (these take effect after the current DMA cycle is done)
    for (int i = 0; i < kChannelCount; i++)
    {
        paulaSetPtr(i, ch_[i].loopStart);
        paulaSetLen(i, ch_[i].repLen);
    }
}

void Replayer::playVoice(Channel& ch)
{
    const int idx = ch.index;

    if (ch.note == 0 && ch.cmd == 0)
        paulaSetPeriod(idx, (uint16_t)ch.period);

    const Note note = mod_->pattern(modPattern_)[(row_ * kChannelCount) + idx];

    ch.note = (int16_t)note.period();
    ch.cmd = (uint16_t)((note.command() << 8) | note.param());

    const uint8_t sampleNumber = note.sample();
    if (sampleNumber >= 1 && sampleNumber <= kSampleCount)
    {
        ch.sampleNum = (uint8_t)(sampleNumber - 1);
        const Sample& s = mod_->samples[ch.sampleNum];

        ch.start = mod_->sampleData(ch.sampleNum);
        ch.fineTune = s.fineTune & 0xF;
        ch.volume = (int8_t)s.volume;
        ch.length = (uint16_t)(s.length >> 1);
        ch.repLen = (uint16_t)(s.loopLength >> 1);

        const uint16_t repeat = (uint16_t)(s.loopStart >> 1);
        if (repeat > 0)
        {
            ch.loopStart = ch.start + ((uint32_t)repeat << 1);
            ch.waveStart = ch.loopStart;
            ch.length = (uint16_t)(repeat + ch.repLen);
        }
        else
        {
            ch.loopStart = ch.start;
            ch.waveStart = ch.start;
        }

        // set safe sample space for empty samples
        if (ch.length == 0)
            ch.loopStart = ch.waveStart = gNullSample;
    }

    if ((ch.note & 0xFFF) > 0)
    {
        if ((ch.cmd & 0xFF0) == 0xE50) // set finetune
        {
            ch.fineTune = ch.cmd & 0xF;
            setPeriod(ch);
        }
        else
        {
            const uint8_t cmd = (ch.cmd & 0x0F00) >> 8;
            if (cmd == 3 || cmd == 5)
            {
                setVu(ch);
                setTonePorta(ch);
                checkMoreEffects(ch);
            }
            else if (cmd == 9)
            {
                checkMoreEffects(ch);
                setPeriod(ch);
            }
            else
            {
                setPeriod(ch);
            }
        }
    }
    else
    {
        checkMoreEffects(ch);
    }
}

void Replayer::setPeriod(Channel& ch)
{
    const uint16_t note = (uint16_t)(ch.note & 0xFFF);

    int i;
    for (i = 0; i < 37; i++)
    {
        // kPeriodTable[36] = 0, so i = 36 is safe
        if (note >= kPeriodTable[i])
            break;
    }

    // it's safe if i = 37 because of zero padding
    ch.period = kPeriodTable[(ch.fineTune * 37) + i];

    if ((ch.cmd & 0xFF0) != 0xED0) // no note delay
    {
        const int idx = ch.index;

        paulaDmaOff(idx);

        if ((ch.waveControl & 0x04) == 0)
            ch.vibratoPos = 0;
        if ((ch.waveControl & 0x40) == 0)
            ch.tremoloPos = 0;

        paulaSetLen(idx, ch.length);
        paulaSetPtr(idx, ch.start);

        if (ch.start == nullptr)
        {
            ch.loopStart = nullptr;
            paulaSetLen(idx, 1);
            ch.repLen = 1;
        }

        paulaSetPeriod(idx, (uint16_t)ch.period);

        dmaConTemp_ |= ch.dmaBit;
    }

    checkMoreEffects(ch);
}

void Replayer::checkEffects(Channel& ch)
{
    if (muted_[ch.index])
        return;

    chkEfx2(ch);

    // volume is not updated after tremolo (the tremolo routine does it itself)
    const uint8_t cmd = (ch.cmd & 0x0F00) >> 8;
    if (cmd != 0x7)
        paulaSetVolume(ch.index, (uint8_t)ch.volume);
}

void Replayer::chkEfx2(Channel& ch)
{
    updateFunk(ch);

    if ((ch.cmd & 0xFFF) == 0)
        return;

    const uint8_t cmd = (ch.cmd & 0x0F00) >> 8;
    switch (cmd)
    {
        case 0x0: arpeggio(ch); return;
        case 0x1: portaUp(ch); return;
        case 0x2: portaDown(ch); return;
        case 0x3: tonePortamento(ch); return;
        case 0x4: vibrato(ch); return;
        case 0x5:
            tonePortNoChange(ch);
            volumeSlide(ch);
            return;
        case 0x6:
            vibrato2(ch);
            volumeSlide(ch);
            return;
        case 0xE: eCommands(ch); return;
        default: break;
    }

    paulaSetPeriod(ch.index, (uint16_t)ch.period);

    if (cmd == 0x7)
        tremolo(ch);
    else if (cmd == 0xA)
        volumeSlide(ch);
}

void Replayer::checkMoreEffects(Channel& ch)
{
    const uint8_t cmd = (ch.cmd & 0x0F00) >> 8;
    switch (cmd)
    {
        case 0x9: sampleOffset(ch); return;
        case 0xB: // position jump
            if (!patternMode_)
                modPos_ = (ch.cmd & 0xFF) - 1; // B00 results in -1, but it safely jumps to order 0
            pBreakPosition_ = 0;
            posJumpAssert_ = true;
            return;
        case 0xD: // pattern break
            pBreakPosition_ = (((ch.cmd & 0xF0) >> 4) * 10) + (ch.cmd & 0x0F);
            if ((uint8_t)pBreakPosition_ > 63)
                pBreakPosition_ = 0;
            posJumpAssert_ = true;
            return;
        case 0xE: eCommands(ch); return;
        case 0xF: // set speed / tempo
            if ((ch.cmd & 0xFF) > 0)
            {
                if ((ch.cmd & 0xFF) < 32)
                    setSpeed(ch.cmd & 0xFF);
                else
                    ciaSetBpm_ = ch.cmd & 0xFF; // CIA applies the new timer value on its next interrupt
            }
            else
            {
                doStopSong_ = true; // F00
            }
            return;
        default: break;
    }

    if (muted_[ch.index])
        return;

    if (cmd == 0xC) // set volume
    {
        ch.volume = (int8_t)(ch.cmd & 0xFF);
        if ((uint8_t)ch.volume > 64)
            ch.volume = 64;
        return;
    }

    paulaSetPeriod(ch.index, (uint16_t)ch.period);
}

void Replayer::eCommands(Channel& ch)
{
    const uint8_t ecmd = (ch.cmd & 0x00F0) >> 4;
    switch (ecmd)
    {
        case 0x0: // filter on/off
            if (tick_ == 0)
            {
                const bool filterOn = ((ch.cmd & 1) ^ 1) != 0;
                if (filterOn != ledFilter_)
                {
                    ledFilter_ = filterOn;
                    ledZ1_[0] = ledZ1_[1] = ledZ2_[0] = ledZ2_[1] = 0.0f;
                }
            }
            return;
        case 0x1: // fine portamento up
            if (tick_ == 0)
            {
                lowMask_ = 0xF;
                portaUp(ch);
            }
            return;
        case 0x2: // fine portamento down
            if (tick_ == 0)
            {
                lowMask_ = 0xF;
                portaDown(ch);
            }
            return;
        case 0x3: ch.glissFunk = (ch.glissFunk & 0xF0) | (ch.cmd & 0x0F); return;
        case 0x4: ch.waveControl = (ch.waveControl & 0xF0) | (ch.cmd & 0x0F); return;
        case 0x5: ch.fineTune = ch.cmd & 0xF; return;
        case 0x6: jumpLoop(ch); return;
        case 0x7: ch.waveControl = (uint8_t)(((ch.cmd & 0xF) << 4) | (ch.waveControl & 0xF)); return;
        case 0x8: return; // E8x (Karplus-Strong) trashes sample data and is almost never used as such
        case 0xE: // pattern delay
            if (tick_ == 0 && pattDelTime2_ == 0)
                pattDelTime_ = (uint8_t)((ch.cmd & 0xF) + 1);
            return;
        default: break;
    }

    if (muted_[ch.index])
        return;

    switch (ecmd)
    {
        case 0x9: retrigNote(ch); return;
        case 0xA: // fine volume up
            if (tick_ == 0)
            {
                ch.volume = (int8_t)(ch.volume + (ch.cmd & 0xF));
                if (ch.volume > 64)
                    ch.volume = 64;
            }
            return;
        case 0xB: // fine volume down
            if (tick_ == 0)
            {
                ch.volume = (int8_t)(ch.volume - (ch.cmd & 0xF));
                if (ch.volume < 0)
                    ch.volume = 0;
            }
            return;
        case 0xC: // note cut
            if (tick_ == (ch.cmd & 0xF))
                ch.volume = 0;
            return;
        case 0xD: // note delay
            if (tick_ == (ch.cmd & 0xF) && (ch.note & 0xFFF) > 0)
                doRetrig(ch);
            return;
        case 0xF: // invert loop (funk repeat)
            if (tick_ == 0)
            {
                ch.glissFunk = (uint8_t)(((ch.cmd & 0xF) << 4) | (ch.glissFunk & 0xF));
                if ((ch.glissFunk & 0xF0) > 0)
                    updateFunk(ch);
            }
            return;
        default: break;
    }
}

void Replayer::doRetrig(Channel& ch)
{
    const int idx = ch.index;

    paulaDmaOff(idx);
    paulaSetPtr(idx, ch.start);
    paulaSetLen(idx, ch.length);
    paulaSetPeriod(idx, (uint16_t)ch.period);
    paulaDmaOn(idx);

    // these take effect after the current DMA cycle is done
    paulaSetPtr(idx, ch.loopStart);
    paulaSetLen(idx, ch.repLen);

    setVu(ch);
}

void Replayer::retrigNote(Channel& ch)
{
    if ((ch.cmd & 0xF) > 0)
    {
        if (tick_ == 0 && (ch.note & 0xFFF) > 0)
            return;

        if (tick_ % (ch.cmd & 0xF) == 0)
            doRetrig(ch);
    }
}

void Replayer::arpeggio(Channel& ch)
{
    int arpNote;

    const int arpTick = tick_ % 3;
    if (arpTick == 1)
    {
        arpNote = ch.cmd >> 4;
    }
    else if (arpTick == 2)
    {
        arpNote = ch.cmd & 0xF;
    }
    else
    {
        paulaSetPeriod(ch.index, (uint16_t)ch.period);
        return;
    }

    // With a -1 finetune this can read up to 15 words past the section; the table is padded
    // with the values the original ProTracker finds there.
    const int16_t* periods = &kPeriodTable[ch.fineTune * 37];
    for (int baseNote = 0; baseNote < 37; baseNote++)
    {
        if (ch.period >= periods[baseNote])
        {
            paulaSetPeriod(ch.index, (uint16_t)periods[baseNote + arpNote]);
            break;
        }
    }
}

void Replayer::portaUp(Channel& ch)
{
    ch.period = (int16_t)(ch.period - ((ch.cmd & 0xFF) & lowMask_));
    lowMask_ = 0xFF;

    if ((ch.period & 0xFFF) < 113) // original PT bug: sign removed before comparison
        ch.period = (int16_t)((ch.period & 0xF000) | 113);

    paulaSetPeriod(ch.index, (uint16_t)(ch.period & 0xFFF));
}

void Replayer::portaDown(Channel& ch)
{
    ch.period = (int16_t)(ch.period + ((ch.cmd & 0xFF) & lowMask_));
    lowMask_ = 0xFF;

    if ((ch.period & 0xFFF) > 856)
        ch.period = (int16_t)((ch.period & 0xF000) | 856);

    paulaSetPeriod(ch.index, (uint16_t)(ch.period & 0xFFF));
}

void Replayer::setTonePorta(Channel& ch)
{
    const uint16_t note = (uint16_t)(ch.note & 0xFFF);
    const int16_t* portaPointer = &kPeriodTable[ch.fineTune * 37];

    int i = 0;
    while (true)
    {
        if (note >= portaPointer[i])
            break;

        if (++i >= 37)
        {
            i = 35;
            break;
        }
    }

    if ((ch.fineTune & 8) && i > 0)
        i--;

    ch.wantedPeriod = portaPointer[i];
    ch.tonePortDirec = 0;

    if (ch.period == ch.wantedPeriod)
        ch.wantedPeriod = 0;
    else if (ch.period > ch.wantedPeriod)
        ch.tonePortDirec = 1;
}

void Replayer::tonePortNoChange(Channel& ch)
{
    if (ch.wantedPeriod <= 0)
        return;

    if (ch.tonePortDirec > 0)
    {
        ch.period = (int16_t)(ch.period - ch.tonePortSpeed);
        if (ch.period <= ch.wantedPeriod)
        {
            ch.period = ch.wantedPeriod;
            ch.wantedPeriod = 0;
        }
    }
    else
    {
        ch.period = (int16_t)(ch.period + ch.tonePortSpeed);
        if (ch.period >= ch.wantedPeriod)
        {
            ch.period = ch.wantedPeriod;
            ch.wantedPeriod = 0;
        }
    }

    if ((ch.glissFunk & 0xF) == 0)
    {
        paulaSetPeriod(ch.index, (uint16_t)ch.period);
    }
    else
    {
        const int16_t* portaPointer = &kPeriodTable[ch.fineTune * 37];

        int i = 0;
        while (true)
        {
            if (ch.period >= portaPointer[i])
                break;

            if (++i >= 37)
            {
                i = 35;
                break;
            }
        }

        paulaSetPeriod(ch.index, (uint16_t)portaPointer[i]);
    }
}

void Replayer::tonePortamento(Channel& ch)
{
    if ((ch.cmd & 0xFF) > 0)
    {
        ch.tonePortSpeed = ch.cmd & 0xFF;
        ch.cmd &= 0xFF00;
    }

    tonePortNoChange(ch);
}

void Replayer::vibrato(Channel& ch)
{
    if ((ch.cmd & 0x0F) > 0)
        ch.vibratoCmd = (uint8_t)((ch.vibratoCmd & 0xF0) | (ch.cmd & 0x0F));

    if ((ch.cmd & 0xF0) > 0)
        ch.vibratoCmd = (uint8_t)((ch.cmd & 0xF0) | (ch.vibratoCmd & 0x0F));

    vibrato2(ch);
}

void Replayer::vibrato2(Channel& ch)
{
    uint16_t vibratoData;

    const uint8_t vibratoPos = (ch.vibratoPos >> 2) & 0x1F;
    const uint8_t vibratoType = ch.waveControl & 3;

    if (vibratoType == 0) // sine
    {
        vibratoData = kVibratoTable[vibratoPos];
    }
    else if (vibratoType == 1) // ramp
    {
        if (ch.vibratoPos < 128)
            vibratoData = (uint16_t)(vibratoPos << 3);
        else
            vibratoData = (uint16_t)(255 - (vibratoPos << 3));
    }
    else // square
    {
        vibratoData = 255;
    }

    vibratoData = (uint16_t)((vibratoData * (ch.vibratoCmd & 0xF)) >> 7);

    if (ch.vibratoPos < 128)
        vibratoData = (uint16_t)(ch.period + vibratoData);
    else
        vibratoData = (uint16_t)(ch.period - vibratoData);

    paulaSetPeriod(ch.index, vibratoData);

    ch.vibratoPos = (uint8_t)(ch.vibratoPos + ((ch.vibratoCmd >> 2) & 0x3C));
}

void Replayer::tremolo(Channel& ch)
{
    int16_t tremoloData;

    if ((ch.cmd & 0x0F) > 0)
        ch.tremoloCmd = (uint8_t)((ch.tremoloCmd & 0xF0) | (ch.cmd & 0x0F));

    if ((ch.cmd & 0xF0) > 0)
        ch.tremoloCmd = (uint8_t)((ch.cmd & 0xF0) | (ch.tremoloCmd & 0x0F));

    const uint8_t tremoloPos = (ch.tremoloPos >> 2) & 0x1F;
    const uint8_t tremoloType = (ch.waveControl >> 4) & 3;

    if (tremoloType == 0) // sine
    {
        tremoloData = kVibratoTable[tremoloPos];
    }
    else if (tremoloType == 1) // ramp
    {
        if (ch.vibratoPos < 128) // original PT bug, should have been tremoloPos
            tremoloData = (int16_t)(tremoloPos << 3);
        else
            tremoloData = (int16_t)(255 - (tremoloPos << 3));
    }
    else // square
    {
        tremoloData = 255;
    }

    tremoloData = (int16_t)(((uint16_t)tremoloData * (ch.tremoloCmd & 0xF)) >> 6);

    if (ch.tremoloPos < 128)
    {
        tremoloData = (int16_t)(ch.volume + tremoloData);
        if (tremoloData > 64)
            tremoloData = 64;
    }
    else
    {
        tremoloData = (int16_t)(ch.volume - tremoloData);
        if (tremoloData < 0)
            tremoloData = 0;
    }

    paulaSetVolume(ch.index, (uint8_t)tremoloData);

    ch.tremoloPos = (uint8_t)(ch.tremoloPos + ((ch.tremoloCmd >> 2) & 0x3C));
}

void Replayer::sampleOffset(Channel& ch)
{
    if ((ch.cmd & 0xFF) > 0)
        ch.sampleOffset = ch.cmd & 0xFF;

    const uint16_t newOffset = (uint16_t)(ch.sampleOffset << 7);
    if (newOffset < ch.length)
    {
        ch.length = (uint16_t)(ch.length - newOffset);
        ch.start += (uint32_t)newOffset << 1;
    }
    else
    {
        ch.length = 1;
    }
}

void Replayer::volumeSlide(Channel& ch)
{
    const uint8_t param = ch.cmd & 0xFF;
    if ((param & 0xF0) == 0)
    {
        ch.volume = (int8_t)(ch.volume - (param & 0x0F));
        if (ch.volume < 0)
            ch.volume = 0;
    }
    else
    {
        ch.volume = (int8_t)(ch.volume + (param >> 4));
        if (ch.volume > 64)
            ch.volume = 64;
    }
}

void Replayer::updateFunk(Channel& ch)
{
    const int8_t funkSpeed = (int8_t)(ch.glissFunk >> 4);
    if (funkSpeed == 0)
        return;

    ch.funkOffset = (uint8_t)(ch.funkOffset + kFunkTable[funkSpeed]);
    if (ch.funkOffset >= 128)
    {
        ch.funkOffset = 0;

        if (ch.loopStart != nullptr && ch.waveStart != nullptr)
        {
            if (++ch.waveStart >= ch.loopStart + (ch.repLen << 1))
                ch.waveStart = ch.loopStart;

            *ch.waveStart = (int8_t)(-1 - *ch.waveStart);
        }
    }
}

void Replayer::jumpLoop(Channel& ch)
{
    if (tick_ != 0)
        return;

    if ((ch.cmd & 0xF) == 0)
    {
        ch.pattPos = (int8_t)row_;
    }
    else
    {
        if (ch.loopCount == 0)
            ch.loopCount = (int8_t)(ch.cmd & 0xF);
        else if (--ch.loopCount == 0)
            return;

        pBreakPosition_ = ch.pattPos;
        pBreakFlag_ = true;
    }
}

} // namespace protracker
