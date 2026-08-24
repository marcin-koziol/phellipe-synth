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

} // namespace phellipe
