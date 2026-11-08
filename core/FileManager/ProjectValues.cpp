// Applies the values of a freshly loaded project to the audio engine and sanitises them.
//
// This used to live in the (removed) project editor screen. The audio engine and the Master and
// Sample recorder screens still rely on it every time a project is loaded.

#include "mtAudioEngine.h"
#include "mtMasterEnhancer.h"
#include "mtPadBoard.h"
#include "mtStructs.h"
#include "fileManager.h"
#include "sampleRecorder/sampleRecorder.h"
#include "util_math.h"
#include "Synth.h"

static const uint8_t trackMasterModeCount = 2;

uint8_t loadProjectValuesImpl(Synth* synth)
{

    if (mtProject.values.isSnapToZeroEnabled > 1)
    {
        mtProject.values.isSnapToZeroEnabled = 0;
    }

    if (mtProject.values.delayFilterFrequency < DELAY_FILTER_FREQ_MIN ||
        mtProject.values.delayFilterFrequency > DELAY_FILTER_FREQ_MAX)
    {
        mtProject.values.delayFilterFrequency = 20000;
    }

    if (mtProject.values.isHeadroomEnabled > 1)
    {
        mtProject.values.isHeadroomEnabled = 0;
    }

    if (mtProject.values.saturationType >= SaturationType::kSaturationTypeEnd)
    {
        mtProject.values.saturationType = SaturationType::kHardClip;
    }

    engine.setDelayFeedback(mtProject.values.delayFeedback);
    engine.setDelayTime(mtProject.values.delayTime);
    engine.setDelayParams(mtProject.values.delayParams);
    engine.setDelayFilterFrequency(mtProject.values.delayFilterFrequency);
    engine.setHeadroomEnable((bool)mtProject.values.isHeadroomEnabled);
    engine.setSnapToZeroEnable((bool)mtProject.values.isSnapToZeroEnabled);
    engine.setSaturationType((SaturationType)mtProject.values.saturationType);
    engine.setLimiterAttack(mtProject.values.limiterAttack);
    engine.setLimiterRelease(mtProject.values.limiterRelease);
    engine.setLimiterThreshold(mtProject.values.limiterThreshold);
    engine.setLimiterSidechain(mtProject.values.limiterSidechain);
    engine.setReverbSize(mtProject.values.reverb.size);
    engine.setReverbDamp(mtProject.values.reverb.damp);
    engine.setReverbPredelay(mtProject.values.reverb.predelay);
    engine.setReverbDiffusion(mtProject.values.reverb.diffusion);
    engine.setInterpolationEnable(mtProject.values.antialiasingEnable);

    masterEnhancer.fiveBandsEq.change(mtProject.values.eqBandAdjustment);
    masterEnhancer.changeBassBoost(mtProject.values.bassBoost);
    masterEnhancer.changeStereoBoost(mtProject.values.stereoBoost);
    engine.refreshDelayVolume();
    engine.refreshReverbVolume();
    engine.refreshTrackVolume();
    engine.refreshLineInStreamVolume();
    engine.refreshLineInStreamDelaySend();
    engine.refreshLineInStreamReverbSend();

    //----------------------------------------------------------------------------------------------------

    mtPadBoard.setPadNotes(mtProject.values.padBoardScale,
                           mtProject.values.padBoardNoteOffset,
                           mtProject.values.padBoardRootNote);
    mtPadBoard.configureInstrumentPlayer(8);

    //----------------------------------------------------------------------------------------------------
    sampleRecorder.recorderConfig.gainLineIn = mtProject.values.gainLineIn;
    sampleRecorder.recorderConfig.gainMicHigh = mtProject.values.gainMicHigh;
    sampleRecorder.recorderConfig.gainMicLow = mtProject.values.gainMicLow;
    sampleRecorder.recorderConfig.gainRadio = mtProject.values.gainRadio;
    sampleRecorder.recorderConfig.monitor = mtProject.values.monitor;
    sampleRecorder.recorderConfig.radioFreq = mtProject.values.radioFreq;
    sampleRecorder.recorderConfig.source =
      mtProject.values.source >= sourceCount ? 0 : mtProject.values.source;

    //----------------------------------------------------------------------------------------------------
    for (uint8_t i = 0; i < kMaxAllTracksInTrackers; i++)
    {
        mtProject.perfTracksPatterns[i] = 1;
    }

    for (uint8_t i = 0; i < kSampleTracks; i++)
    {
        // paterny na trakach w performance mode
        if (mtProject.values.perfTracksPatterns[i] < 1 ||
            mtProject.values.perfTracksPatterns[i] > 255)
        {
            mtProject.values.perfTracksPatterns[i] = 1;
        }

        // globalne mute trackow
        if (mtProject.values.trackMute[i] >= trackMasterModeCount)
            mtProject.values.trackMute[i] = 0;

        engine.muteTrack(i, mtProject.values.trackMute[i]);
    }

    for (uint8_t i = 0; i < kMaxMidiTracksInTrackers; i++)
    {
        if (mtProject.values.midiTrackMute[i] >= trackMasterModeCount)
        {
            mtProject.values.midiTrackMute[i] = 0;
        }
    }

    //----------------------------------------------------------------------------------------------------
    // song pattern
    if (mtProject.values.globalTempo > 1000)
        mtProject.values.globalTempo = DEFAULT_TEMPO;
    if (mtProject.values.patternLength > 255)
        mtProject.values.patternLength = 32;
    //----------------------------------------------------------------------------------------------------
    // midi instruments
    for (uint8_t channel = 0; channel < 16; channel++)
    {
        if (mtProject.values.midiInstrument[channel].channel > 16)
        {
            mtProject.values.midiInstrument[channel].channel = channel;
        }

        if (mtProject.values.midiInstrument[channel].velocity > 127)
        {
            mtProject.values.midiInstrument[channel].velocity = 127;
        }

        for (uint8_t cc = 0; cc < 6; cc++)
        {
            if (mtProject.values.midiInstrument[channel].ccOut[cc] > 130)
            {
                mtProject.values.midiInstrument[channel].ccOut[cc] = 0;
            }
        }
    }

    // last used
    if (mtProject.values.lastUsedInstrument >= kSampleInstrumentsCount + 16)
        mtProject.values.lastUsedInstrument = 0;

    if (mtProject.values.lastUsedNote >= 128)
        mtProject.values.lastUsedNote = 48;

    if (mtProject.values.lastUsedFx > FX_MAX)
        mtProject.values.lastUsedFx = 0;

    if (mtProject.values.recorderGainUsb > 100)
    {
        mtProject.values.recorderGainUsb = 0;
    }

#ifdef FEATURE_SYNTH
    for (int part = 0; part < kSynthInstrumentsCount; part++)
    {
        synth->validateSynthPartSettings(part, mtProject.values.synthPart[part]);

        mtProject.values.synthPart[part].reverbSend =
          clamp<float>(mtProject.values.synthPart[part].reverbSend, 0.0f, 100.0f);
        mtProject.values.synthPart[part].delaySend =
          clamp<float>(mtProject.values.synthPart[part].delaySend, 0.0f, 100.0f);

        engine.setSynthParam(audioEngine::SynthParam::kReverbSend,
                             part,
                             mtProject.values.synthPart[part].reverbSend,
                             audioEngine::SynthValuesSource::kInstrument);
        engine.setSynthParam(audioEngine::SynthParam::kDelaySend,
                             part,
                             mtProject.values.synthPart[part].delaySend,
                             audioEngine::SynthValuesSource::kInstrument);
        engine.setSynthParam(audioEngine::SynthParam::kPanning,
                             part,
                             mtProject.values.synthPart[part].panning,
                             audioEngine::SynthValuesSource::kInstrument);
        engine.setSynthParam(audioEngine::SynthParam::kVolume,
                             part,
                             mtProject.values.synthPart[part].gainDb,
                             audioEngine::SynthValuesSource::kInstrument);

        for (int macroIdx = 0; macroIdx < kSynthMacrosCount; macroIdx++)
        {
            engine.setSynthParam(
              audioEngine::SynthParam(audioEngine::SynthParam::kMacro1 + macroIdx),
              part,
              mtProject.values.synthPart[part].macroValue[macroIdx],
              audioEngine::SynthValuesSource::kInstrument);
        }

        engine.refreshSynthInstrumentParams(mtProject.values.synthPart[part], part);
    }
#endif

    return 1;
}

void cFileManager::loadProjectValues()
{
    loadProjectValuesImpl(synth);
}
