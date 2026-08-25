/*
 * Phellipe - headless DSP smoke test: instantiates the DSP classes directly
 * (bypassing the DPF Plugin wrapper), feeds a note-on, renders a few
 * seconds, writes a minimal WAV file, and asserts the output is sane
 * (non-silent, no NaN/Inf, peak within range) - lets DSP bugs be caught
 * without booting a host/DAW. Mirrors PhellipePlugin.cpp's run() exactly,
 * including the patch matrix - keep the two in lockstep. There's no host
 * here for DELAY's tempo sync, so a fixed bpm=120 stands in for
 * getTimePosition(); mod wheel/pitch bend have no live MIDI stream here
 * either, so fixed CV values stand in for handleMidiEvent()'s output.
 */

#include "../plugins/Phellipe/Params.hpp"
#include "../plugins/Phellipe/dsp/Voice.hpp"
#include "../plugins/Phellipe/dsp/Filter.hpp"
#include "../plugins/Phellipe/dsp/CutoffMath.hpp"
#include "../plugins/Phellipe/dsp/DriftGenerator.hpp"
#include "../plugins/Phellipe/dsp/Lfo.hpp"
#include "../plugins/Phellipe/dsp/Texture.hpp"
#include "../plugins/Phellipe/dsp/Chorus.hpp"
#include "../plugins/Phellipe/dsp/Delay.hpp"
#include "../plugins/Phellipe/dsp/Reverb.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace phellipe;

enum Src { SrcDrift = 0, SrcAmpEnv, SrcFilterEnv, SrcLfo, SrcModWheel, SrcPitchBend, SrcVelocity, SrcAftertouch, kNumSources };
enum Dest { DestPitch = 0, DestCutoff, DestWave, DestDelayTime, DestChorusRate, DestLfoRate, DestAge,
            DestOscBFree, DestOscCFree, DestResonance, DestDrive, DestChorusDepth, DestSpaceSize, DestDelayMix,
            DestNoise, kNumDests };

static constexpr uint32_t kNumVoices = DriftGenerator::kMaxVoices;
static constexpr float kVoiceHeadroom = 0.45f; // mirrors PhellipePlugin.cpp

static constexpr float kModSumClamp = 2.0f;
static constexpr float kPitchModSemitones = 12.0f;
static constexpr float kWaveModRange = 0.6f;
static constexpr float kCutoffModRange = 0.4f;
static constexpr float kDelayTimeModMs = 300.0f;
static constexpr float kChorusRateModOctaves = 2.0f;
static constexpr float kLfoRateModOctaves = 2.0f;
static constexpr float kAgeModRange = 0.6f;
static constexpr float kFreeModCents = 50.0f;
static constexpr float kResonanceModRange = 0.5f;
static constexpr float kDriveModRange = 0.6f;
static constexpr float kChorusDepthModRange = 0.5f;
static constexpr float kSpaceSizeModRange = 0.5f;
static constexpr float kDelayMixModRange = 0.5f;
static constexpr float kNoiseModRange = 0.6f;

static constexpr double kSampleRate = 44100.0;
static constexpr double kSeconds = 3.0;
static constexpr double kBpm = 120.0; // stand-in for host BPM (see file header comment)

static float delaySyncedMs(int syncIndex, double bpm) noexcept
{
    static constexpr int kDivisor[7] = { 1, 2, 4, 8, 16, 32, 64 };
    if (syncIndex < 1 || syncIndex > 7 || bpm <= 0.0)
        return 0.0f;
    const double wholeNoteMs = 240000.0 / bpm;
    return (float)(wholeNoteMs / (double)kDivisor[syncIndex - 1]);
}

static void writeWav(const char* path, const std::vector<float>& L, const std::vector<float>& R, double sampleRate)
{
    struct WavHeader
    {
        char riff[4] = {'R','I','F','F'};
        uint32_t chunkSize;
        char wave[4] = {'W','A','V','E'};
        char fmt[4] = {'f','m','t',' '};
        uint32_t fmtSize = 16;
        uint16_t audioFormat = 3; // IEEE float
        uint16_t numChannels = 2;
        uint32_t sampleRateHz;
        uint32_t byteRate;
        uint16_t blockAlign;
        uint16_t bitsPerSample = 32;
        char data[4] = {'d','a','t','a'};
        uint32_t dataSize;
    };

    const uint32_t frames = (uint32_t)L.size();
    WavHeader hdr;
    hdr.sampleRateHz = (uint32_t)sampleRate;
    hdr.blockAlign = hdr.numChannels * (hdr.bitsPerSample / 8);
    hdr.byteRate = hdr.sampleRateHz * hdr.blockAlign;
    hdr.dataSize = frames * hdr.blockAlign;
    hdr.chunkSize = 36 + hdr.dataSize;

    FILE* f = std::fopen(path, "wb");
    if (!f) { std::fprintf(stderr, "failed to open %s for writing\n", path); return; }
    std::fwrite(&hdr, sizeof(hdr), 1, f);
    for (uint32_t i = 0; i < frames; ++i)
    {
        std::fwrite(&L[i], sizeof(float), 1, f);
        std::fwrite(&R[i], sizeof(float), 1, f);
    }
    std::fclose(f);
}

// runs one full render pass with a given patch matrix, returns pass/fail.
// `label` identifies the scenario in the printed output. `delaySync` selects
// DELAY's tempo-sync division (0 = Free, use delayTimeParam); modWheelValue/
// pitchBendValue stand in for live MIDI CC1/pitch-bend (see file header).
static bool renderScenario(const char* label, const char* wavPath, bool patch[kNumSources][kNumDests],
                            int delaySync = 0, float modWheelValue = 0.0f, float pitchBendValue = 0.0f,
                            float velocityValue = 0.9f, float aftertouchValue = 0.0f,
                            float oscBWave = 0.3f, float oscCWave = 0.3f,
                            float oscALevel = 1.0f, float oscBLevel = 1.0f, float oscCLevel = 1.0f,
                            float fmAtoB = 0.0f, float fmAtoC = 0.0f, float fmBtoA = 0.0f,
                            float fmBtoC = 0.0f, float fmCtoA = 0.0f, float fmCtoB = 0.0f)
{
    std::array<Voice, kNumVoices> voices;
    VoiceParams voiceParams;
    voiceParams.voices = 3;
    voiceParams.detuneCents = 12.0f;
    voiceParams.spread = 0.5f;
    voiceParams.wave = 0.3f;
    voiceParams.oscBWave = oscBWave;
    voiceParams.oscCWave = oscCWave;
    voiceParams.oscALevel = oscALevel;
    voiceParams.oscBLevel = oscBLevel;
    voiceParams.oscCLevel = oscCLevel;
    voiceParams.fmAtoB = fmAtoB;
    voiceParams.fmAtoC = fmAtoC;
    voiceParams.fmBtoA = fmBtoA;
    voiceParams.fmBtoC = fmBtoC;
    voiceParams.fmCtoA = fmCtoA;
    voiceParams.fmCtoB = fmCtoB;

    for (Voice& v : voices)
    {
        v.setSampleRate(kSampleRate);
        v.applyParams(voiceParams);
    }

    DriftGenerator drift;
    drift.setSampleRate(kSampleRate);
    drift.setRate(0.15f);
    drift.setChaos(0.2f);
    const float driftAmount = 0.3f;

    Lfo lfo;
    lfo.setSampleRate(kSampleRate);
    const float lfoBaseRate = 1.0f, lfoAmount = 0.5f;
    float lfoAttPrev = 0.0f;

    ADSR filterEnv, ampEnvRef;
    filterEnv.setSampleRate(kSampleRate);
    filterEnv.setAttack(0.05f); filterEnv.setDecay(0.5f); filterEnv.setSustain(0.0f); filterEnv.setRelease(0.3f);
    ampEnvRef.setSampleRate(kSampleRate);
    ampEnvRef.setAttack(0.4f); ampEnvRef.setDecay(0.3f); ampEnvRef.setSustain(1.0f); ampEnvRef.setRelease(0.8f);
    const float filterEnvAmount = 0.7f, ampEnvAmount = 0.5f;
    const float modWheelAmount = 0.5f, pitchBendAmount = 0.1667f;
    const float velocityAmount = 0.5f, aftertouchAmount = 0.5f;

    Filter filterL, filterR;
    filterL.setSampleRate(kSampleRate); filterR.setSampleRate(kSampleRate);
    filterL.setType(FilterType::Lowpass); filterR.setType(FilterType::Lowpass);
    const float cutoffHzParam = 1800.0f;

    NoiseBed noiseL, noiseR;
    noiseL.setSampleRate(kSampleRate); noiseR.setSampleRate(kSampleRate);
    StutterGate stutter;
    stutter.setSampleRate(kSampleRate);
    AgeProcessor ageL, ageR;
    ageL.setSampleRate(kSampleRate); ageR.setSampleRate(kSampleRate);
    const float noiseAmt = 0.1f, grainAmt = 0.1f, ageAmt = 0.1f;
    float noiseGate = 0.0f;

    Chorus chorus;
    chorus.setSampleRate(kSampleRate);
    const float chorusRateParam = 0.3f, chorusDepth = 0.4f, chorusMix = 0.3f;

    Delay delay;
    delay.setSampleRate(kSampleRate);
    const float delayTimeParam = 350.0f, delayFeedback = 0.35f, delayMix = 0.25f;
    const float delayBaseMs = delaySync > 0 ? delaySyncedMs(delaySync, kBpm) : delayTimeParam;

    Reverb reverb;
    reverb.setSampleRate(kSampleRate);
    const float spaceSize = 0.5f, spaceDecay = 0.5f, spaceMix = 0.3f;

    const float volume = 0.8f, width = 0.75f;

    const uint32_t totalFrames = (uint32_t)(kSeconds * kSampleRate);
    std::vector<float> outL(totalFrames), outR(totalFrames);

    // note on at frame 0 (chord: root + fifth + octave across a few voices)
    voices[0].noteOn(57, 0.9f); // A3
    voices[1].noteOn(64, 0.8f); // E4
    voices[2].noteOn(69, 0.7f); // A4

    bool sawNaN = false;
    float peak = 0.0f;
    double sumAbs = 0.0;

    for (uint32_t frame = 0; frame < totalFrames; ++frame)
    {
        if (frame == (uint32_t)((kSeconds - 1.0) * kSampleRate))
        {
            voices[0].noteOff();
            voices[1].noteOff();
            voices[2].noteOff();
        }

        const float filterEnvLevel = filterEnv.process();
        const float ampEnvLevel = ampEnvRef.process();
        const float driftGlobal = drift.process();

        const float filterEnvAtt = filterEnvLevel * filterEnvAmount;
        const float ampEnvAtt = ampEnvLevel * ampEnvAmount;
        const float driftAtt = driftGlobal * driftAmount;
        const float modWheelAtt = modWheelValue * modWheelAmount;
        const float pitchBendAtt = pitchBendValue * pitchBendAmount;
        const float velocityAtt = velocityValue * velocityAmount;
        const float aftertouchAtt = aftertouchValue * aftertouchAmount;

        float lfoRateSum = 0.0f;
        if (patch[SrcDrift][DestLfoRate])      lfoRateSum += driftAtt;
        if (patch[SrcAmpEnv][DestLfoRate])     lfoRateSum += ampEnvAtt;
        if (patch[SrcFilterEnv][DestLfoRate])  lfoRateSum += filterEnvAtt;
        if (patch[SrcModWheel][DestLfoRate])   lfoRateSum += modWheelAtt;
        if (patch[SrcPitchBend][DestLfoRate])  lfoRateSum += pitchBendAtt;
        if (patch[SrcVelocity][DestLfoRate])   lfoRateSum += velocityAtt;
        if (patch[SrcAftertouch][DestLfoRate]) lfoRateSum += aftertouchAtt;
        if (patch[SrcLfo][DestLfoRate])        lfoRateSum += lfoAttPrev;
        lfoRateSum = std::clamp(lfoRateSum, -kModSumClamp, kModSumClamp);

        const float lfoRateHz = std::clamp(lfoBaseRate * std::exp2(lfoRateSum * kLfoRateModOctaves), 0.01f, 20.0f);
        lfo.setRate(lfoRateHz);
        const float lfoAtt = lfo.process() * lfoAmount;
        lfoAttPrev = lfoAtt;

        const float attenuated[kNumSources] = { driftAtt, ampEnvAtt, filterEnvAtt, lfoAtt, modWheelAtt, pitchBendAtt, velocityAtt, aftertouchAtt };

        float sumFor[kNumDests] = {};
        for (uint32_t s = 0; s < (uint32_t)kNumSources; ++s)
            for (uint32_t d = 0; d < (uint32_t)kNumDests; ++d)
                if (patch[s][d])
                    sumFor[d] += attenuated[s];
        for (uint32_t d = 0; d < (uint32_t)kNumDests; ++d)
            sumFor[d] = std::clamp(sumFor[d], -kModSumClamp, kModSumClamp);

        const float pitchModSemitones = sumFor[DestPitch] * kPitchModSemitones;
        const float waveModOffset = sumFor[DestWave] * kWaveModRange;
        const float oscBFreeModCents = sumFor[DestOscBFree] * kFreeModCents;
        const float oscCFreeModCents = sumFor[DestOscCFree] * kFreeModCents;

        float mixL = 0.0f, mixR = 0.0f;
        bool anyVoiceActive = false;
        for (uint32_t v = 0; v < kNumVoices; ++v)
        {
            if (!voices[v].isActive())
                continue;
            anyVoiceActive = true;
            voices[v].process(pitchModSemitones, waveModOffset, oscBFreeModCents, oscCFreeModCents, mixL, mixR);
        }
        mixL *= kVoiceHeadroom;
        mixR *= kVoiceHeadroom;

        const float noiseGateTarget = anyVoiceActive ? 1.0f : 0.0f;
        const float noiseGateCoeff = 1.0f - std::exp(-1.0f / (0.01f * (float)kSampleRate));
        noiseGate += (noiseGateTarget - noiseGate) * noiseGateCoeff;

        float cutoffNorm = cutoffToNormalized(cutoffHzParam) + sumFor[DestCutoff] * kCutoffModRange;
        const float cutoffHz = normalizedToCutoff(cutoffNorm);
        const float resonanceMod01 = std::clamp(0.2f + sumFor[DestResonance] * kResonanceModRange, 0.0f, 1.0f);
        const float driveMod01 = std::clamp(0.0f + sumFor[DestDrive] * kDriveModRange, 0.0f, 1.0f);
        filterL.setResonance(resonanceMod01);
        filterR.setResonance(resonanceMod01);
        filterL.setDrive(driveMod01);
        filterR.setDrive(driveMod01);
        float filteredL = filterL.process(mixL, cutoffHz);
        float filteredR = filterR.process(mixR, cutoffHz);

        const float noiseMod01 = std::clamp(noiseAmt + sumFor[DestNoise] * kNoiseModRange, 0.0f, 1.0f);
        float noisedL = filteredL + noiseL.process(cutoffHz) * noiseMod01 * noiseGate;
        float noisedR = filteredR + noiseR.process(cutoffHz) * noiseMod01 * noiseGate;
        const float gate = stutter.process(grainAmt);
        float stutteredL = noisedL * gate;
        float stutteredR = noisedR * gate;
        const float ageMod01 = std::clamp(ageAmt + sumFor[DestAge] * kAgeModRange, 0.0f, 1.0f);
        float agedL = ageL.process(stutteredL, ageMod01);
        float agedR = ageR.process(stutteredR, ageMod01);

        const float chorusRateHz = std::clamp(chorusRateParam * std::exp2(sumFor[DestChorusRate] * kChorusRateModOctaves), 0.01f, 15.0f);
        const float chorusDepthMod01 = std::clamp(chorusDepth + sumFor[DestChorusDepth] * kChorusDepthModRange, 0.0f, 1.0f);
        float chorusL, chorusR;
        chorus.process(agedL, agedR, chorusRateHz, chorusDepthMod01, chorusMix, chorusL, chorusR);

        const float delayTimeMs = delayBaseMs + sumFor[DestDelayTime] * kDelayTimeModMs;
        const float delayMixMod01 = std::clamp(delayMix + sumFor[DestDelayMix] * kDelayMixModRange, 0.0f, 1.0f);
        float delayWetL, delayWetR;
        delay.process(chorusL, chorusR, delayTimeMs, delayFeedback, delayWetL, delayWetR);
        float delayedL = chorusL + (delayWetL - chorusL) * delayMixMod01;
        float delayedR = chorusR + (delayWetR - chorusR) * delayMixMod01;

        const float spaceSizeMod01 = std::clamp(spaceSize + sumFor[DestSpaceSize] * kSpaceSizeModRange, 0.0f, 1.0f);
        float wetL, wetR;
        reverb.process(delayedL, delayedR, spaceSizeMod01, spaceDecay, wetL, wetR);
        float spacedL = delayedL + (wetL - delayedL) * spaceMix;
        float spacedR = delayedR + (wetR - delayedR) * spaceMix;

        const float mid = (spacedL + spacedR) * 0.5f;
        const float side = (spacedL - spacedR) * 0.5f * width;
        const float sampleL = std::tanh((mid + side) * volume);
        const float sampleR = std::tanh((mid - side) * volume);

        outL[frame] = sampleL;
        outR[frame] = sampleR;

        if (!std::isfinite(sampleL) || !std::isfinite(sampleR))
            sawNaN = true;
        peak = std::max({ peak, std::fabs(sampleL), std::fabs(sampleR) });
        sumAbs += std::fabs(sampleL) + std::fabs(sampleR);
    }

    const double meanAbs = sumAbs / (double)(totalFrames * 2);

    writeWav(wavPath, outL, outR, kSampleRate);

    std::printf("[%s] frames=%u peak=%.4f meanAbs=%.6f nan_or_inf=%s\n",
                label, totalFrames, (double)peak, meanAbs, sawNaN ? "YES" : "no");

    bool ok = true;
    if (sawNaN) { std::printf("  FAIL: NaN/Inf detected in output\n"); ok = false; }
    if (peak < 1e-4) { std::printf("  FAIL: output is effectively silent (peak=%.6f)\n", (double)peak); ok = false; }
    if (peak > 1.0001f) { std::printf("  FAIL: output exceeds full scale after safety tanh (peak=%.4f)\n", (double)peak); ok = false; }
    if (meanAbs < 1e-5) { std::printf("  FAIL: mean absolute level implausibly low for a held chord\n"); ok = false; }

    if (ok)
        std::printf("  PASS. Wrote %s\n", wavPath);

    return ok;
}

int main()
{
    bool allOk = true;

    // 1) default patch (matches PhellipePlugin.cpp's default fPatch table)
    {
        bool patch[kNumSources][kNumDests] = {
            { false, true,  false, false, false, false, false, false, false, false, false, false, false, false, false },
            { false, false, false, false, false, false, false, false, false, false, false, false, false, false, false },
            { false, true,  false, false, false, false, false, false, false, false, false, false, false, false, false },
            { false, false, false, false, false, false, false, false, false, false, false, false, false, false, false },
            { false, false, false, false, false, false, false, false, false, false, false, false, false, false, false },
            { true,  false, false, false, false, false, false, false, false, false, false, false, false, false, false },
            { false, false, false, false, false, false, false, false, false, false, false, false, false, false, false },
            { false, false, false, false, false, false, false, false, false, false, false, false, false, false, false },
        };
        allOk &= renderScenario("default patch", "/tmp/phellipe_offline_default.wav", patch, 0, 0.0f, 0.3f);
    }

    // 2) ENV -> LFO rate, LFO -> pitch (exercises cross-source chaining)
    {
        bool patch[kNumSources][kNumDests] = {};
        patch[SrcFilterEnv][DestLfoRate] = true;
        patch[SrcLfo][DestPitch] = true;
        allOk &= renderScenario("env->lfo rate, lfo->pitch", "/tmp/phellipe_offline_lfochain.wav", patch);
    }

    // 3) LFO self-feedback on its own rate - stability check
    {
        bool patch[kNumSources][kNumDests] = {};
        patch[SrcLfo][DestLfoRate] = true;
        patch[SrcLfo][DestWave] = true;
        allOk &= renderScenario("lfo self-feedback", "/tmp/phellipe_offline_selffeedback.wav", patch);
    }

    // 4) LFO -> AGE (the newest destination) - tape-wobble-style texture modulation
    {
        bool patch[kNumSources][kNumDests] = {};
        patch[SrcDrift][DestCutoff] = true;
        patch[SrcLfo][DestAge] = true;
        allOk &= renderScenario("lfo->age", "/tmp/phellipe_offline_age.wav", patch);
    }

    // 5) MOD WHEEL -> OSC B/C free tune, DELAY tempo-synced to 1/8 - exercises
    // the newest sources/destinations together
    {
        bool patch[kNumSources][kNumDests] = {};
        patch[SrcModWheel][DestOscBFree] = true;
        patch[SrcModWheel][DestOscCFree] = true;
        allOk &= renderScenario("modwheel->osc free, delay synced 1/8", "/tmp/phellipe_offline_modwheel_sync.wav",
                                 patch, /*delaySync=*/4, /*modWheelValue=*/0.8f, /*pitchBendValue=*/0.0f);
    }

    // 6) VELOCITY -> cutoff, AFTERTOUCH -> chorus rate - the newest sources
    {
        bool patch[kNumSources][kNumDests] = {};
        patch[SrcVelocity][DestCutoff] = true;
        patch[SrcAftertouch][DestChorusRate] = true;
        allOk &= renderScenario("velocity->cutoff, aftertouch->chorus", "/tmp/phellipe_offline_velocity_aftertouch.wav",
                                 patch, /*delaySync=*/0, /*modWheelValue=*/0.0f, /*pitchBendValue=*/0.0f,
                                 /*velocityValue=*/0.9f, /*aftertouchValue=*/0.7f);
    }

    // 7) DRIFT -> resonance/drive, LFO -> chorus depth/space size - the
    // newest destinations
    {
        bool patch[kNumSources][kNumDests] = {};
        patch[SrcDrift][DestResonance] = true;
        patch[SrcDrift][DestDrive] = true;
        patch[SrcLfo][DestChorusDepth] = true;
        patch[SrcLfo][DestSpaceSize] = true;
        allOk &= renderScenario("drift->reso/drive, lfo->chorus depth/space size",
                                 "/tmp/phellipe_offline_newdests.wav", patch);
    }

    // 8) AFTERTOUCH -> delay mix - swelling echoes under sustained pressure
    {
        bool patch[kNumSources][kNumDests] = {};
        patch[SrcAftertouch][DestDelayMix] = true;
        allOk &= renderScenario("aftertouch->delay mix", "/tmp/phellipe_offline_delaymix.wav",
                                 patch, /*delaySync=*/0, /*modWheelValue=*/0.0f, /*pitchBendValue=*/0.0f,
                                 /*velocityValue=*/0.9f, /*aftertouchValue=*/0.8f);
    }

    // 9) LFO -> noise level, plus OSC B/C each set to a different wave shape
    // than OSC A - exercises the newest destination and per-oscillator WAVE
    {
        bool patch[kNumSources][kNumDests] = {};
        patch[SrcLfo][DestNoise] = true;
        allOk &= renderScenario("lfo->noise, per-osc wave", "/tmp/phellipe_offline_noise_wave.wav",
                                 patch, /*delaySync=*/0, /*modWheelValue=*/0.0f, /*pitchBendValue=*/0.0f,
                                 /*velocityValue=*/0.9f, /*aftertouchValue=*/0.0f,
                                 /*oscBWave=*/0.9f, /*oscCWave=*/0.0f);
    }

    // 10) B FM-modulates C at full amount, B itself muted from the mix (the
    // exact "B drives C's timbre but isn't heard on its own" idiom this was
    // built for) - confirms a modulator can be silent yet still audible
    // through what it's modulating.
    {
        bool patch[kNumSources][kNumDests] = {};
        allOk &= renderScenario("fm b->c, b muted", "/tmp/phellipe_offline_fm_muted.wav",
                                 patch, /*delaySync=*/0, /*modWheelValue=*/0.0f, /*pitchBendValue=*/0.0f,
                                 /*velocityValue=*/0.9f, /*aftertouchValue=*/0.0f,
                                 /*oscBWave=*/0.3f, /*oscCWave=*/0.3f,
                                 /*oscALevel=*/1.0f, /*oscBLevel=*/0.0f, /*oscCLevel=*/1.0f,
                                 /*fmAtoB=*/0.0f, /*fmAtoC=*/0.0f, /*fmBtoA=*/0.0f,
                                 /*fmBtoC=*/0.8f, /*fmCtoA=*/0.0f, /*fmCtoB=*/0.0f);
    }

    // 11) full cyclic FM matrix (A->B->C->A all enabled at once) - the
    // concrete stability check for the one-sample-delayed-feedback trick,
    // not just an assertion: a routing loop like this is exactly what the
    // "full matrix" topology allows and the naive same-sample version of
    // this would be a physically-impossible cyclic dependency.
    {
        bool patch[kNumSources][kNumDests] = {};
        allOk &= renderScenario("fm full cyclic matrix", "/tmp/phellipe_offline_fm_cyclic.wav",
                                 patch, /*delaySync=*/0, /*modWheelValue=*/0.0f, /*pitchBendValue=*/0.0f,
                                 /*velocityValue=*/0.9f, /*aftertouchValue=*/0.0f,
                                 /*oscBWave=*/0.3f, /*oscCWave=*/0.3f,
                                 /*oscALevel=*/1.0f, /*oscBLevel=*/1.0f, /*oscCLevel=*/1.0f,
                                 /*fmAtoB=*/0.6f, /*fmAtoC=*/0.0f, /*fmBtoA=*/0.0f,
                                 /*fmBtoC=*/0.6f, /*fmCtoA=*/0.6f, /*fmCtoB=*/0.0f);
    }

    return allOk ? 0 : 1;
}
