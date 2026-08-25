/*
 * Phellipe - a handful of factory example presets, seeded into the user's
 * preset directory (see ui/PresetStore.hpp) the first time the UI ever runs
 * with an empty preset library. Compiled in (rather than shipped as separate
 * data files) so they exist identically across every build/install of the
 * plugin regardless of platform or plugin format - no bundle-path discovery
 * needed, they just get written out as ordinary, user-editable ".phlpreset"
 * files on first run and behave exactly like any preset the user saves
 * themselves from that point on.
 */

#pragma once

#include "PresetStore.hpp"

#include <cstring>
#include <utility>
#include <vector>

namespace phellipe {
namespace ui {

struct FactoryPreset
{
    const char* name;
    std::vector<std::pair<const char*, float>> overrides; // symbol -> value; everything else stays at that param's Params.hpp default
};

inline const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets = {
        // dark, sub-heavy, barely-moving - the default starting point
        { "Deep Well", {
            { "voices", 5.0f }, { "detune", 8.0f }, { "spread", 0.6f }, { "wave", 0.1f },
            { "cutoff", 600.0f }, { "resonance", 0.35f }, { "drive", 0.1f },
            { "drift_rate", 0.08f }, { "drift_depth", 0.4f }, { "drift_chaos", 0.15f },
            { "noise", 0.05f }, { "grain", 0.0f }, { "age", 0.1f },
            { "space_size", 0.7f }, { "space_decay", 0.7f }, { "space_mix", 0.4f },
            { "volume", 0.8f }, { "width", 0.7f },
        }},
        // wider, brighter, more actively drifting - an evolving pad
        { "Slow Bloom", {
            { "voices", 4.0f }, { "detune", 18.0f }, { "spread", 0.7f }, { "wave", 0.5f },
            { "cutoff", 2200.0f }, { "resonance", 0.25f }, { "drive", 0.0f },
            { "drift_rate", 0.2f }, { "drift_depth", 0.6f }, { "drift_chaos", 0.3f },
            { "noise", 0.1f }, { "grain", 0.0f }, { "age", 0.0f },
            { "space_size", 0.6f }, { "space_decay", 0.55f }, { "space_mix", 0.35f },
            { "volume", 0.75f }, { "width", 0.85f },
        }},
        // texture-forward: noisy, stuttering, degraded
        { "Rust & Static", {
            { "voices", 3.0f }, { "detune", 25.0f }, { "spread", 0.4f }, { "wave", 0.7f },
            { "cutoff", 1500.0f }, { "resonance", 0.5f }, { "drive", 0.3f },
            { "drift_rate", 0.3f }, { "drift_depth", 0.3f }, { "drift_chaos", 0.6f },
            { "noise", 0.35f }, { "grain", 0.4f }, { "age", 0.6f },
            { "space_size", 0.4f }, { "space_decay", 0.4f }, { "space_mix", 0.25f },
            { "volume", 0.7f }, { "width", 0.6f },
        }},
        // vast, cold, wide open
        { "Glacier", {
            { "voices", 6.0f }, { "detune", 6.0f }, { "spread", 0.9f }, { "wave", 0.05f },
            { "cutoff", 3500.0f }, { "resonance", 0.15f }, { "drive", 0.0f },
            { "drift_rate", 0.05f }, { "drift_depth", 0.5f }, { "drift_chaos", 0.1f },
            { "noise", 0.02f }, { "grain", 0.0f }, { "age", 0.0f },
            { "space_size", 1.0f }, { "space_decay", 0.85f }, { "space_mix", 0.6f },
            { "volume", 0.7f }, { "width", 1.0f },
        }},
        // narrow, wobbly, heavily aged - a half-remembered tape loop
        { "Tape Ghost", {
            { "voices", 2.0f }, { "detune", 15.0f }, { "spread", 0.3f }, { "wave", 0.4f },
            { "cutoff", 1200.0f }, { "resonance", 0.3f }, { "drive", 0.15f },
            { "drift_rate", 0.4f }, { "drift_depth", 0.4f }, { "drift_chaos", 0.5f },
            { "noise", 0.15f }, { "grain", 0.2f }, { "age", 0.8f },
            { "space_size", 0.5f }, { "space_decay", 0.5f }, { "space_mix", 0.3f },
            { "volume", 0.75f }, { "width", 0.5f },
        }},

        // --- the following showcase the semi-modular patch matrix and the
        // OSC B/C tuning added later - each demonstrates one distinct patch
        // idiom rather than just being "another sound".

        // OSC A at root, B an octave down, C a fifth up - a droning chord
        // from a single held note. No exotic patching, just interval tuning.
        { "Interval Choir", {
            { "voices", 2.0f }, { "detune", 6.0f }, { "spread", 0.6f }, { "wave", 0.25f },
            { "osc_b_octave", -1.0f }, { "osc_c_semi", 7.0f },
            { "cutoff", 2000.0f }, { "resonance", 0.2f }, { "drive", 0.05f },
            { "drift_rate", 0.1f }, { "drift_depth", 0.3f }, { "drift_chaos", 0.1f },
            { "noise", 0.05f }, { "grain", 0.0f }, { "age", 0.0f },
            { "space_size", 0.6f }, { "space_decay", 0.6f }, { "space_mix", 0.35f },
            { "volume", 0.75f }, { "width", 0.8f },
            { "amp_attack", 0.6f }, { "amp_decay", 0.3f }, { "amp_sustain", 1.0f }, { "amp_release", 1.0f },
            { "delay_time", 400.0f }, { "delay_feedback", 0.3f }, { "delay_mix", 0.15f },
            { "chorus_rate", 0.25f }, { "chorus_depth", 0.5f }, { "chorus_mix", 0.35f },
        }},

        // the FILTER ENV's own attack/decay shape - not the filter directly -
        // drives the LFO's speed, and that LFO sweeps both cutoff and wave
        // blend: each note's envelope indirectly conducts a little melody of
        // its own movement. patch_filterenv_cutoff is turned off so FILTER
        // ENV's only job here is steering the LFO, not the filter itself.
        { "Ghost Chain", {
            { "voices", 3.0f }, { "detune", 10.0f }, { "spread", 0.5f }, { "wave", 0.35f },
            { "cutoff", 1200.0f }, { "resonance", 0.3f }, { "drive", 0.05f },
            { "drift_rate", 0.1f }, { "drift_depth", 0.2f }, { "drift_chaos", 0.2f },
            { "noise", 0.05f }, { "grain", 0.1f }, { "age", 0.1f },
            { "space_size", 0.6f }, { "space_decay", 0.6f }, { "space_mix", 0.3f },
            { "volume", 0.75f }, { "width", 0.7f },
            { "amp_attack", 0.5f }, { "amp_decay", 0.3f }, { "amp_sustain", 1.0f }, { "amp_release", 1.0f },
            { "filter_attack", 0.02f }, { "filter_decay", 1.2f }, { "filter_sustain", 0.3f }, { "filter_release", 0.6f },
            { "delay_time", 300.0f }, { "delay_feedback", 0.25f }, { "delay_mix", 0.2f },
            { "chorus_rate", 0.2f }, { "chorus_depth", 0.3f }, { "chorus_mix", 0.2f },
            { "lfo_rate", 0.3f }, { "filterenv_amount", 0.8f }, { "lfo_amount", 0.6f },
            { "patch_filterenv_cutoff", 0.0f }, { "patch_filterenv_lforate", 1.0f },
            { "patch_lfo_cutoff", 1.0f }, { "patch_lfo_wave", 1.0f },
        }},

        // LFO patched into its own rate (one-sample-delayed self-feedback -
        // stable, just chaotic) plus into AGE: an unpredictable, ever-mutating
        // texture that never quite repeats.
        { "Feedback Loop", {
            { "voices", 4.0f }, { "detune", 20.0f }, { "spread", 0.5f }, { "wave", 0.6f },
            { "cutoff", 1800.0f }, { "resonance", 0.4f }, { "drive", 0.2f },
            { "drift_rate", 0.15f }, { "drift_depth", 0.2f }, { "drift_chaos", 0.4f },
            { "noise", 0.1f }, { "grain", 0.3f }, { "age", 0.2f },
            { "space_size", 0.5f }, { "space_decay", 0.5f }, { "space_mix", 0.25f },
            { "volume", 0.7f }, { "width", 0.6f },
            { "amp_attack", 0.3f }, { "amp_decay", 0.3f }, { "amp_sustain", 0.9f }, { "amp_release", 0.8f },
            { "delay_time", 250.0f }, { "delay_feedback", 0.4f }, { "delay_mix", 0.2f },
            { "chorus_rate", 0.4f }, { "chorus_depth", 0.5f }, { "chorus_mix", 0.3f },
            { "lfo_rate", 2.0f }, { "lfo_amount", 0.7f },
            { "patch_lfo_lforate", 1.0f }, { "patch_lfo_age", 1.0f },
        }},

        // DRIFT (slow, deep) steers the LFO's speed, and the LFO itself
        // swings the pitch - a wandering vibrato whose rate never settles,
        // like a pendulum losing and regaining momentum.
        { "Pendulum", {
            { "voices", 3.0f }, { "detune", 8.0f }, { "spread", 0.4f }, { "wave", 0.2f },
            { "cutoff", 2500.0f }, { "resonance", 0.2f }, { "drive", 0.0f },
            { "drift_rate", 0.06f }, { "drift_depth", 0.6f }, { "drift_chaos", 0.2f },
            { "noise", 0.03f }, { "grain", 0.0f }, { "age", 0.05f },
            { "space_size", 0.55f }, { "space_decay", 0.6f }, { "space_mix", 0.3f },
            { "volume", 0.75f }, { "width", 0.7f },
            { "amp_attack", 0.8f }, { "amp_decay", 0.3f }, { "amp_sustain", 1.0f }, { "amp_release", 1.2f },
            { "delay_time", 500.0f }, { "delay_feedback", 0.2f }, { "delay_mix", 0.15f },
            { "chorus_rate", 0.15f }, { "chorus_depth", 0.2f }, { "chorus_mix", 0.15f },
            { "lfo_rate", 0.4f }, { "lfo_amount", 0.35f },
            { "patch_drift_lforate", 1.0f }, { "patch_lfo_pitch", 1.0f },
        }},

        // the note's own AMP ENV shapes both cutoff and AGE as it swells -
        // an envelope-follower idiom: brighter and cleaner at the peak of
        // each note, darker and grittier as it fades. patch_filterenv_cutoff
        // is off so AMP ENV is the only thing driving the filter here.
        { "Static Bloom", {
            { "voices", 3.0f }, { "detune", 12.0f }, { "spread", 0.5f }, { "wave", 0.4f },
            { "cutoff", 800.0f }, { "resonance", 0.3f }, { "drive", 0.1f },
            { "drift_rate", 0.1f }, { "drift_depth", 0.2f }, { "drift_chaos", 0.2f },
            { "noise", 0.08f }, { "grain", 0.15f }, { "age", 0.1f },
            { "space_size", 0.6f }, { "space_decay", 0.55f }, { "space_mix", 0.3f },
            { "volume", 0.75f }, { "width", 0.7f },
            { "amp_attack", 1.0f }, { "amp_decay", 0.4f }, { "amp_sustain", 0.8f }, { "amp_release", 1.5f },
            { "delay_time", 350.0f }, { "delay_feedback", 0.3f }, { "delay_mix", 0.2f },
            { "chorus_rate", 0.2f }, { "chorus_depth", 0.3f }, { "chorus_mix", 0.2f },
            { "ampenv_amount", 0.8f },
            { "patch_filterenv_cutoff", 0.0f }, { "patch_ampenv_cutoff", 1.0f }, { "patch_ampenv_age", 1.0f },
        }},

        // --- the following showcase MIDI sync/tempo sync and the MOD WHEEL/
        // PITCH BEND/VELOCITY/AFTERTOUCH patch sources added later, plus the
        // RESONANCE/DRIVE/CHORUS DEPTH/SPACE SIZE/DELAY MIX destinations.

        // DELAY locked to a half-note division instead of free-running ms -
        // echoes stay in time with the host no matter the tempo.
        { "Synced Chapel", {
            { "voices", 4.0f }, { "detune", 10.0f }, { "spread", 0.7f }, { "wave", 0.2f },
            { "cutoff", 1800.0f }, { "resonance", 0.2f }, { "drive", 0.0f },
            { "drift_rate", 0.08f }, { "drift_depth", 0.3f }, { "drift_chaos", 0.15f },
            { "noise", 0.03f }, { "grain", 0.0f }, { "age", 0.0f },
            { "space_size", 0.8f }, { "space_decay", 0.75f }, { "space_mix", 0.45f },
            { "volume", 0.72f }, { "width", 0.85f },
            { "amp_attack", 0.7f }, { "amp_decay", 0.3f }, { "amp_sustain", 1.0f }, { "amp_release", 1.4f },
            { "delay_time", 350.0f }, { "delay_feedback", 0.45f }, { "delay_mix", 0.3f }, { "delay_sync", 2.0f },
            { "chorus_rate", 0.2f }, { "chorus_depth", 0.3f }, { "chorus_mix", 0.2f },
        }},

        // channel aftertouch swells both the delay's wet mix and the reverb's
        // room size - lean into a held note and it blooms outward.
        { "Breath Control", {
            { "voices", 3.0f }, { "detune", 10.0f }, { "spread", 0.5f }, { "wave", 0.3f },
            { "cutoff", 1500.0f }, { "resonance", 0.25f }, { "drive", 0.05f },
            { "drift_rate", 0.1f }, { "drift_depth", 0.25f }, { "drift_chaos", 0.15f },
            { "noise", 0.05f }, { "grain", 0.0f }, { "age", 0.05f },
            { "space_size", 0.4f }, { "space_decay", 0.5f }, { "space_mix", 0.2f },
            { "volume", 0.75f }, { "width", 0.7f },
            { "amp_attack", 0.6f }, { "amp_decay", 0.3f }, { "amp_sustain", 1.0f }, { "amp_release", 1.0f },
            { "delay_time", 300.0f }, { "delay_feedback", 0.35f }, { "delay_mix", 0.1f },
            { "chorus_rate", 0.2f }, { "chorus_depth", 0.25f }, { "chorus_mix", 0.2f },
            { "aftertouch_amount", 0.9f },
            { "patch_aftertouch_delaymix", 1.0f }, { "patch_aftertouch_spacesize", 1.0f },
        }},

        // the mod wheel opens up resonance and drive together - a classic
        // "push the wheel, the filter growls" gesture.
        { "Wax Wheel", {
            { "voices", 3.0f }, { "detune", 15.0f }, { "spread", 0.5f }, { "wave", 0.5f },
            { "cutoff", 1200.0f }, { "resonance", 0.15f }, { "drive", 0.0f },
            { "drift_rate", 0.12f }, { "drift_depth", 0.2f }, { "drift_chaos", 0.2f },
            { "noise", 0.05f }, { "grain", 0.1f }, { "age", 0.1f },
            { "space_size", 0.5f }, { "space_decay", 0.5f }, { "space_mix", 0.25f },
            { "volume", 0.75f }, { "width", 0.65f },
            { "amp_attack", 0.4f }, { "amp_decay", 0.3f }, { "amp_sustain", 0.9f }, { "amp_release", 0.9f },
            { "delay_time", 280.0f }, { "delay_feedback", 0.3f }, { "delay_mix", 0.15f },
            { "chorus_rate", 0.3f }, { "chorus_depth", 0.3f }, { "chorus_mix", 0.2f },
            { "modwheel_amount", 0.8f },
            { "patch_modwheel_resonance", 1.0f }, { "patch_modwheel_drive", 1.0f },
        }},

        // hit harder and the tone gets both brighter and dirtier - velocity
        // driving cutoff and drive together, the way a real amp responds to
        // being pushed.
        { "Heavy Hands", {
            { "voices", 3.0f }, { "detune", 14.0f }, { "spread", 0.4f }, { "wave", 0.55f },
            { "cutoff", 900.0f }, { "resonance", 0.3f }, { "drive", 0.0f },
            { "drift_rate", 0.1f }, { "drift_depth", 0.2f }, { "drift_chaos", 0.25f },
            { "noise", 0.05f }, { "grain", 0.1f }, { "age", 0.05f },
            { "space_size", 0.45f }, { "space_decay", 0.5f }, { "space_mix", 0.25f },
            { "volume", 0.75f }, { "width", 0.6f },
            { "amp_attack", 0.1f }, { "amp_decay", 0.3f }, { "amp_sustain", 0.9f }, { "amp_release", 0.7f },
            { "delay_time", 260.0f }, { "delay_feedback", 0.25f }, { "delay_mix", 0.15f },
            { "chorus_rate", 0.25f }, { "chorus_depth", 0.25f }, { "chorus_mix", 0.2f },
            { "velocity_amount", 0.8f },
            { "patch_velocity_cutoff", 1.0f }, { "patch_velocity_drive", 1.0f },
        }},

        // a wider-than-default pitch bend that also widens the chorus as you
        // bend - dive the wheel and the whole sound stretches with it.
        { "Rubber Horizon", {
            { "voices", 3.0f }, { "detune", 10.0f }, { "spread", 0.6f }, { "wave", 0.25f },
            { "cutoff", 2000.0f }, { "resonance", 0.2f }, { "drive", 0.0f },
            { "drift_rate", 0.09f }, { "drift_depth", 0.25f }, { "drift_chaos", 0.15f },
            { "noise", 0.03f }, { "grain", 0.0f }, { "age", 0.0f },
            { "space_size", 0.6f }, { "space_decay", 0.6f }, { "space_mix", 0.3f },
            { "volume", 0.75f }, { "width", 0.8f },
            { "amp_attack", 0.5f }, { "amp_decay", 0.3f }, { "amp_sustain", 1.0f }, { "amp_release", 1.0f },
            { "delay_time", 320.0f }, { "delay_feedback", 0.3f }, { "delay_mix", 0.2f },
            { "chorus_rate", 0.25f }, { "chorus_depth", 0.2f }, { "chorus_mix", 0.25f },
            { "pitchbend_amount", 0.4f },
            { "patch_pitchbend_chorusdepth", 1.0f },
        }},

        // OSC B is muted from the mix entirely (osc_b_level = 0) but still
        // running, and heavily FM's OSC C (fm_b_to_c) - a "hidden operator"
        // shaping C's timbre into a clangorous bell/metallic texture while
        // never being heard on its own. OSC A stays a plain sine-ish root
        // underneath for a stable fundamental.
        { "Hidden Operator", {
            { "voices", 2.0f }, { "detune", 8.0f }, { "spread", 0.5f }, { "wave", 0.1f },
            { "osc_c_octave", -1.0f }, { "osc_c_semi", 7.0f }, { "osc_c_wave", 0.0f },
            { "osc_b_level", 0.0f },
            { "cutoff", 2200.0f }, { "resonance", 0.2f }, { "drive", 0.05f },
            { "drift_rate", 0.07f }, { "drift_depth", 0.2f }, { "drift_chaos", 0.15f },
            { "noise", 0.02f }, { "grain", 0.0f }, { "age", 0.0f },
            { "space_size", 0.55f }, { "space_decay", 0.55f }, { "space_mix", 0.3f },
            { "volume", 0.7f }, { "width", 0.7f },
            { "amp_attack", 0.6f }, { "amp_decay", 0.3f }, { "amp_sustain", 1.0f }, { "amp_release", 1.2f },
            { "delay_time", 340.0f }, { "delay_feedback", 0.3f }, { "delay_mix", 0.2f },
            { "chorus_rate", 0.2f }, { "chorus_depth", 0.2f }, { "chorus_mix", 0.15f },
            { "fm_b_to_c", 0.5f },
        }},
    };
    return presets;
}

// writes every factory preset to disk via the normal savePreset() path (so
// they're indistinguishable from user-saved presets from that point on) -
// call once, only when the preset library is empty (first run).
inline void seedFactoryPresets()
{
    for (const FactoryPreset& fp : factoryPresets())
    {
        float values[kParamCount];
        for (uint32_t i = 0; i < kParamCount; ++i)
            values[i] = getParamInfo(i).def;

        for (const auto& kv : fp.overrides)
        {
            for (uint32_t i = 0; i < kParamCount; ++i)
            {
                if (std::strcmp(getParamInfo(i).symbol, kv.first) == 0)
                {
                    values[i] = kv.second;
                    break;
                }
            }
        }

        savePreset(fp.name, values);
    }
}

} // namespace ui
} // namespace phellipe
