/*
 * Phellipe - shared log-space cutoff normalization, used so cutoff
 * modulation (drift) sweeps perceptually-even amounts of the filter's
 * range regardless of where the base cutoff knob sits.
 */

#pragma once

#include <cmath>
#include <algorithm>

namespace phellipe {

static constexpr float kCutoffMin = 20.0f;
static constexpr float kCutoffMax = 20000.0f;

inline float cutoffToNormalized(float hz) noexcept
{
    return std::log(hz / kCutoffMin) / std::log(kCutoffMax / kCutoffMin);
}

inline float normalizedToCutoff(float t) noexcept
{
    t = std::clamp(t, 0.0f, 1.0f);
    return kCutoffMin * std::pow(kCutoffMax / kCutoffMin, t);
}

// The CUTOFF parameter itself is this position as 0..100 % (~10 % per
// octave), not Hz: hosts (VST3 normalisation, CLAP plain values) automate a
// parameter's range linearly, and a linear 20..20000 Hz lane puts 2 kHz at
// 10 % and 10 kHz at the halfway point. As a position, automation sweeps the
// filter evenly in pitch, like the knob does; Hz is only shown in the UI.
inline float cutoffPercentToHz(float percent) noexcept { return normalizedToCutoff(percent * 0.01f); }
inline float cutoffHzToPercent(float hz) noexcept { return 100.0f * cutoffToNormalized(std::clamp(hz, kCutoffMin, kCutoffMax)); }

} // namespace phellipe
