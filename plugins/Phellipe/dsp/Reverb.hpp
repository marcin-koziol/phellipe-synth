/*
 * Phellipe - the SPACE section: a Freeverb/Jezar-at-Dreampoint-style
 * algorithmic reverb (8 parallel lowpass-feedback combs + 4 series allpass
 * filters, per channel), adapted to take genuinely separate stereo input
 * (rather than Freeverb's usual mono-summed input feeding both channel
 * networks) so it preserves whatever stereo image the CORE unison stack
 * already built.
 *
 * DECAY maps to comb feedback (how long the tail rings). SIZE maps to the
 * delay-line lengths themselves (echo density/room size, not decay time) -
 * changing SIZE live causes a brief pitch-warble as the active length
 * jumps, which is fine for a knob meant to be set once per patch rather
 * than automated.
 *
 * All delay buffers are allocated once (at their maximum size) in
 * setSampleRate() and never reallocated in process() - SIZE only moves an
 * "active length" bound within the pre-allocated buffer, so there's no
 * audio-thread heap activity.
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace phellipe {

class Comb
{
public:
    void setMaxLength(uint32_t maxSamples) noexcept
    {
        fBuffer.assign(std::max(1u, maxSamples), 0.0f);
        fActiveLength = (uint32_t)fBuffer.size();
        fIndex = 0;
    }

    void setActiveLength(uint32_t len) noexcept
    {
        fActiveLength = std::clamp(len, 1u, (uint32_t)fBuffer.size());
        if (fIndex >= fActiveLength)
            fIndex = 0;
    }

    float process(float input, float feedback, float damp) noexcept
    {
        const float output = fBuffer[fIndex];
        fFilterStore = output * (1.0f - damp) + fFilterStore * damp;
        fBuffer[fIndex] = input + fFilterStore * feedback;
        fIndex = (fIndex + 1) % fActiveLength;
        return output;
    }

private:
    std::vector<float> fBuffer;
    uint32_t fActiveLength = 1;
    uint32_t fIndex = 0;
    float fFilterStore = 0.0f;
};

class Allpass
{
public:
    void setMaxLength(uint32_t maxSamples) noexcept
    {
        fBuffer.assign(std::max(1u, maxSamples), 0.0f);
        fActiveLength = (uint32_t)fBuffer.size();
        fIndex = 0;
    }

    void setActiveLength(uint32_t len) noexcept
    {
        fActiveLength = std::clamp(len, 1u, (uint32_t)fBuffer.size());
        if (fIndex >= fActiveLength)
            fIndex = 0;
    }

    float process(float input, float feedback = 0.5f) noexcept
    {
        const float bufOut = fBuffer[fIndex];
        const float output = -input + bufOut;
        fBuffer[fIndex] = input + bufOut * feedback;
        fIndex = (fIndex + 1) % fActiveLength;
        return output;
    }

private:
    std::vector<float> fBuffer;
    uint32_t fActiveLength = 1;
    uint32_t fIndex = 0;
};

class Reverb
{
public:
    void setSampleRate(double sampleRate) noexcept
    {
        fSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        const float scale = (float)(fSampleRate / 44100.0);

        static constexpr uint32_t kCombBaseL[kNumCombs]     = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static constexpr uint32_t kAllpassBaseL[kNumAllpass] = { 556, 441, 341, 225 };

        // max active length is 1.5x the base tuning (SIZE range 0.5x..1.5x) -
        // allocate for the max up front, never reallocated afterward.
        for (uint32_t i = 0; i < kNumCombs; ++i)
        {
            const uint32_t maxL = (uint32_t)std::ceil((float)kCombBaseL[i] * 1.5f * scale);
            const uint32_t maxR = (uint32_t)std::ceil((float)(kCombBaseL[i] + kStereoOffset) * 1.5f * scale);
            fCombL[i].setMaxLength(maxL);
            fCombR[i].setMaxLength(maxR);
        }
        for (uint32_t i = 0; i < kNumAllpass; ++i)
        {
            const uint32_t maxL = (uint32_t)std::ceil((float)kAllpassBaseL[i] * 1.5f * scale);
            const uint32_t maxR = (uint32_t)std::ceil((float)(kAllpassBaseL[i] + kStereoOffset) * 1.5f * scale);
            fApL[i].setMaxLength(maxL);
            fApR[i].setMaxLength(maxR);
        }

        fBaseCombL[0]=1116; fBaseCombL[1]=1188; fBaseCombL[2]=1277; fBaseCombL[3]=1356;
        fBaseCombL[4]=1422; fBaseCombL[5]=1491; fBaseCombL[6]=1557; fBaseCombL[7]=1617;
        fBaseApL[0]=556; fBaseApL[1]=441; fBaseApL[2]=341; fBaseApL[3]=225;
        fScale = scale;
        fLastSize = -1.0f;
        applySize(0.5f);
    }

    void process(float inL, float inR, float size01, float decay01, float& outL, float& outR) noexcept
    {
        if (std::abs(size01 - fLastSize) > 0.004f)
            applySize(size01);

        const float feedback = std::clamp(0.7f + decay01 * 0.28f, 0.0f, 0.98f);
        static constexpr float kDamp = 0.25f;

        float sumL = 0.0f, sumR = 0.0f;
        for (uint32_t i = 0; i < kNumCombs; ++i)
        {
            sumL += fCombL[i].process(inL, feedback, kDamp);
            sumR += fCombR[i].process(inR, feedback, kDamp);
        }
        // 8 resonant combs summed unnormalized would otherwise multiply
        // energy well past the dry input's level, especially at high
        // decay/feedback - average rather than sum (same reasoning as
        // Freeverb's own fixed input-gain compensation).
        sumL /= (float)kNumCombs;
        sumR /= (float)kNumCombs;

        for (uint32_t i = 0; i < kNumAllpass; ++i)
        {
            sumL = fApL[i].process(sumL);
            sumR = fApR[i].process(sumR);
        }

        outL = sumL;
        outR = sumR;
    }

private:
    static constexpr uint32_t kNumCombs = 8;
    static constexpr uint32_t kNumAllpass = 4;
    static constexpr uint32_t kStereoOffset = 23;

    void applySize(float size01) noexcept
    {
        fLastSize = size01;
        const float mult = 0.5f + std::clamp(size01, 0.0f, 1.0f); // 0.5x .. 1.5x
        for (uint32_t i = 0; i < kNumCombs; ++i)
        {
            fCombL[i].setActiveLength((uint32_t)std::round((float)fBaseCombL[i] * mult * fScale));
            fCombR[i].setActiveLength((uint32_t)std::round((float)(fBaseCombL[i] + kStereoOffset) * mult * fScale));
        }
        for (uint32_t i = 0; i < kNumAllpass; ++i)
        {
            fApL[i].setActiveLength((uint32_t)std::round((float)fBaseApL[i] * mult * fScale));
            fApR[i].setActiveLength((uint32_t)std::round((float)(fBaseApL[i] + kStereoOffset) * mult * fScale));
        }
    }

    double fSampleRate = 44100.0;
    float fScale = 1.0f;
    float fLastSize = -1.0f;

    uint32_t fBaseCombL[kNumCombs] = {};
    uint32_t fBaseApL[kNumAllpass] = {};

    Comb fCombL[kNumCombs], fCombR[kNumCombs];
    Allpass fApL[kNumAllpass], fApR[kNumAllpass];
};

} // namespace phellipe
