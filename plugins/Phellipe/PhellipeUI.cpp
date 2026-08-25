/*
 * Phellipe - custom voronoi-mosaic Cairo UI. Drawing/layout logic lives in
 * ui/UIPainter.hpp; this file wires mouse/keyboard input to parameter
 * changes, plus the preset bar's CRUD - a small file-based preset library
 * (see ui/PresetStore.hpp) that's entirely UI-side, since a preset is just
 * "every parameter's current value" and the UI already owns that.
 */

#include "DistrhoUI.hpp"
#include "Params.hpp"
#include "ui/FactoryPresets.hpp"
#include "ui/PresetStore.hpp"
#include "ui/UIPainter.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::CairoGraphicsContext;
using namespace phellipe;

// -----------------------------------------------------------------------------------------------------------

class PhellipeUI : public UI
{
public:
    PhellipeUI()
        : UI()
    {
        fLayout = ui::buildLayout((float)getWidth(), (float)getHeight());

        for (uint32_t i = 0; i < kParamCount; ++i)
            fValues[i] = getParamInfo(i).def;

        fPresetNames = ui::listPresets();
        if (fPresetNames.empty())
        {
            ui::seedFactoryPresets();
            fPresetNames = ui::listPresets();
        }
    }

protected:
    // ---------------------------------------------------------------------

    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= kParamCount)
            return;

        // Meters and the scope buffer are output-only telemetry (DSP-driven,
        // updated continuously even with no notes held - drift/LFO never
        // stop) - they must keep repainting live but must never mark the
        // preset unsaved or trigger a knob's value-flash popup.
        const bool isOutputParam = index >= kParamMeterFirst && index < kParamPatchFirst;

        if (value != fValues[index] && !isOutputParam)
        {
            fValueDisplayUntil[index] = std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
            fHasActiveValueDisplay = true;

            if (!fLoadingPreset)
                fDirty = true;
        }

        fValues[index] = value;
        repaint();
    }

    void uiIdle() override
    {
        if (!fHasActiveValueDisplay)
            return;

        const auto now = std::chrono::steady_clock::now();
        uint32_t activeCount = 0;
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (now < fValueDisplayUntil[i])
                ++activeCount;

        if (activeCount < fLastActiveValueDisplayCount)
            repaint();

        fLastActiveValueDisplayCount = activeCount;
        fHasActiveValueDisplay = activeCount > 0;
    }

    void onCairoDisplay(const CairoGraphicsContext& context) override
    {
        const auto now = std::chrono::steady_clock::now();
        bool autoShow[kParamCount];
        for (uint32_t i = 0; i < kParamCount; ++i)
            autoShow[i] = now < fValueDisplayUntil[i];

        ui::PaintState state;
        state.values = fValues;
        state.hoverKnob = fHoverKnob;
        state.dragKnob = fDragKnob;
        state.autoShowValue = autoShow;

        state.presetName = fCurrentName.c_str();
        state.presetIsUnsaved = fDirty;
        state.presetEditingName = fEditingName;
        state.presetEditBuffer = fEditBuffer.c_str();
        state.hoverPresetButton = fHoverPresetButton;
        state.presetDeleteEnabled = fPresetIndex >= 0;

        state.hoverSourceJack = fHoverSourceJack;
        state.hoverDestJack = fHoverDestJack;
        state.dragging = fDragJackKind != 0;
        state.dragFromSource = fDragJackKind == 1;
        state.dragIndex = fDragJackIndex;
        state.dragCurX = fDragCurX;
        state.dragCurY = fDragCurY;

        ui::paint(context.handle, fLayout, state);
    }

    // ---------------------------------------------------------------------
    // input

    bool onMouse(const MouseEvent& ev) override
    {
        const double mx = ev.pos.getX();
        const double my = ev.pos.getY();

        // right-click removes a patch cable - either the one clicked
        // directly, or (shortcut) a jack with exactly one connection. Kept
        // off the left button entirely so a stray left-click near a cable
        // (easy to trigger by accident given how much of the canvas a long
        // cable can cross) can no longer rip out a connection.
        if (ev.button == 2) // DGL::kMouseButtonRight (dgl/Base.hpp) - not in scope here, so a plain literal like the existing `button != 1` checks below
        {
            if (!ev.press)
                return false;

            int cableSrc = -1, cableDest = -1;
            if (hitTestCable(mx, my, cableSrc, cableDest))
            {
                setPatch(cableSrc, cableDest, false);
                repaint();
                return true;
            }

            int srcJack = hitTestSourceJack(mx, my);
            if (srcJack >= 0)
            {
                int only = -1, count = 0;
                for (int d = 0; d < (int)ui::kPatchNumDests; ++d)
                    if (fValues[patchParamIndex(srcJack, d)] > 0.5f) { only = d; ++count; }
                if (count == 1)
                {
                    setPatch(srcJack, only, false);
                    repaint();
                    return true;
                }
            }

            int destJack = hitTestDestJack(mx, my);
            if (destJack >= 0)
            {
                int only = -1, count = 0;
                for (int s = 0; s < (int)ui::kPatchNumSources; ++s)
                    if (fValues[patchParamIndex(s, destJack)] > 0.5f) { only = s; ++count; }
                if (count == 1)
                {
                    setPatch(only, destJack, false);
                    repaint();
                    return true;
                }
            }

            return false;
        }

        if (ev.button != 1)
            return false;

        if (ev.press)
        {
            const int presetBtn = hitTestPresetButton(mx, my);
            if (presetBtn >= 0)
            {
                switch (presetBtn)
                {
                case 0: fEditingName = false; presetStep(-1); break;
                case 1: fEditingName = false; presetStep(1);  break;
                case 2: presetSave();   break;
                case 3: fEditingName = false; presetDelete(); break;
                default: fEditingName = false; break;
                }
                repaint();
                return true;
            }

            if (hitTestPresetName(mx, my))
            {
                fEditingName = true;
                fEditBuffer = fCurrentName;
                repaint();
                return true;
            }

            if (fEditingName)
            {
                fEditingName = false;
                repaint();
            }

            // patch cables: a left-press on a jack starts a potential
            // connect-drag, completed on release over a compatible jack (see
            // onMouse's release branch). Disconnecting is right-click only
            // (see the top of onMouse) - a left click here that isn't a real
            // drag just does nothing.
            int srcJack = hitTestSourceJack(mx, my);
            if (srcJack >= 0)
            {
                fDragJackKind = 1;
                fDragJackIndex = srcJack;
                fDragCurX = mx;
                fDragCurY = my;
                repaint();
                return true;
            }
            int destJack = hitTestDestJack(mx, my);
            if (destJack >= 0)
            {
                fDragJackKind = 2;
                fDragJackIndex = destJack;
                fDragCurX = mx;
                fDragCurY = my;
                repaint();
                return true;
            }

            const int knobIdx = hitTestKnob(mx, my);
            if (knobIdx < 0)
                return false;

            const ui::Knob& k = fLayout.knobs[knobIdx];

            const auto now = std::chrono::steady_clock::now();
            const bool doubleClick = fLastClickKnob == knobIdx &&
                std::chrono::duration_cast<std::chrono::milliseconds>(now - fLastClickTime).count() < 350;
            fLastClickKnob = knobIdx;
            fLastClickTime = now;

            if (doubleClick)
            {
                const float def = getParamInfo(k.param).def;
                editParameter(k.param, true);
                setParameterValue(k.param, def);
                editParameter(k.param, false);
                fValues[k.param] = def;
                fDirty = true;
                fDragKnob = -1;
                repaint();
                return true;
            }

            fDragKnob = knobIdx;
            fDragStartY = my;
            fDragStartT = ui::paramToNormalized(k.param, fValues[k.param]);
            editParameter(k.param, true);
            return true;
        }

        if (fDragKnob >= 0)
        {
            editParameter(fLayout.knobs[fDragKnob].param, false);
            fDragKnob = -1;
            repaint();
            return true;
        }

        if (fDragJackKind != 0)
        {
            // complete the connection if released over a compatible
            // (opposite-kind) jack; a plain click that never moved just
            // cancels the drag with no effect - disconnecting is right-click
            // only (see the top of onMouse).
            if (fDragJackKind == 1)
            {
                const int destJack = hitTestDestJack(mx, my);
                if (destJack >= 0)
                    setPatch(fDragJackIndex, destJack, true);
            }
            else
            {
                const int srcJack = hitTestSourceJack(mx, my);
                if (srcJack >= 0)
                    setPatch(srcJack, fDragJackIndex, true);
            }

            fDragJackKind = 0;
            fDragJackIndex = -1;
            repaint();
            return true;
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const double mx = ev.pos.getX();
        const double my = ev.pos.getY();

        if (fDragKnob >= 0)
        {
            const ui::Knob& k = fLayout.knobs[fDragKnob];
            const double dy = fDragStartY - my;
            float t = fDragStartT + (float)(dy / 200.0);
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);

            const float value = ui::normalizedToParam(k.param, t);
            fValues[k.param] = value;
            fDirty = true;
            setParameterValue(k.param, value);
            repaint();
            return true;
        }

        if (fDragJackKind != 0)
        {
            fDragCurX = mx;
            fDragCurY = my;
            repaint();
            return true;
        }

        const int newHoverKnob = hitTestKnob(mx, my);
        const int newHoverPresetButton = hitTestPresetButton(mx, my);
        const int newHoverSourceJack = hitTestSourceJack(mx, my);
        const int newHoverDestJack = hitTestDestJack(mx, my);

        if (newHoverKnob != fHoverKnob || newHoverPresetButton != fHoverPresetButton
            || newHoverSourceJack != fHoverSourceJack || newHoverDestJack != fHoverDestJack)
        {
            fHoverKnob = newHoverKnob;
            fHoverPresetButton = newHoverPresetButton;
            fHoverSourceJack = newHoverSourceJack;
            fHoverDestJack = newHoverDestJack;
            repaint();
        }

        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const int knobIdx = hitTestKnob(ev.pos.getX(), ev.pos.getY());
        if (knobIdx < 0)
            return false;

        const ui::Knob& k = fLayout.knobs[knobIdx];
        const float t = ui::paramToNormalized(k.param, fValues[k.param]);
        const float step = 0.02f;
        float newT = t + (ev.delta.getY() > 0.0 ? step : -step);
        newT = newT < 0.0f ? 0.0f : (newT > 1.0f ? 1.0f : newT);

        const float value = ui::normalizedToParam(k.param, newT);
        editParameter(k.param, true);
        setParameterValue(k.param, value);
        editParameter(k.param, false);
        fValues[k.param] = value;
        fDirty = true;
        repaint();
        return true;
    }

    // low-level key events: only used for the few control keys that matter
    // while renaming a preset (Enter/Escape/Backspace); actual typed
    // characters come through onCharacterInput() below.
    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (!fEditingName)
            return false;
        if (!ev.press)
            return true;

        if (ev.key == kKeyEnter || ev.key == kKeyPadEnter)
        {
            fCurrentName = ui::sanitizePresetName(fEditBuffer);
            fEditingName = false;
            repaint();
            return true;
        }
        if (ev.key == kKeyEscape)
        {
            fEditingName = false;
            repaint();
            return true;
        }
        if (ev.key == kKeyBackspace || ev.key == kKeyDelete)
        {
            if (!fEditBuffer.empty())
                fEditBuffer.pop_back();
            repaint();
            return true;
        }

        return true;
    }

    bool onCharacterInput(const CharacterInputEvent& ev) override
    {
        if (!fEditingName)
            return false;

        if (ev.character >= 32 && fEditBuffer.size() < 32)
            fEditBuffer += ev.string;

        repaint();
        return true;
    }

private:
    int hitTestKnob(double x, double y) const noexcept
    {
        for (size_t i = 0; i < fLayout.knobs.size(); ++i)
        {
            const ui::Knob& k = fLayout.knobs[i];
            const double dx = x - k.cx, dy = y - k.cy;
            const double reach = k.radius + 6.0;
            if (dx * dx + dy * dy <= reach * reach)
                return (int)i;
        }
        return -1;
    }

    // ---------------------------------------------------------------------
    // patch cables

    static uint32_t patchParamIndex(int src, int dest) noexcept
    {
        return kParamPatchFirst + (uint32_t)src * (uint32_t)ui::kPatchNumDests + (uint32_t)dest;
    }

    void setPatch(int src, int dest, bool connected) noexcept
    {
        const uint32_t param = patchParamIndex(src, dest);
        const float value = connected ? 1.0f : 0.0f;
        editParameter(param, true);
        setParameterValue(param, value);
        editParameter(param, false);
        fValues[param] = value;
        fDirty = true;
    }

    int hitTestSourceJack(double x, double y) const noexcept
    {
        for (size_t i = 0; i < fLayout.sourceJacks.size(); ++i)
        {
            const ui::Jack& j = fLayout.sourceJacks[i];
            const double dx = x - j.cx, dy = y - j.cy;
            const double reach = ui::kJackRadius + 5.0;
            if (dx * dx + dy * dy <= reach * reach)
                return (int)i;
        }
        return -1;
    }

    int hitTestDestJack(double x, double y) const noexcept
    {
        for (size_t i = 0; i < fLayout.destJacks.size(); ++i)
        {
            const ui::Jack& j = fLayout.destJacks[i];
            const double dx = x - j.cx, dy = y - j.cy;
            const double reach = ui::kJackRadius + 5.0;
            if (dx * dx + dy * dy <= reach * reach)
                return (int)i;
        }
        return -1;
    }

    // primary disconnect path: hit-test against ~20 sampled points along
    // each currently-active cable's curve (same math as ui::paintCable()) -
    // unambiguous even when a destination has several connections, unlike
    // clicking a jack.
    bool hitTestCable(double x, double y, int& outSrc, int& outDest) const noexcept
    {
        static constexpr int kSamples = 20;
        static constexpr double kTolerance = 6.0;

        for (int s = 0; s < (int)ui::kPatchNumSources; ++s)
        {
            for (int d = 0; d < (int)ui::kPatchNumDests; ++d)
            {
                if (fValues[patchParamIndex(s, d)] <= 0.5f)
                    continue;

                const ui::Jack& sj = fLayout.sourceJacks[(size_t)s];
                const ui::Jack& dj = fLayout.destJacks[(size_t)d];
                for (int i = 0; i <= kSamples; ++i)
                {
                    double px, py;
                    ui::cablePointAt(sj.cx, sj.cy, dj.cx, dj.cy, (double)i / (double)kSamples, px, py);
                    const double dx = x - px, dy = y - py;
                    if (dx * dx + dy * dy <= kTolerance * kTolerance)
                    {
                        outSrc = s;
                        outDest = d;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    static bool insideButton(const ui::Button& b, double x, double y) noexcept
    {
        return x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h;
    }

    int hitTestPresetButton(double x, double y) const noexcept
    {
        const ui::PresetBarLayout& pb = fLayout.presetBar;
        if (insideButton(pb.prev, x, y)) return 0;
        if (insideButton(pb.next, x, y)) return 1;
        if (insideButton(pb.save, x, y)) return 2;
        if (insideButton(pb.del, x, y) && fPresetIndex >= 0) return 3;
        return -1;
    }

    bool hitTestPresetName(double x, double y) const noexcept
    {
        const ui::PresetBarLayout& pb = fLayout.presetBar;
        return x >= pb.nameX && x <= pb.nameX + pb.nameW && y >= pb.nameY && y <= pb.nameY + pb.nameH;
    }

    // ---------------------------------------------------------------------
    // preset CRUD - index < 0 means INIT (compiled-in defaults, no file)

    void loadPresetAt(int index) noexcept
    {
        fLoadingPreset = true;

        if (index < 0 || index >= (int)fPresetNames.size())
        {
            for (uint32_t i = 0; i < kParamCount; ++i)
                fValues[i] = getParamInfo(i).def;
            fCurrentName = "INIT";
            fPresetIndex = -1;
        }
        else
        {
            ui::loadPreset(fPresetNames[(size_t)index], fValues);
            fCurrentName = fPresetNames[(size_t)index];
            fPresetIndex = index;
        }

        for (uint32_t i = 0; i < kParamCount; ++i)
        {
            editParameter(i, true);
            setParameterValue(i, fValues[i]);
            editParameter(i, false);
        }

        fDirty = false;
        fLoadingPreset = false;
        repaint();
    }

    void presetStep(int dir) noexcept
    {
        const int total = (int)fPresetNames.size() + 1;
        int slot = fPresetIndex + 1;
        slot = ((slot + dir) % total + total) % total;
        loadPresetAt(slot - 1);
    }

    void presetSave() noexcept
    {
        std::string name = fEditingName ? ui::sanitizePresetName(fEditBuffer) : fCurrentName;
        if (name.empty() || name == "INIT")
            name = "New Preset";

        ui::savePreset(name, fValues);
        fPresetNames = ui::listPresets();
        fCurrentName = name;
        fEditingName = false;
        fDirty = false;

        fPresetIndex = -1;
        for (size_t i = 0; i < fPresetNames.size(); ++i)
        {
            if (fPresetNames[i] == name)
            {
                fPresetIndex = (int)i;
                break;
            }
        }
    }

    void presetDelete() noexcept
    {
        if (fPresetIndex < 0)
            return;

        ui::deletePreset(fCurrentName);
        fPresetNames = ui::listPresets();

        const int newIndex = fPresetIndex < (int)fPresetNames.size() ? fPresetIndex : (int)fPresetNames.size() - 1;
        loadPresetAt(newIndex);
    }

    ui::Layout fLayout;
    float fValues[kParamCount];

    std::chrono::steady_clock::time_point fValueDisplayUntil[kParamCount]{};
    bool fHasActiveValueDisplay = false;
    uint32_t fLastActiveValueDisplayCount = 0;

    int fDragKnob = -1;
    double fDragStartY = 0.0;
    float fDragStartT = 0.0f;

    int fHoverKnob = -1;
    int fLastClickKnob = -1;
    std::chrono::steady_clock::time_point fLastClickTime{};

    // patch cables: 0 = not dragging, 1 = dragging from a source jack,
    // 2 = dragging from a dest jack
    int fDragJackKind = 0;
    int fDragJackIndex = -1;
    double fDragCurX = 0.0, fDragCurY = 0.0;
    int fHoverSourceJack = -1;
    int fHoverDestJack = -1;

    std::vector<std::string> fPresetNames;
    int fPresetIndex = -1;
    std::string fCurrentName = "INIT";
    bool fDirty = false;
    bool fLoadingPreset = false;
    bool fEditingName = false;
    std::string fEditBuffer;
    int fHoverPresetButton = -1;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhellipeUI)
};

// -----------------------------------------------------------------------------------------------------------

UI* createUI()
{
    return new PhellipeUI();
}

// -----------------------------------------------------------------------------------------------------------

END_NAMESPACE_DISTRHO
