/*
 * Phellipe - a stereo chorus/ensemble (thickening, drifting texture layer):
 * one LFO-modulated short delay tap per channel, the two LFOs held in
 * quadrature (90 degrees apart) so the modulation itself is stereo-wide
 * rather than moving both channels in lockstep. A small fixed feedback
 * amount thickens it toward "ensemble" character rather than a thin,
 * plain chorus.
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace phellipe {

class Chorus
{
public:
    void setSampleRate(double sampleRate) noexcept
    {
        fSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        const uint32_t len = (uint32_t)(0.05 * fSampleRate) + 4; // 50ms max buffer
        fBufferL.assign(len, 0.0f);
        fBufferR.assign(len, 0.0f);
        fIndex = 0;
        fPhase = 0.0;
    }

    // rateHz: LFO speed. depth01: modulation depth (0 = static, near-inaudible
    // short delay; 1 = a full, wavering ensemble sweep). mix01: dry/wet.
    void process(float inL, float inR, float rateHz, float depth01, float mix01,
                 float& outL, float& outR) noexcept
    {
        fPhase += (double)std::max(0.01f, rateHz) / fSampleRate;
        if (fPhase >= 1.0)
            fPhase -= 1.0;

        const float depthMs = std::clamp(depth01, 0.0f, 1.0f) * 7.0f;
        const float baseMs = 12.0f; // nominal center delay, classic chorus range

        const float lfoL = std::sin(2.0f * (float)M_PI * (float)fPhase);
        const float lfoR = std::sin(2.0f * (float)M_PI * (float)(fPhase + 0.25)); // quadrature

        const float delayMsL = baseMs + lfoL * depthMs;
        const float delayMsR = baseMs + lfoR * depthMs;

        const uint32_t len = (uint32_t)fBufferL.size();
        const float tapL = readInterpolated(fBufferL, len, delayMsL * 0.001f * (float)fSampleRate);
        const float tapR = readInterpolated(fBufferR, len, delayMsR * 0.001f * (float)fSampleRate);

        static constexpr float kFeedback = 0.18f; // fixed, just enough to feel like an ensemble, not a resonant comb
        fBufferL[fIndex] = inL + tapL * kFeedback;
        fBufferR[fIndex] = inR + tapR * kFeedback;
        fIndex = (fIndex + 1) % len;

        const float mix = std::clamp(mix01, 0.0f, 1.0f);
        outL = inL + (tapL - inL) * mix;
        outR = inR + (tapR - inR) * mix;
    }

private:
    float readInterpolated(const std::vector<float>& buf, uint32_t len, float delaySamples) const noexcept
    {
        float readPos = (float)fIndex - delaySamples;
        while (readPos < 0.0f)
            readPos += (float)len;
        const uint32_t i0 = (uint32_t)readPos % len;
        const uint32_t i1 = (i0 + 1) % len;
        const float frac = readPos - std::floor(readPos);
        return buf[i0] + (buf[i1] - buf[i0]) * frac;
    }

    double fSampleRate = 44100.0;
    double fPhase = 0.0;
    std::vector<float> fBufferL, fBufferR;
    uint32_t fIndex = 0;
};

} // namespace phellipe
