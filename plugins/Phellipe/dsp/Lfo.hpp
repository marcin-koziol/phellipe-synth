/*
 * Phellipe - the LFO module: a minimal free-running sine oscillator, one of
 * the four patch-matrix modulation sources (see PhellipePlugin.cpp's run()).
 * Deliberately simple (no waveform choice, no chaos/random-walk component
 * like DriftGenerator has) - it's meant to be a plain, predictable LFO that
 * gets its character from what it's patched into and from having its own
 * rate patchable as a destination, not from internal complexity.
 */

#pragma once

#include <cmath>
#include <algorithm>

namespace phellipe {

class Lfo
{
public:
    void setSampleRate(double sampleRate) noexcept
    {
        fSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    }

    void setRate(float hz) noexcept { fRate = std::max(0.01f, hz); }

    // -1..1
    float process() noexcept
    {
        const float out = std::sin(2.0f * (float)M_PI * (float)fPhase);
        fPhase += (double)fRate / fSampleRate;
        if (fPhase >= 1.0)
            fPhase -= 1.0;
        return out;
    }

private:
    double fSampleRate = 44100.0;
    double fPhase = 0.0;
    float fRate = 1.0f;
};

} // namespace phellipe
