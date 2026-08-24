/*
 * Phellipe - a single polyphonic voice: three independently-tunable
 * oscillator groups (OSC A/B/C - the CORE section), each its own unison
 * stack of detuned, stereo-spread oscillators sharing the same VOICES/
 * DETUNE/SPREAD "thickness" controls, but each with its own WAVE (sine<->saw
 * blend). OSC A always plays the note's root pitch; OSC B/C are each
 * transposed by their own octave/semitone/fine-cents offset, so e.g. B can
 * sit an octave down as a sub layer while C sits a fifth up. Behind all
 * three: a fixed, un-exposed swell envelope (drone-appropriate: slow in,
 * sustain forever while held, slow out - no user-facing ADSR knobs). Pitch
 * modulation and the patch matrix's WAVE offset (see PhellipePlugin.cpp) are
 * computed once globally per sample and passed into process() rather than
 * computed here, so every patch source behaves identically regardless of
 * which destination it's routed to; the WAVE offset is then added to each
 * group's own base wave setting independently.
 */

#pragma once

#include <cmath>
#include <algorithm>

#include "Oscillator.hpp"
#include "ADSR.hpp"

namespace phellipe {

struct VoiceParams
{
    uint32_t voices = 3;       // 1..kMaxUnison, shared by all three oscillator groups
    float detuneCents = 12.0f; // 0..50, unison spread within each group
    float spread = 0.5f;       // 0..1, stereo pan spread within each group
    float wave = 0.3f;         // OSC A's 0 = sine, 1 = saw (see Oscillator::setWave)
                                // base value - process() adds a per-sample patch-
                                // matrix offset to this (and oscBWave/oscCWave
                                // below) independently for each group

    // amp envelope (per-voice) - defaults keep the old fixed swell-in/out
    // behavior (full sustain, decay stage inert) until the user dials in
    // something else via the AMP ENV panel
    float ampAttack = 0.4f, ampDecay = 0.3f, ampSustain = 1.0f, ampRelease = 0.8f;

    // OSC B/C tuning, relative to OSC A's (the note's) root pitch, plus each
    // group's own independent WAVE base value
    float oscBFreeCents = 0.0f; float oscBOctave = 0.0f; float oscBSemi = 0.0f; float oscBWave = 0.3f;
    float oscCFreeCents = 0.0f; float oscCOctave = 0.0f; float oscCSemi = 0.0f; float oscCWave = 0.3f;
};

class Voice
{
public:
    static constexpr uint32_t kMaxUnison = 7;

    Voice() noexcept
    {
        for (uint32_t i = 0; i < kMaxUnison; ++i)
        {
            const float phase = (float)i / (float)kMaxUnison;
            fOscsA[i].resetPhase(phase);
            fOscsB[i].resetPhase(phase);
            fOscsC[i].resetPhase(phase);
        }
    }

    void setSampleRate(double sampleRate) noexcept
    {
        fSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        for (uint32_t i = 0; i < kMaxUnison; ++i)
        {
            fOscsA[i].setSampleRate(sampleRate);
            fOscsB[i].setSampleRate(sampleRate);
            fOscsC[i].setSampleRate(sampleRate);
        }
        fSwellEnv.setSampleRate(sampleRate);
    }

    void applyParams(const VoiceParams& p) noexcept
    {
        fParams = p;
        fSwellEnv.setAttack(p.ampAttack);
        fSwellEnv.setDecay(p.ampDecay);
        fSwellEnv.setSustain(p.ampSustain);
        fSwellEnv.setRelease(p.ampRelease);
        for (uint32_t i = 0; i < kMaxUnison; ++i)
        {
            fOscsA[i].setWave(p.wave);
            fOscsB[i].setWave(p.oscBWave);
            fOscsC[i].setWave(p.oscCWave);
        }
    }

    void noteOn(int note, float velocity) noexcept
    {
        fNote = note;
        fVelocity = velocity;
        fFrequency = noteToHz(note);
        fSwellEnv.noteOn();
        fActive = true;
    }

    void noteOff() noexcept
    {
        fSwellEnv.noteOff();
    }

    void kill() noexcept { fActive = false; }

    bool isActive() const noexcept { return fActive && !fSwellEnv.isIdle(); }
    bool isReleasing() const noexcept { return fSwellEnv.getStage() == ADSR::Stage::Release; }
    int getNote() const noexcept { return fNote; }

    // pitchModSemitones/waveModOffset/oscBFreeModCents/oscCFreeModCents:
    // already-computed, already-summed patch-matrix modulation (see
    // PhellipePlugin.cpp's run()). pitch applies identically to all three
    // oscillator groups; waveModOffset is added to each group's own
    // independent base WAVE value (fParams.wave/oscBWave/oscCWave) before
    // clamping, so a single WAVE patch connection sweeps all three groups in
    // lockstep without erasing their individually-dialed-in tone; the two
    // free-tune mods each apply only to their own group (B or C), added on
    // top of that group's own FREE knob value. outL/outR are added into, not
    // overwritten, so the caller can sum voices directly.
    void process(float pitchModSemitones, float waveModOffset, float oscBFreeModCents, float oscCFreeModCents,
                 float& outL, float& outR) noexcept
    {
        if (!fActive)
            return;

        const float env = fSwellEnv.process();
        if (fSwellEnv.isIdle())
        {
            fActive = false;
            return;
        }

        const float pitchRatio = std::exp2(pitchModSemitones / 12.0f);
        const float oscBRatio = std::exp2((fParams.oscBOctave * 12.0f + fParams.oscBSemi
                                          + (fParams.oscBFreeCents + oscBFreeModCents) / 100.0f) / 12.0f);
        const float oscCRatio = std::exp2((fParams.oscCOctave * 12.0f + fParams.oscCSemi
                                          + (fParams.oscCFreeCents + oscCFreeModCents) / 100.0f) / 12.0f);
        const float waveA = std::clamp(fParams.wave + waveModOffset, 0.0f, 1.0f);
        const float waveB = std::clamp(fParams.oscBWave + waveModOffset, 0.0f, 1.0f);
        const float waveC = std::clamp(fParams.oscCWave + waveModOffset, 0.0f, 1.0f);
        // three oscillator groups summed - equal-power normalized like the
        // unison stack within each, so dialing in B/C doesn't jump the
        // overall level the way a plain 3x sum would
        const float ampGain = (0.25f + 0.75f * fVelocity) * env * (1.0f / std::sqrt(3.0f));

        float mixL = 0.0f, mixR = 0.0f;
        processGroup(fOscsA, fFrequency * pitchRatio, waveA, mixL, mixR);
        processGroup(fOscsB, fFrequency * pitchRatio * oscBRatio, waveB, mixL, mixR);
        processGroup(fOscsC, fFrequency * pitchRatio * oscCRatio, waveC, mixL, mixR);

        outL += mixL * ampGain;
        outR += mixR * ampGain;
    }

private:
    static float noteToHz(int note) noexcept
    {
        return 440.0f * std::exp2(((float)note - 69.0f) / 12.0f);
    }

    // renders one oscillator group's unison stack (VOICES/DETUNE/SPREAD,
    // shared across all three groups) at the given root frequency, adding
    // into mixL/mixR - not gain-normalized here, see the 1/sqrt(3) at the
    // three-group call site in process().
    void processGroup(Oscillator* oscs, float rootFreq, float wave, float& mixL, float& mixR) noexcept
    {
        const uint32_t n = std::clamp(fParams.voices, 1u, kMaxUnison);
        const float unisonGain = 1.0f / std::sqrt((float)n);

        for (uint32_t i = 0; i < n; ++i)
        {
            const float t = n > 1 ? (float)i / (float)(n - 1) : 0.5f;
            const float centsOffset = n > 1 ? (-fParams.detuneCents + t * 2.0f * fParams.detuneCents) : 0.0f;
            const float panPos = n > 1 ? (-fParams.spread + t * 2.0f * fParams.spread) : 0.0f;

            const float freq = rootFreq * std::exp2(centsOffset / 1200.0f);
            oscs[i].setFrequency(freq);
            oscs[i].setWave(wave); // per-sample, so a patched wave modulation actually sweeps
            const float sample = oscs[i].process() * unisonGain;

            const float angle = (panPos + 1.0f) * 0.25f * (float)M_PI; // 0..pi/2
            mixL += sample * std::cos(angle);
            mixR += sample * std::sin(angle);
        }
    }

    Oscillator fOscsA[kMaxUnison];
    Oscillator fOscsB[kMaxUnison];
    Oscillator fOscsC[kMaxUnison];

    ADSR fSwellEnv;
    VoiceParams fParams;

    double fSampleRate = 44100.0;
    float fFrequency = 440.0f;
    int fNote = -1;
    float fVelocity = 0.0f;
    bool fActive = false;
};

} // namespace phellipe
