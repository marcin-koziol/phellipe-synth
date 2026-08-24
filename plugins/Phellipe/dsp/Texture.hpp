/*
 * Phellipe - the GRAIN section's three independent texture layers:
 *  - NoiseBed: a filtered noise bed mixed in at NOISE amount, tracking the
 *    RESONATOR's cutoff so it sits in the same spectral region as the tone.
 *  - StutterGate: a smoothed random-hold amplitude gate ("grain" in the
 *    granular-adjacent, not literal-granular-synthesis, sense) - GRAIN=0
 *    always passes at unity, GRAIN=1 chops deeply. The gate value itself is
 *    one-pole smoothed (~3ms) so the random hold doesn't click.
 *  - AgeProcessor: lo-fi "aged tape" character - a short wow/flutter-
 *    modulated delay, then bit-depth crush, then sample-hold downsampling,
 *    all scaled by AGE (0 = clean, 1 = heavily degraded).
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

#include "Filter.hpp"

namespace phellipe {

// tiny xorshift32 PRNG shared by the texture layers - real-time safe, no
// heap/syscalls, deterministic per-instance seed.
class SimpleRng
{
public:
    explicit SimpleRng(uint32_t seed = 0x9e3779b9u) noexcept : fState(seed ? seed : 1u) {}

    float unit() noexcept
    {
        fState ^= fState << 13;
        fState ^= fState >> 17;
        fState ^= fState << 5;
        return (float)(fState & 0xFFFFFFu) / (float)0xFFFFFFu;
    }

private:
    uint32_t fState;
};

class NoiseBed
{
public:
    void setSampleRate(double sampleRate) noexcept
    {
        fFilter.setSampleRate(sampleRate);
        fFilter.setType(FilterType::Bandpass);
        fFilter.setResonance(0.15f);
    }

    // cutoffHz: track the RESONATOR's current cutoff so the noise bed glues
    // to the filtered tone rather than sitting in an unrelated band.
    float process(float cutoffHz) noexcept
    {
        const float n = fRng.unit() * 2.0f - 1.0f;
        return fFilter.process(n, std::clamp(cutoffHz, 80.0f, 12000.0f));
    }

private:
    Filter fFilter;
    SimpleRng fRng{0x2545F491u};
};

class StutterGate
{
public:
    void setSampleRate(double sampleRate) noexcept
    {
        fSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        fHoldCountdown = 0;
    }

    // grain01: 0 = always unity (no effect), 1 = deep, choppy gating
    float process(float grain01) noexcept
    {
        if (fHoldCountdown <= 0)
        {
            const float seconds = 0.02f + fRng.unit() * 0.18f; // 20-200ms
            fHoldCountdown = (int32_t)(seconds * (float)fSampleRate);
            fTargetGate = 1.0f - grain01 * fRng.unit();
        }
        --fHoldCountdown;

        const float coeff = 1.0f - std::exp(-1.0f / (0.003f * (float)fSampleRate)); // ~3ms
        fGate += (fTargetGate - fGate) * coeff;
        return fGate;
    }

private:
    double fSampleRate = 44100.0;
    int32_t fHoldCountdown = 0;
    float fTargetGate = 1.0f;
    float fGate = 1.0f;
    SimpleRng fRng{0x85EBCA6Bu};
};

class AgeProcessor
{
public:
    void setSampleRate(double sampleRate) noexcept
    {
        fSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        const uint32_t len = (uint32_t)(0.05 * fSampleRate) + 4; // 50ms max buffer
        fDelayBuffer.assign(len, 0.0f);
        fWriteIndex = 0;
        fWowPhase = fFlutterPhase = 0.0;
        fHoldCounter = 0;
        fHeldValue = 0.0f;
    }

    // age01: 0 = clean/hi-fi passthrough, 1 = heavily degraded tape character
    float process(float in, float age01) noexcept
    {
        age01 = std::clamp(age01, 0.0f, 1.0f);

        // 1) wow/flutter: modulate a short delay line's read offset
        const float depthMs = age01 * 3.0f;
        fWowPhase += 0.7 / fSampleRate;
        if (fWowPhase >= 1.0) fWowPhase -= 1.0;
        fFlutterPhase += 5.0 / fSampleRate;
        if (fFlutterPhase >= 1.0) fFlutterPhase -= 1.0;

        const float modMs = std::sin(2.0f * (float)M_PI * (float)fWowPhase) * depthMs * 0.6f
                           + std::sin(2.0f * (float)M_PI * (float)fFlutterPhase) * depthMs * 0.4f;
        const float delayMs = depthMs + modMs; // stays >= 0 since |modMs| <= depthMs

        const uint32_t len = (uint32_t)fDelayBuffer.size();
        fDelayBuffer[fWriteIndex] = in;

        const float delaySamples = delayMs * 0.001f * (float)fSampleRate;
        float readPos = (float)fWriteIndex - delaySamples;
        while (readPos < 0.0f)
            readPos += (float)len;
        const uint32_t i0 = (uint32_t)readPos % len;
        const uint32_t i1 = (i0 + 1) % len;
        const float frac = readPos - std::floor(readPos);
        const float delayed = fDelayBuffer[i0] + (fDelayBuffer[i1] - fDelayBuffer[i0]) * frac;

        fWriteIndex = (fWriteIndex + 1) % len;

        // 2) bit-depth crush
        const float bits = 24.0f - age01 * 20.0f; // 24..4
        const float levels = std::pow(2.0f, bits);
        const float crushed = std::round(delayed * levels) / levels;

        // 3) sample-and-hold downsampling
        const uint32_t holdSamples = 1u + (uint32_t)(age01 * 19.0f); // 1..20
        if (fHoldCounter == 0)
            fHeldValue = crushed;
        fHoldCounter = (fHoldCounter + 1) % holdSamples;

        return fHeldValue;
    }

private:
    double fSampleRate = 44100.0;
    std::vector<float> fDelayBuffer;
    uint32_t fWriteIndex = 0;
    double fWowPhase = 0.0, fFlutterPhase = 0.0;
    uint32_t fHoldCounter = 0;
    float fHeldValue = 0.0f;
};

} // namespace phellipe
