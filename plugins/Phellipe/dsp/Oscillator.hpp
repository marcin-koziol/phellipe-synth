/*
 * Phellipe - a single warm drone oscillator: crossfades from a pure sine
 * (wave=0) to a band-limited (PolyBLEP) saw (wave=1), sharing one phase
 * accumulator so the blend stays coherent as it's swept. Also supports
 * phase modulation (see process()) for inter-oscillator FM (Voice.hpp) -
 * the modulation offsets the waveform readout point only, never the
 * accumulator itself, so a carrier's own pitch stays clean regardless of
 * how hard it's being FM'd.
 */

#pragma once

#include <cmath>

namespace phellipe {

class Oscillator
{
public:
    void setSampleRate(double sampleRate) noexcept
    {
        fSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    }

    void setFrequency(float hz) noexcept
    {
        fIncrement = (double)hz / fSampleRate;
    }

    // 0 = pure sine, 1 = full band-limited saw
    void setWave(float wave01) noexcept
    {
        fWave = wave01 < 0.0f ? 0.0f : (wave01 > 1.0f ? 1.0f : wave01);
    }

    void resetPhase(float phase = 0.0f) noexcept { fPhase = phase; }

    // phaseModOffset: an external FM input (in cycles, unbounded) added to
    // the readout point only - the persistent accumulator (fPhase) advances
    // by the unmodulated fIncrement regardless, so heavy FM can't drag pitch
    // off track. The PolyBLEP correction is still keyed to the unmodulated
    // fPhase/fIncrement (the true discontinuity timing), which is a mild
    // approximation once phaseModOffset shifts the discontinuity's apparent
    // position - an acceptable trade for how much simpler/more stable this
    // is than re-deriving the correction under modulation.
    float process(float phaseModOffset = 0.0f) noexcept
    {
        double modPhase = fPhase + (double)phaseModOffset;
        modPhase -= std::floor(modPhase);

        const float sine = std::sin(2.0f * (float)M_PI * (float)modPhase);

        float saw = 2.0f * (float)modPhase - 1.0f;
        saw -= polyBlep(fPhase, fIncrement);

        const float out = sine + (saw - sine) * fWave;

        fPhase += fIncrement;
        if (fPhase >= 1.0)
            fPhase -= 1.0;
        else if (fPhase < 0.0)
            fPhase += 1.0;

        return out;
    }

private:
    // classic PolyBLEP band-limiting correction at a discontinuity - keeps
    // the saw's edge from aliasing at audio rates.
    static float polyBlep(double t, double dt) noexcept
    {
        if (dt <= 0.0)
            return 0.0f;

        if (t < dt)
        {
            const double x = t / dt;
            return (float)(x + x - x * x - 1.0);
        }
        else if (t > 1.0 - dt)
        {
            const double x = (t - 1.0) / dt;
            return (float)(x * x + x + x + 1.0);
        }
        return 0.0f;
    }

    double fSampleRate = 44100.0;
    double fPhase = 0.0;
    double fIncrement = 0.0;
    float fWave = 0.3f;
};

} // namespace phellipe
