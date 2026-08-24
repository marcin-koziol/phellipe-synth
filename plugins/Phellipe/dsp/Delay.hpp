/*
 * Phellipe - a stereo ping-pong delay (the DELAY section, separate from
 * SPACE's reverb - the two are meant to be used together: echoes bouncing
 * left/right before the reverb tail smooths them into the room). Cross-
 * feedback (each channel's feedback is fed from the OTHER channel's
 * delayed signal) is what gives ping-pong delays their bouncing character,
 * rather than a plain repeat-in-place echo.
 *
 * The delay buffer is allocated once (at a generous max length) in
 * setSampleRate() and never reallocated in process() - TIME only moves a
 * read offset within that fixed buffer, so there's no audio-thread heap
 * activity.
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace phellipe {

class Delay
{
public:
    void setSampleRate(double sampleRate) noexcept
    {
        fSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        const uint32_t len = (uint32_t)(kMaxSeconds * fSampleRate) + 4;
        fBufferL.assign(len, 0.0f);
        fBufferR.assign(len, 0.0f);
        fIndex = 0;
    }

    // timeMs: delay time per channel (both channels share it - the bounce
    // comes from the cross-feedback, not from differing per-channel times).
    // feedback01: 0..1, clamped comfortably below self-oscillation.
    void process(float inL, float inR, float timeMs, float feedback01,
                 float& outL, float& outR) noexcept
    {
        const uint32_t len = (uint32_t)fBufferL.size();
        const float delaySamples = std::clamp(timeMs, 1.0f, kMaxSeconds * 1000.0f) * 0.001f * (float)fSampleRate;
        const float feedback = std::clamp(feedback01, 0.0f, 0.92f);

        const float readL = readInterpolated(fBufferL, len, delaySamples);
        const float readR = readInterpolated(fBufferR, len, delaySamples);

        fBufferL[fIndex] = inL + readR * feedback; // cross-feed: R's tail feeds L
        fBufferR[fIndex] = inR + readL * feedback; // and L's tail feeds R
        fIndex = (fIndex + 1) % len;

        outL = readL;
        outR = readR;
    }

private:
    static constexpr float kMaxSeconds = 1.5f;

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
    std::vector<float> fBufferL, fBufferR;
    uint32_t fIndex = 0;
};

} // namespace phellipe
