/*
 * Phellipe - an 8-voice polyphonic drone synth. A unison oscillator stack
 * (CORE) feeds a shared resonant filter (RESONATOR), texture layer (GRAIN),
 * chorus, tempo-syncable delay, and reverb (SPACE). Eight modulation sources
 * - DRIFT, AMP ENV, FILTER ENV, LFO, MOD WHEEL, PITCH BEND, VELOCITY,
 * AFTERTOUCH - can each be patched (semi-modular style, via the UI's patch
 * cables) into any of nine destinations: CORE pitch, CORE wave blend,
 * RESONATOR cutoff, DELAY time, CHORUS rate, the LFO's own rate, GRAIN age,
 * and OSC B/C's FREE fine-tune. No arpeggiator, mono mode, or portamento.
 */

#include "DistrhoPlugin.hpp"
#include "Params.hpp"
#include "dsp/Voice.hpp"
#include "dsp/ADSR.hpp"
#include "dsp/Filter.hpp"
#include "dsp/CutoffMath.hpp"
#include "dsp/DriftGenerator.hpp"
#include "dsp/Lfo.hpp"
#include "dsp/Texture.hpp"
#include "dsp/Chorus.hpp"
#include "dsp/Delay.hpp"
#include "dsp/Reverb.hpp"

#include <array>
#include <algorithm>
#include <cmath>

START_NAMESPACE_DISTRHO

using namespace phellipe;

static constexpr const uint32_t kNumVoices = phellipe::DriftGenerator::kMaxVoices; // 8

// the patch matrix: 8 sources x 15 destinations. Order here must match the
// source-major kParamPatch* block in Params.hpp exactly.
enum Src { SrcDrift = 0, SrcAmpEnv, SrcFilterEnv, SrcLfo, SrcModWheel, SrcPitchBend, SrcVelocity, SrcAftertouch, kNumSources };
enum Dest { DestPitch = 0, DestCutoff, DestWave, DestDelayTime, DestChorusRate, DestLfoRate, DestAge,
            DestOscBFree, DestOscCFree, DestResonance, DestDrive, DestChorusDepth, DestSpaceSize, DestDelayMix,
            DestNoise, kNumDests };

// modulation range constants: how far a destination moves at a clamped,
// fully-summed modulation value of +-1 (multiple patched sources can stack
// up to +-kModSumClamp before their own destination range is applied).
static constexpr const float kModSumClamp = 2.0f;
static constexpr const float kPitchModSemitones = 12.0f;
static constexpr const float kWaveModRange = 0.6f;
static constexpr const float kCutoffModRange = 0.4f;
static constexpr const float kDelayTimeModMs = 300.0f;   // Delay.hpp self-clamps to [1,1500]ms regardless
static constexpr const float kChorusRateModOctaves = 2.0f; // Chorus.hpp has no internal rate ceiling - the
static constexpr const float kLfoRateModOctaves = 2.0f;    // 15Hz/20Hz clamps below are load-bearing, not decorative
static constexpr const float kAgeModRange = 0.6f;          // added to base AGE (0..1), then clamped 0..1
static constexpr const float kFreeModCents = 50.0f;        // matches OSC B/C's own FREE knob range (+-50ct)
static constexpr const float kResonanceModRange = 0.5f;    // added to base RESONANCE (0..1), then clamped 0..1
static constexpr const float kDriveModRange = 0.6f;        // added to base DRIVE (0..1), then clamped 0..1
static constexpr const float kChorusDepthModRange = 0.5f;  // added to base CHORUS DEPTH (0..1), then clamped 0..1
static constexpr const float kSpaceSizeModRange = 0.5f;    // added to base SPACE SIZE (0..1), then clamped 0..1
static constexpr const float kDelayMixModRange = 0.5f;     // added to base DELAY MIX (0..1), then clamped 0..1
static constexpr const float kNoiseModRange = 0.6f;        // added to base NOISE (0..1), then clamped 0..1

// several simultaneously-held chord voices (each already unison-normalized
// internally) plus a resonant filter and reverb wet signal can otherwise sum
// well past 0dBFS and slam the final safety tanh into audible hard clipping;
// pull the mix down before any of that, same reasoning as Sideous's own
// kVoiceHeadroom.
static constexpr const float kVoiceHeadroom = 0.45f;

// retro pixel scope: 32 samples of the final mixed output, captured one at
// a time at a fixed decimation so a full buffer is one "sweep" - see
// sampleRateChanged() for how kScopeSweepSeconds becomes a per-sample-rate
// decimation interval, and Params.hpp's kParamScopeFirst comment for why
// the UI needs no separate read-offset.
static constexpr const uint32_t kScopeSize = 32;
static constexpr const float kScopeSweepSeconds = 0.12f;

// DELAY's tempo sync: 0 = Free (use the Time knob), 1..7 = the note division
// below. A whole note is 240000/bpm ms (4 quarter notes, each 60000/bpm ms).
static float delaySyncedMs(int syncIndex, double bpm) noexcept
{
    static constexpr int kDivisor[7] = { 1, 2, 4, 8, 16, 32, 64 };
    if (syncIndex < 1 || syncIndex > 7 || bpm <= 0.0)
        return 0.0f;
    const double wholeNoteMs = 240000.0 / bpm;
    return (float)(wholeNoteMs / (double)kDivisor[syncIndex - 1]);
}

class PhellipePlugin : public Plugin
{
public:
    PhellipePlugin()
        : Plugin(kParamCount, 0, 0)
    {
        sampleRateChanged(getSampleRate());
    }

protected:
    // ---------------------------------------------------------------------
    // Information

    const char* getLabel() const override { return "Phellipe"; }
    const char* getDescription() const override
    {
        return "An 8-voice polyphonic drone synth with a semi-modular patch matrix: "
               "DRIFT, AMP ENV, FILTER ENV, LFO, MOD WHEEL, PITCH BEND, VELOCITY, and "
               "AFTERTOUCH can each be patched into pitch, wave blend, filter cutoff, "
               "resonance, drive, a tempo-syncable delay time, chorus rate or depth, "
               "the LFO's own rate, grain age or noise level, reverb size, delay mix, "
               "or either sub-oscillator's fine-tune. OSC A/B/C each have their own "
               "sine-to-saw WAVE blend.";
    }
    const char* getMaker() const override { return "Phellipe"; }
    const char* getHomePage() const override { return DISTRHO_PLUGIN_URI; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t getVersion() const override { return d_version(0, 3, 0); }

    // ---------------------------------------------------------------------
    // Init

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        const ParamInfo& info = getParamInfo(index);

        // patch-cable voltage meters and the retro scope buffer are both
        // read-only (see Params.hpp's kParamMeterFirst/kParamScopeFirst
        // comments) - not automatable, just periodically polled by the
        // host and pushed to the UI as output-parameter updates.
        const bool isMeter = index >= kParamMeterFirst && index < kParamMeterFirst + kNumSources;
        const bool isScope = index >= kParamScopeFirst && index < kParamScopeFirst + kScopeSize;

        parameter.hints = (isMeter || isScope) ? kParameterIsOutput : kParameterIsAutomatable;
        parameter.name = info.name;
        parameter.symbol = info.symbol;
        parameter.unit = info.unit;
        parameter.ranges.min = info.min;
        parameter.ranges.max = info.max;
        parameter.ranges.def = info.def;

        if (info.shape == ParamShape::Logarithmic)
            parameter.hints |= kParameterIsLogarithmic;
        if (info.shape == ParamShape::Boolean)
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
        if (index == kParamVoices || index == kParamOscBOctave || index == kParamOscBSemi
            || index == kParamOscCOctave || index == kParamOscCSemi || index == kParamDelaySync)
            parameter.hints |= kParameterIsInteger;
    }

    // ---------------------------------------------------------------------
    // Internal data

    float getParameterValue(uint32_t index) const override
    {
        if (index >= kParamPatchFirst && index < kParamPatchFirst + kNumSources * kNumDests)
        {
            const uint32_t offset = index - kParamPatchFirst;
            return fPatch[offset / kNumDests][offset % kNumDests] ? 1.0f : 0.0f;
        }
        if (index >= kParamMeterFirst && index < kParamMeterFirst + kNumSources)
            return fMeter[index - kParamMeterFirst];
        if (index >= kParamScopeFirst && index < kParamScopeFirst + kScopeSize)
            return fScopeBuffer[index - kParamScopeFirst];

        switch (index)
        {
        case kParamVoices:      return (float)fVoiceParams.voices;
        case kParamDetune:      return fVoiceParams.detuneCents;
        case kParamSpread:      return fVoiceParams.spread;
        case kParamWave:        return fVoiceParams.wave;
        case kParamOscALevel:   return fVoiceParams.oscALevel;
        case kParamOscBFree:    return fVoiceParams.oscBFreeCents;
        case kParamOscBOctave:  return fVoiceParams.oscBOctave;
        case kParamOscBSemi:    return fVoiceParams.oscBSemi;
        case kParamOscBWave:    return fVoiceParams.oscBWave;
        case kParamOscBLevel:   return fVoiceParams.oscBLevel;
        case kParamOscCFree:    return fVoiceParams.oscCFreeCents;
        case kParamOscCOctave:  return fVoiceParams.oscCOctave;
        case kParamOscCSemi:    return fVoiceParams.oscCSemi;
        case kParamOscCWave:    return fVoiceParams.oscCWave;
        case kParamOscCLevel:   return fVoiceParams.oscCLevel;
        case kParamFmAtoB:      return fVoiceParams.fmAtoB;
        case kParamFmAtoC:      return fVoiceParams.fmAtoC;
        case kParamFmBtoA:      return fVoiceParams.fmBtoA;
        case kParamFmBtoC:      return fVoiceParams.fmBtoC;
        case kParamFmCtoA:      return fVoiceParams.fmCtoA;
        case kParamFmCtoB:      return fVoiceParams.fmCtoB;
        case kParamCutoff:      return fCutoffHz;
        case kParamResonance:   return fResonance;
        case kParamDrive:       return fDrive;
        case kParamDriftRate:   return fDriftRate;
        case kParamDriftDepth:  return fDriftAmount;
        case kParamDriftChaos:  return fDriftChaos;
        case kParamNoise:       return fNoise;
        case kParamGrain:       return fGrain;
        case kParamAge:         return fAge;
        case kParamSpaceSize:   return fSpaceSize;
        case kParamSpaceDecay:  return fSpaceDecay;
        case kParamSpaceMix:    return fSpaceMix;
        case kParamVolume:      return fVolume;
        case kParamWidth:       return fWidth;
        case kParamAmpAttack:   return fVoiceParams.ampAttack;
        case kParamAmpDecay:    return fVoiceParams.ampDecay;
        case kParamAmpSustain:  return fVoiceParams.ampSustain;
        case kParamAmpRelease:  return fVoiceParams.ampRelease;
        case kParamFilterAttack:  return fFilterAttack;
        case kParamFilterDecay:   return fFilterDecay;
        case kParamFilterSustain: return fFilterSustain;
        case kParamFilterRelease: return fFilterRelease;
        case kParamDelayTime:     return fDelayTime;
        case kParamDelayFeedback: return fDelayFeedback;
        case kParamDelayMix:      return fDelayMix;
        case kParamDelaySync:     return (float)fDelaySync;
        case kParamChorusRate:    return fChorusRate;
        case kParamChorusDepth:   return fChorusDepth;
        case kParamChorusMix:     return fChorusMix;
        case kParamLfoRate:         return fLfoBaseRate;
        case kParamAmpEnvAmount:    return fAmpEnvAmount;
        case kParamFilterEnvAmount: return fFilterEnvAmount;
        case kParamLfoAmount:       return fLfoAmount;
        case kParamModWheelAmount:  return fModWheelAmount;
        case kParamPitchBendAmount: return fPitchBendAmount;
        case kParamVelocityAmount:  return fVelocityAmount;
        case kParamAftertouchAmount: return fAftertouchAmount;
        default:                return 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= kParamPatchFirst && index < kParamPatchFirst + kNumSources * kNumDests)
        {
            const uint32_t offset = index - kParamPatchFirst;
            fPatch[offset / kNumDests][offset % kNumDests] = value > 0.5f;
            return;
        }

        switch (index)
        {
        case kParamVoices:     fVoiceParams.voices = (uint32_t)(value + 0.5f); break;
        case kParamDetune:     fVoiceParams.detuneCents = value; break;
        case kParamSpread:     fVoiceParams.spread = value; break;
        case kParamWave:       fVoiceParams.wave = value; break;
        case kParamOscALevel:  fVoiceParams.oscALevel = value; break;
        case kParamOscBFree:   fVoiceParams.oscBFreeCents = value; break;
        case kParamOscBOctave: fVoiceParams.oscBOctave = value; break;
        case kParamOscBSemi:   fVoiceParams.oscBSemi = value; break;
        case kParamOscBWave:   fVoiceParams.oscBWave = value; break;
        case kParamOscBLevel:  fVoiceParams.oscBLevel = value; break;
        case kParamOscCFree:   fVoiceParams.oscCFreeCents = value; break;
        case kParamOscCOctave: fVoiceParams.oscCOctave = value; break;
        case kParamOscCSemi:   fVoiceParams.oscCSemi = value; break;
        case kParamOscCWave:   fVoiceParams.oscCWave = value; break;
        case kParamOscCLevel:  fVoiceParams.oscCLevel = value; break;
        case kParamFmAtoB:     fVoiceParams.fmAtoB = value; break;
        case kParamFmAtoC:     fVoiceParams.fmAtoC = value; break;
        case kParamFmBtoA:     fVoiceParams.fmBtoA = value; break;
        case kParamFmBtoC:     fVoiceParams.fmBtoC = value; break;
        case kParamFmCtoA:     fVoiceParams.fmCtoA = value; break;
        case kParamFmCtoB:     fVoiceParams.fmCtoB = value; break;
        case kParamCutoff:     fCutoffHz = value; break;
        case kParamResonance:  fResonance = value; break;
        case kParamDrive:      fDrive = value; break;
        case kParamDriftRate:  fDriftRate = value; fDrift.setRate(value); break;
        case kParamDriftDepth: fDriftAmount = value; break;
        case kParamDriftChaos: fDriftChaos = value; fDrift.setChaos(value); break;
        case kParamNoise:      fNoise = value; break;
        case kParamGrain:      fGrain = value; break;
        case kParamAge:        fAge = value; break;
        case kParamSpaceSize:  fSpaceSize = value; break;
        case kParamSpaceDecay: fSpaceDecay = value; break;
        case kParamSpaceMix:   fSpaceMix = value; break;
        case kParamVolume:     fVolume = value; break;
        case kParamWidth:      fWidth = value; break;
        case kParamAmpAttack:  fVoiceParams.ampAttack = value; break;
        case kParamAmpDecay:   fVoiceParams.ampDecay = value; break;
        case kParamAmpSustain: fVoiceParams.ampSustain = value; break;
        case kParamAmpRelease: fVoiceParams.ampRelease = value; break;
        case kParamFilterAttack:  fFilterAttack = value; break;
        case kParamFilterDecay:   fFilterDecay = value; break;
        case kParamFilterSustain: fFilterSustain = value; break;
        case kParamFilterRelease: fFilterRelease = value; break;
        case kParamDelayTime:     fDelayTime = value; break;
        case kParamDelayFeedback: fDelayFeedback = value; break;
        case kParamDelayMix:      fDelayMix = value; break;
        case kParamDelaySync:     fDelaySync = (int)(value + 0.5f); break;
        case kParamChorusRate:    fChorusRate = value; break;
        case kParamChorusDepth:   fChorusDepth = value; break;
        case kParamChorusMix:     fChorusMix = value; break;
        case kParamLfoRate:         fLfoBaseRate = value; break;
        case kParamAmpEnvAmount:    fAmpEnvAmount = value; break;
        case kParamFilterEnvAmount: fFilterEnvAmount = value; break;
        case kParamLfoAmount:       fLfoAmount = value; break;
        case kParamModWheelAmount:  fModWheelAmount = value; break;
        case kParamPitchBendAmount: fPitchBendAmount = value; break;
        case kParamVelocityAmount:  fVelocityAmount = value; break;
        case kParamAftertouchAmount: fAftertouchAmount = value; break;
        default: return;
        }

        for (Voice& voice : fVoices)
            voice.applyParams(fVoiceParams);
    }

    // ---------------------------------------------------------------------
    // Audio/MIDI Processing

    void activate() override
    {
        sampleRateChanged(getSampleRate());
    }

    void sampleRateChanged(double newSampleRate) override
    {
        fSampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

        for (Voice& voice : fVoices)
        {
            voice.setSampleRate(fSampleRate);
            voice.applyParams(fVoiceParams);
        }

        fDrift.setSampleRate(fSampleRate);
        fDrift.setRate(fDriftRate);
        fDrift.setChaos(fDriftChaos);

        fLfo.setSampleRate(fSampleRate);

        fFilterL.setSampleRate(fSampleRate);
        fFilterR.setSampleRate(fSampleRate);
        fFilterL.setType(FilterType::Lowpass);
        fFilterR.setType(FilterType::Lowpass);

        fNoiseBedL.setSampleRate(fSampleRate);
        fNoiseBedR.setSampleRate(fSampleRate);
        fStutter.setSampleRate(fSampleRate);
        fAgeL.setSampleRate(fSampleRate);
        fAgeR.setSampleRate(fSampleRate);

        fReverb.setSampleRate(fSampleRate);

        fChorus.setSampleRate(fSampleRate);
        fDelay.setSampleRate(fSampleRate);

        fFilterEnv.setSampleRate(fSampleRate);
        fAmpEnvRef.setSampleRate(fSampleRate);

        fScopeDecimateInterval = std::max(1u, (uint32_t)(fSampleRate * kScopeSweepSeconds / (double)kScopeSize));
    }

    void run(const float**, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        float* outL = outputs[0];
        float* outR = outputs[1];

        fFilterEnv.setAttack(fFilterAttack);
        fFilterEnv.setDecay(fFilterDecay);
        fFilterEnv.setSustain(fFilterSustain);
        fFilterEnv.setRelease(fFilterRelease);

        // AMP ENV patch-source: a second, global ADSR mirroring the same
        // knobs that drive each voice's own per-note fSwellEnv - a single
        // shared CV, not tied to any one voice (see the fAmpEnvRef comment
        // below).
        fAmpEnvRef.setAttack(fVoiceParams.ampAttack);
        fAmpEnvRef.setDecay(fVoiceParams.ampDecay);
        fAmpEnvRef.setSustain(fVoiceParams.ampSustain);
        fAmpEnvRef.setRelease(fVoiceParams.ampRelease);

        // DELAY tempo sync: host BPM, same "fall back to 120" convention as
        // every other DPF synth that reads TimePosition.
        const TimePosition& timePos = getTimePosition();
        const double bpm = timePos.bbt.valid && timePos.bbt.beatsPerMinute > 0.0
                          ? timePos.bbt.beatsPerMinute : 120.0;
        const float syncedMs = delaySyncedMs(fDelaySync, bpm);
        const float delayBaseMs = fDelaySync > 0 ? syncedMs : fDelayTime;

        uint32_t nextEvent = 0;

        for (uint32_t frame = 0; frame < frames; ++frame)
        {
            while (nextEvent < midiEventCount && midiEvents[nextEvent].frame == frame)
            {
                handleMidiEvent(midiEvents[nextEvent]);
                ++nextEvent;
            }

            // --- patch matrix: sources, each attenuated by its own amount knob ---
            const float filterEnvLevel = fFilterEnv.process();  // 0..1
            const float ampEnvLevel = fAmpEnvRef.process();      // 0..1
            const float driftGlobal = fDrift.process();           // -1..1

            const float filterEnvAtt = filterEnvLevel * fFilterEnvAmount;
            const float ampEnvAtt = ampEnvLevel * fAmpEnvAmount;
            const float driftAtt = driftGlobal * fDriftAmount;
            const float modWheelAtt = fModWheelValue * fModWheelAmount;     // 0..1 raw
            const float pitchBendAtt = fPitchBendValue * fPitchBendAmount; // -1..1 raw
            const float velocityAtt = fVelocityValue * fVelocityAmount;     // 0..1 raw, last note-on
            const float aftertouchAtt = fAftertouchValue * fAftertouchAmount; // 0..1 raw

            // LFO rate can itself be patched (e.g. an envelope speeding it
            // up); a self-patch reads the PREVIOUS sample's LFO output - a
            // harmless one-sample-delayed feedback, since Lfo::process()'s
            // amplitude is structurally independent of its rate (bounded
            // regardless of how the rate wobbles).
            float lfoRateSum = 0.0f;
            if (fPatch[SrcDrift][DestLfoRate])      lfoRateSum += driftAtt;
            if (fPatch[SrcAmpEnv][DestLfoRate])     lfoRateSum += ampEnvAtt;
            if (fPatch[SrcFilterEnv][DestLfoRate])  lfoRateSum += filterEnvAtt;
            if (fPatch[SrcModWheel][DestLfoRate])   lfoRateSum += modWheelAtt;
            if (fPatch[SrcPitchBend][DestLfoRate])  lfoRateSum += pitchBendAtt;
            if (fPatch[SrcVelocity][DestLfoRate])   lfoRateSum += velocityAtt;
            if (fPatch[SrcAftertouch][DestLfoRate]) lfoRateSum += aftertouchAtt;
            if (fPatch[SrcLfo][DestLfoRate])        lfoRateSum += fLfoAttPrev;
            lfoRateSum = std::clamp(lfoRateSum, -kModSumClamp, kModSumClamp);

            const float lfoRateHz = std::clamp(fLfoBaseRate * std::exp2(lfoRateSum * kLfoRateModOctaves), 0.01f, 20.0f);
            fLfo.setRate(lfoRateHz);
            const float lfoAtt = fLfo.process() * fLfoAmount;
            fLfoAttPrev = lfoAtt;

            const float attenuated[kNumSources] = { driftAtt, ampEnvAtt, filterEnvAtt, lfoAtt, modWheelAtt, pitchBendAtt, velocityAtt, aftertouchAtt };

            // patch-cable voltage meters: last sample of the block is close
            // enough for a UI glow that's only ever polled a few dozen times
            // a second (see kParamMeterFirst's comment) - no need to average.
            for (uint32_t s = 0; s < (uint32_t)kNumSources; ++s)
                fMeter[s] = attenuated[s];

            float sumFor[kNumDests] = {};
            for (uint32_t s = 0; s < (uint32_t)kNumSources; ++s)
                for (uint32_t d = 0; d < (uint32_t)kNumDests; ++d)
                    if (fPatch[s][d])
                        sumFor[d] += attenuated[s];
            for (uint32_t d = 0; d < (uint32_t)kNumDests; ++d)
                sumFor[d] = std::clamp(sumFor[d], -kModSumClamp, kModSumClamp);

            const float pitchModSemitones = sumFor[DestPitch] * kPitchModSemitones;
            // each oscillator group has its own base WAVE value now (see
            // Voice::process()) - only the raw offset is computed here, added
            // to (and clamped against) each group's own base independently.
            const float waveModOffset = sumFor[DestWave] * kWaveModRange;
            const float oscBFreeModCents = sumFor[DestOscBFree] * kFreeModCents;
            const float oscCFreeModCents = sumFor[DestOscCFree] * kFreeModCents;

            float mixL = 0.0f, mixR = 0.0f;
            bool anyVoiceActive = false;
            uint32_t activeVoiceCount = 0;
            for (uint32_t v = 0; v < kNumVoices; ++v)
            {
                if (!fVoices[v].isActive())
                    continue;
                anyVoiceActive = true;
                ++activeVoiceCount;
                fVoices[v].process(pitchModSemitones, waveModOffset, oscBFreeModCents, oscCFreeModCents, mixL, mixR);
            }
            // headroom scales with how many voices are actually stacked up
            // this sample - equal-power (1/sqrt(n)) down to kVoiceHeadroom's
            // floor, which is where it plateaus at the original flat
            // constant's own proven-safe worst case (a full dense chord
            // plus reverb tail). A single held note was previously cut by
            // that same worst-case factor even though nothing was piling
            // up, making everyday single-note/small-chord playing much
            // quieter than it needed to be.
            const float headroom = std::clamp(1.0f / std::sqrt(std::max(1.0f, (float)activeVoiceCount)), kVoiceHeadroom, 1.0f);
            mixL *= headroom;
            mixR *= headroom;

            // GRAIN's NOISE bed is a texture layered onto the tone, not an
            // independent drone of its own - gate it by whether anything is
            // actually sounding (smoothed, so it fades in/out with the notes
            // rather than clicking) instead of hissing continuously even
            // with no MIDI input at all.
            const float noiseGateTarget = anyVoiceActive ? 1.0f : 0.0f;
            const float noiseGateCoeff = 1.0f - std::exp(-1.0f / (0.01f * (float)fSampleRate));
            fNoiseGate += (noiseGateTarget - fNoiseGate) * noiseGateCoeff;

            // RESONATOR: log-space cutoff, resonance, and drive, all patch-
            // matrix-modulated. The filter is a single shared stage (post-
            // mix, not per-voice).
            float cutoffNorm = cutoffToNormalized(fCutoffHz) + sumFor[DestCutoff] * kCutoffModRange;
            const float cutoffHz = normalizedToCutoff(cutoffNorm);
            const float resonanceMod01 = std::clamp(fResonance + sumFor[DestResonance] * kResonanceModRange, 0.0f, 1.0f);
            const float driveMod01 = std::clamp(fDrive + sumFor[DestDrive] * kDriveModRange, 0.0f, 1.0f);
            fFilterL.setResonance(resonanceMod01);
            fFilterR.setResonance(resonanceMod01);
            fFilterL.setDrive(driveMod01);
            fFilterR.setDrive(driveMod01);
            float filteredL = fFilterL.process(mixL, cutoffHz);
            float filteredR = fFilterR.process(mixR, cutoffHz);

            // GRAIN: noise bed (level patch-matrix-modulated), stutter gate,
            // tape-age character
            const float noiseMod01 = std::clamp(fNoise + sumFor[DestNoise] * kNoiseModRange, 0.0f, 1.0f);
            float noisedL = filteredL + fNoiseBedL.process(cutoffHz) * noiseMod01 * fNoiseGate;
            float noisedR = filteredR + fNoiseBedR.process(cutoffHz) * noiseMod01 * fNoiseGate;
            const float gate = fStutter.process(fGrain);
            float stutteredL = noisedL * gate;
            float stutteredR = noisedR * gate;
            const float ageMod01 = std::clamp(fAge + sumFor[DestAge] * kAgeModRange, 0.0f, 1.0f);
            float agedL = fAgeL.process(stutteredL, ageMod01);
            float agedR = fAgeR.process(stutteredR, ageMod01);

            // CHORUS: LFO-modulated stereo-wide ensemble thickening, rate and
            // depth both patch-matrix-modulated (15Hz clamp is load-bearing -
            // Chorus.hpp has no internal ceiling of its own)
            const float chorusRateHz = std::clamp(fChorusRate * std::exp2(sumFor[DestChorusRate] * kChorusRateModOctaves), 0.01f, 15.0f);
            const float chorusDepthMod01 = std::clamp(fChorusDepth + sumFor[DestChorusDepth] * kChorusDepthModRange, 0.0f, 1.0f);
            float chorusL, chorusR;
            fChorus.process(agedL, agedR, chorusRateHz, chorusDepthMod01, fChorusMix, chorusL, chorusR);

            // DELAY: cross-feedback ping-pong echoes, ahead of the reverb so
            // the repeats themselves get smoothed into the room too; base
            // time comes from tempo sync (delay_sync != Free) or the Time
            // knob, and the patch matrix can still nudge it (and the dry/wet
            // mix) further either way (Delay::process() self-clamps to
            // [1,1500]ms regardless)
            const float delayTimeMs = delayBaseMs + sumFor[DestDelayTime] * kDelayTimeModMs;
            const float delayMixMod01 = std::clamp(fDelayMix + sumFor[DestDelayMix] * kDelayMixModRange, 0.0f, 1.0f);
            float delayWetL, delayWetR;
            fDelay.process(chorusL, chorusR, delayTimeMs, fDelayFeedback, delayWetL, delayWetR);
            float delayedL = chorusL + (delayWetL - chorusL) * delayMixMod01;
            float delayedR = chorusR + (delayWetR - chorusR) * delayMixMod01;

            // SPACE: reverb, size patch-matrix-modulated, dry/wet
            const float spaceSizeMod01 = std::clamp(fSpaceSize + sumFor[DestSpaceSize] * kSpaceSizeModRange, 0.0f, 1.0f);
            float wetL, wetR;
            fReverb.process(delayedL, delayedR, spaceSizeMod01, fSpaceDecay, wetL, wetR);
            float spacedL = delayedL + (wetL - delayedL) * fSpaceMix;
            float spacedR = delayedR + (wetR - delayedR) * fSpaceMix;

            // OUTPUT: mid/side width, master volume, safety saturation
            const float mid = (spacedL + spacedR) * 0.5f;
            const float side = (spacedL - spacedR) * 0.5f * fWidth;
            const float sampleL = std::tanh((mid + side) * fVolume);
            const float sampleR = std::tanh((mid - side) * fVolume);
            outL[frame] = sampleL;
            outR[frame] = sampleR;

            // retro pixel scope: one decimated mono sample at a time: see
            // kScopeSweepSeconds's comment above for why the write position
            // just wraps rather than needing a separate read-offset.
            if (++fScopeSampleCounter >= fScopeDecimateInterval)
            {
                fScopeSampleCounter = 0;
                fScopeBuffer[fScopePos] = (sampleL + sampleR) * 0.5f;
                fScopePos = (fScopePos + 1) % kScopeSize;
            }
        }

        while (nextEvent < midiEventCount)
            handleMidiEvent(midiEvents[nextEvent++]);
    }

private:
    void handleMidiEvent(const MidiEvent& event) noexcept
    {
        if (event.size < 2 || event.size > 3)
            return;

        const uint8_t status = event.data[0] & 0xF0;
        const uint8_t note = event.data[1];
        const uint8_t velocity = event.size > 2 ? event.data[2] : 0;

        if (status == 0x90 && velocity > 0)
            triggerVoiceNoteOn(note, velocity);
        else if (status == 0x80 || (status == 0x90 && velocity == 0))
            triggerVoiceNoteOff(note);
        else if (status == 0xB0 && event.size >= 3 && note == 1) // CC1 = mod wheel
            fModWheelValue = (float)velocity / 127.0f;
        else if (status == 0xE0 && event.size >= 3) // pitch bend, 14-bit, center = 8192
        {
            const int raw = (int)event.data[1] | ((int)event.data[2] << 7);
            const float normalized = ((float)raw - 8192.0f) / 8192.0f;
            fPitchBendValue = std::clamp(normalized, -1.0f, 1.0f);
        }
        else if (status == 0xD0) // channel aftertouch (2-byte message: status, pressure)
            fAftertouchValue = (float)note / 127.0f;
    }

    // first pass: a fully free voice; second pass: a releasing voice (its
    // tail will be cut short, acceptable for a steal); otherwise round-robin
    // over the whole pool. Mirrors Sideous's triggerVoiceNoteOn() exactly.
    //
    // FILTER ENV and AMP ENV (as a patch source) are triggered here too:
    // each is one shared envelope, so it retriggers on the first note of a
    // phrase (0 -> 1 held notes) and releases only once every held note is
    // gone (1 -> 0), the same "legato across a chord" convention a mono
    // synth's envelope uses - never per individual overlapping note.
    void triggerVoiceNoteOn(int note, int velocity) noexcept
    {
        // VELOCITY patch source: the most recently struck note's velocity,
        // held (not reset on note-off) until the next note-on - the natural
        // reading of "velocity" as a single global CV for a polyphonic synth.
        fVelocityValue = (float)velocity / 127.0f;

        if (fHeldNoteCount++ == 0)
        {
            fFilterEnv.noteOn();
            fAmpEnvRef.noteOn();
        }

        Voice* target = nullptr;

        for (Voice& voice : fVoices)
        {
            if (!voice.isActive())
            {
                target = &voice;
                break;
            }
        }

        if (target == nullptr)
        {
            for (Voice& voice : fVoices)
            {
                if (voice.isReleasing())
                {
                    target = &voice;
                    break;
                }
            }
        }

        if (target == nullptr)
            target = &fVoices[fStealCursor++ % kNumVoices];

        target->applyParams(fVoiceParams);
        target->noteOn(note, (float)velocity / 127.0f);
    }

    void triggerVoiceNoteOff(int note) noexcept
    {
        if (fHeldNoteCount > 0 && --fHeldNoteCount == 0)
        {
            fFilterEnv.noteOff();
            fAmpEnvRef.noteOff();
        }

        for (Voice& voice : fVoices)
        {
            if (voice.isActive() && voice.getNote() == note && !voice.isReleasing())
                voice.noteOff();
        }
    }

    std::array<Voice, kNumVoices> fVoices;
    VoiceParams fVoiceParams;
    uint32_t fStealCursor = 0;
    double fSampleRate = 44100.0;

    float fCutoffHz = 1800.0f;
    float fResonance = 0.2f;
    float fDrive = 0.0f;

    float fDriftRate = 0.15f;
    float fDriftChaos = 0.2f;
    float fDriftAmount = 0.3f; // DRIFT's patch-matrix output attenuator (was VoiceParams::driftDepth)
    DriftGenerator fDrift;

    float fLfoBaseRate = 1.0f;
    float fLfoAmount = 0.5f;
    float fLfoAttPrev = 0.0f; // last sample's attenuated LFO output, for patch_lfo_lforate self-feedback
    Lfo fLfo;

    float fAmpEnvAmount = 0.5f;
    float fFilterEnvAmount = 0.7f;

    float fModWheelValue = 0.0f;   // 0..1, from MIDI CC1
    float fModWheelAmount = 0.5f;
    float fPitchBendValue = 0.0f;  // -1..1, from MIDI pitch bend (0xE0)
    float fPitchBendAmount = 0.1667f;
    float fVelocityValue = 0.0f;   // 0..1, last note-on velocity (held, see triggerVoiceNoteOn)
    float fVelocityAmount = 0.5f;
    float fAftertouchValue = 0.0f; // 0..1, from MIDI channel aftertouch (0xD0)
    float fAftertouchAmount = 0.5f;

    // patch-cable voltage meters: fMeter[source], see kParamMeterFirst's
    // comment in Params.hpp - written every sample in run(), read back via
    // getParameterValue() as output parameters.
    float fMeter[kNumSources] = {};

    // retro pixel scope: fScopeBuffer[i], see kScopeSize's comment above -
    // fScopeDecimateInterval (recomputed in sampleRateChanged()) samples
    // pass between each capture; fScopeSampleCounter/fScopePos track where
    // in that cycle/buffer we are.
    float fScopeBuffer[kScopeSize] = {};
    uint32_t fScopePos = 0;
    uint32_t fScopeSampleCounter = 0;
    uint32_t fScopeDecimateInterval = 128;

    // patch matrix: fPatch[source][dest], see the Src/Dest enums above.
    // Defaults reproduce the plugin's old hardwired behavior (drift/filter-
    // env -> cutoff) plus standard synth behavior for pitch bend (-> pitch),
    // so it "just works" the way a normal pitch wheel would, out of the box.
    bool fPatch[kNumSources][kNumDests] = {
        /* Drift      */ { false, true,  false, false, false, false, false, false, false, false, false, false, false, false, false },
        /* AmpEnv     */ { false, false, false, false, false, false, false, false, false, false, false, false, false, false, false },
        /* FilterEnv  */ { false, true,  false, false, false, false, false, false, false, false, false, false, false, false, false },
        /* Lfo        */ { false, false, false, false, false, false, false, false, false, false, false, false, false, false, false },
        /* ModWheel   */ { false, false, false, false, false, false, false, false, false, false, false, false, false, false, false },
        /* PitchBend  */ { true,  false, false, false, false, false, false, false, false, false, false, false, false, false, false },
        /* Velocity   */ { false, false, false, false, false, false, false, false, false, false, false, false, false, false, false },
        /* Aftertouch */ { false, false, false, false, false, false, false, false, false, false, false, false, false, false, false },
    };

    float fNoise = 0.0f;
    float fGrain = 0.0f;
    float fAge = 0.0f;
    float fNoiseGate = 0.0f; // smoothed 0..1, follows anyVoiceActive - see run()

    float fSpaceSize = 0.5f;
    float fSpaceDecay = 0.5f;
    float fSpaceMix = 0.3f;

    float fVolume = 0.8f;
    float fWidth = 0.75f;

    Filter fFilterL, fFilterR;
    NoiseBed fNoiseBedL, fNoiseBedR;
    StutterGate fStutter;
    AgeProcessor fAgeL, fAgeR;
    Reverb fReverb;

    float fDelayTime = 350.0f;
    float fDelayFeedback = 0.35f;
    float fDelayMix = 0.25f;
    int fDelaySync = 0; // 0 = Free, 1..7 = note division, see delaySyncedMs()
    Delay fDelay;

    float fChorusRate = 0.3f;
    float fChorusDepth = 0.4f;
    float fChorusMix = 0.3f;
    Chorus fChorus;

    // shared FILTER ENV (see triggerVoiceNoteOn/Off()); fAmpEnvRef is the
    // analogous shared reference for the AMP ENV patch source - separate
    // from each Voice's own per-note fSwellEnv, which still drives loudness
    // and is unaffected by any of this.
    ADSR fFilterEnv;
    ADSR fAmpEnvRef;
    uint32_t fHeldNoteCount = 0;
    float fFilterAttack = 0.05f;
    float fFilterDecay = 0.5f;
    float fFilterSustain = 0.0f;
    float fFilterRelease = 0.3f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhellipePlugin)
};

// -----------------------------------------------------------------------------------------------------------

Plugin* createPlugin()
{
    return new PhellipePlugin();
}

// -----------------------------------------------------------------------------------------------------------

END_NAMESPACE_DISTRHO
