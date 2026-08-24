/*
 * Phellipe - the DRIFT module: a sine LFO (rate-controlled) crossfaded
 * against a smoothed random walk (chaos-controlled). One shared instance,
 * one value per sample - it's one of the four patch-matrix modulation
 * sources (see PhellipePlugin.cpp's run()), so every voice/destination that
 * uses it reads the same global value rather than a per-voice variant.
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace phellipe {

class DriftGenerator
{
public:
    static constexpr uint32_t kMaxVoices = 8;

    void setSampleRate(double sampleRate) noexcept
    {
        fSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    }

    void setRate(float hz) noexcept { fRate = std::max(0.01f, hz); }
    void setChaos(float chaos01) noexcept { fChaos = std::clamp(chaos01, 0.0f, 1.0f); }

    // -1..1, the global (filter cutoff) drift value
    float process() noexcept
    {
        advance();
        const float lfo = std::sin(2.0f * (float)M_PI * (float)fLfoPhase);
        fRandomValue += (fRandomTarget - fRandomValue) * slewCoeff(fBaseTau);
        return lfo * (1.0f - fChaos) + fRandomValue * fChaos;
    }

private:
    void advance() noexcept
    {
        fLfoPhase += (double)fRate / fSampleRate;
        if (fLfoPhase >= 1.0)
            fLfoPhase -= 1.0;

        if (fReseedCountdown <= 0)
        {
            fRandomTarget = randomBipolar();
            const float seconds = (0.3f + randomUnit() * 1.2f) / fRate;
            fReseedCountdown = (int32_t)(seconds * (float)fSampleRate);
        }
        --fReseedCountdown;
    }

    float slewCoeff(float tauSeconds) const noexcept
    {
        return 1.0f - std::exp(-1.0f / (tauSeconds * (float)fSampleRate));
    }

    float randomUnit() noexcept
    {
        fRngState ^= fRngState << 13;
        fRngState ^= fRngState >> 17;
        fRngState ^= fRngState << 5;
        return (float)(fRngState & 0xFFFFFFu) / (float)0xFFFFFFu;
    }

    float randomBipolar() noexcept { return randomUnit() * 2.0f - 1.0f; }

    double fSampleRate = 44100.0;
    double fLfoPhase = 0.0;
    float fRate = 0.15f;
    float fChaos = 0.2f;

    float fRandomValue = 0.0f;
    float fRandomTarget = 0.0f;
    int32_t fReseedCountdown = 0;
    float fBaseTau = 0.4f; // smoothing time constant, seconds

    uint32_t fRngState = 0x9e3779b9u;
};

} // namespace phellipe
