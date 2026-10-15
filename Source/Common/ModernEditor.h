#pragma once
// Gemeinsame Oberfläche der modernen Onno-Suite: festes Raster, das mit dem Fenster skaliert,
// flache Bogen-Regler, Pill-Buttons und eine große Anzeige, die jedes Plugin selbst zeichnet.
// Bedienung überall gleich: ziehen (Shift = fein), Mausrad, Doppelklick = Standardwert.
#include <juce_audio_processors/juce_audio_processors.h>
#include "Common/OnnoLook.h"
#include <functional>

namespace onno
{
namespace mc
{
    inline Colour btn()      { return rgb (40, 45, 55); }
    inline Colour btnHover() { return rgb (56, 62, 75); }
    inline Colour track()    { return rgb (46, 51, 62); }
    inline Colour screen()   { return rgb (14, 16, 21); }
    inline Colour grid()     { return rgb (255, 255, 255, 0.06f); }
    inline Colour gridText() { return rgb (132, 140, 156, 0.75f); }
    inline Colour red()      { return rgb (255, 96, 90); }
    inline Colour blue()     { return rgb (96, 146, 255); }
    inline Colour amber()    { return rgb (255, 176, 72); }
    inline Colour green()    { return rgb (96, 214, 136); }
    inline Colour purple()   { return rgb (178, 128, 255); }
    inline Colour pink()     { return rgb (255, 112, 176); }

    // Farben für Bänder/Taps
    inline Colour band (int i)
    {
        static const Colour c[] = { rgb (255, 96, 90), rgb (255, 152, 64), rgb (240, 210, 80), rgb (96, 214, 136),
                                    rgb (58, 209, 197), rgb (96, 146, 255), rgb (178, 128, 255), rgb (255, 112, 176) };
        return c[(i % 8 + 8) % 8];
    }
}

// ---------------------------------------------------------------- Formatierung
namespace fmt
{
    inline String db (float v, int dec = 1) { return (v > 0.04f ? "+" : "") + String (v, dec) + " dB"; }
    inline String hz (float v)
    {
        if (v >= 1000.0f) return String (v / 1000.0f, v < 10000.0f ? 2 : 1) + " kHz";
        return String (v, v < 100.0f ? 1 : 0) + " Hz";
    }
    inline String ms (float v)
    {
        if (v >= 1000.0f) return String (v / 1000.0f, 2) + " s";
        return String (v, v < 10.0f ? 2 : (v < 100.0f ? 1 : 0)) + " ms";
    }
    inline String pct (float v) { return String (juce::roundToInt (v)) + " %"; }
    inline String ratio (float v) { return v >= 99.0f ? String ("inf : 1") : String (v, v < 10.0f ? 1 : 0) + " : 1"; }
    inline String pan (float v)
    {
        const int p = juce::roundToInt (v);
        return p == 0 ? String ("C") : (p < 0 ? "L" + String (-p) : "R" + String (p));
    }
}

// ---------------------------------------------------------------- Achsen
// Frequenz 20 Hz..20 kHz logarithmisch über ein Rechteck
inline float freqToX (float f, juce::Rectangle<float> r, float f0 = 20.0f, float f1 = 20000.0f)
{
    return r.getX() + r.getWidth() * std::log (std::max (f, 1.0f) / f0) / std::log (f1 / f0);
}

inline float xToFreq (float x, juce::Rectangle<float> r, float f0 = 20.0f, float f1 = 20000.0f)
{
    return f0 * std::exp ((x - r.getX()) / r.getWidth() * std::log (f1 / f0));
}

inline void drawFreqGrid (Graphics& g, juce::Rectangle<float> r, bool labels = true)
{
    static const float lines[] = { 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000,
                                    2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000 };
    for (float f : lines)
    {
        const bool major = f == 100 || f == 1000 || f == 10000;
        g.setColour (major ? rgb (255, 255, 255, 0.11f) : mc::grid());
        g.fillRect (freqToX (f, r), r.getY(), 1.0f, r.getHeight());
    }
    if (! labels) return;
    g.setFont (font (9, false)); g.setColour (mc::gridText());
    static const std::pair<float, const char*> lab[] = { { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" },
                                                         { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" } };
    for (auto& [f, s] : lab) ltext (g, s, freqToX (f, r) + 3, r.getBottom() - 13, 9);
}

// ---------------------------------------------------------------- Bausteine
struct MKnob
{
    String id, label;
    float cx = 0, cy = 0, r = 22;
    Colour c = modern::accent();
    std::function<String (float)> format;   // leer = Text des Parameters
    bool bipolar = false;                   // Bogen von der Mitte aus
};

struct MButton
{
    juce::Rectangle<float> r;
    std::function<String()> label;
    std::function<bool()> on;
    std::function<void()> click;
    Colour c = modern::accent();
    std::function<bool()> visible;          // leer = immer sichtbar
};

inline void drawPill (Graphics& g, juce::Rectangle<float> r, bool on, bool hover, const String& label, Colour c)
{
    g.setColour (on ? c : (hover ? mc::btnHover() : mc::btn()));
    g.fillRoundedRectangle (r, std::min (r.getHeight() * 0.5f, 9.0f));
    g.setColour (on ? modern::bg() : (hover ? modern::text() : modern::dim()));
    float fh = std::min (10.0f, r.getHeight() * 0.55f);
    while (fh > 6.5f && font (fh).getStringWidthFloat (label) > r.getWidth() - 8) fh -= 0.5f;
    g.setFont (font (fh));
    g.drawText (label, r, juce::Justification::centred, false);
}

// ---------------------------------------------------------------- Editor-Basis
class ModernEditor : public juce::AudioProcessorEditor, protected juce::Timer
{
public:
    ModernEditor (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& state, const String& pluginName, float width, float height)
        : AudioProcessorEditor (p), apvts (state), name (pluginName), W (width), H (height)
    {
        setResizable (true, true);
        setResizeLimits ((int) (W * 0.5f), (int) (H * 0.5f), (int) (W * 2.4f), (int) (H * 2.4f));
        getConstrainer()->setFixedAspectRatio (W / H);
        setSize ((int) W, (int) H);
        startTimerHz (30);
    }

    // für Tests
    void tickMeters() { timerCallback(); }
    void setHot (int k) { hot = k; }

    void paint (Graphics& g) override
    {
        s = std::max (0.25f, std::min (getWidth() / W, getHeight() / H));
        ox = std::floor ((getWidth() - W * s) * 0.5f);
        oy = std::floor ((getHeight() - H * s) * 0.5f);
        g.fillAll (modern::bg());
        g.addTransform (juce::AffineTransform::scale (s).translated (ox, oy));

        modern::header (g, name);
        drawBackground (g);
        if (! display.isEmpty())
        {
            g.setColour (mc::screen()); g.fillRoundedRectangle (display, 8);
            g.saveState();
            juce::Path clip; clip.addRoundedRectangle (display, 8);
            g.reduceClipRegion (clip);
            drawDisplay (g, display);
            g.restoreState();
            g.setColour (rgb (255, 255, 255, 0.06f)); g.drawRoundedRectangle (display, 8, 1);
        }
        for (int i = 0; i < (int) knobs.size(); ++i) drawKnob (g, i);
        for (auto& b : buttons)
            if (! b.visible || b.visible())
                drawPill (g, b.r, b.on && b.on(), b.r.contains (mouseBase), b.label(), b.c);
        drawOverlay (g);
    }

    // ------------------------------------------------------------ Maus
    void mouseMove (const juce::MouseEvent& e) override
    {
        mouseBase = toBase (e.position);
        hot = hitKnob (mouseBase);
        if (display.contains (mouseBase)) displayMove (mouseBase);
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        mouseBase = { -1000, -1000 };
        if (drag < 0) hot = -1;
        displayMove (mouseBase);
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto b = toBase (e.position);
        for (auto& bt : buttons)
            if ((! bt.visible || bt.visible()) && bt.r.contains (b)) { if (bt.click) bt.click(); repaint(); return; }
        hot = hitKnob (b);
        if (hot >= 0)
        {
            drag = hot;
            auto* p = param (knobs[(size_t) drag].id);
            dragNorm = p->getValue();
            lastY = b.y;
            p->beginChangeGesture();
            return;
        }
        if (display.contains (b)) dragDisplay = displayDown (b, e);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto b = toBase (e.position);
        mouseBase = b;
        if (drag >= 0)
        {
            dragNorm = juce::jlimit (0.0f, 1.0f, dragNorm + (lastY - b.y) / (e.mods.isShiftDown() ? 900.0f : 200.0f));
            lastY = b.y;
            param (knobs[(size_t) drag].id)->setValueNotifyingHost (dragNorm);
        }
        else if (dragDisplay) displayDrag (b, e);
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (drag >= 0 && drag < (int) knobs.size()) param (knobs[(size_t) drag].id)->endChangeGesture();
        drag = -1;
        if (dragDisplay) displayUp (toBase (e.position));
        dragDisplay = false;
        hot = hitKnob (toBase (e.position));
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto b = toBase (e.position);
        const int k = hitKnob (b);
        if (k >= 0)
        {
            auto* p = param (knobs[(size_t) k].id);
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
        else if (display.contains (b)) displayDoubleClick (b);
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const auto b = toBase (e.position);
        const float d = (w.isReversed ? -w.deltaY : w.deltaY);
        const int k = hitKnob (b);
        if (k >= 0)
        {
            auto* p = param (knobs[(size_t) k].id);
            p->beginChangeGesture();
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->getValue() + d * (e.mods.isShiftDown() ? 0.04f : 0.2f)));
            p->endChangeGesture();
        }
        else if (display.contains (b)) displayWheel (b, d, e);
        repaint();
    }

protected:
    // ------------------------------------------------------------ für die Plugins
    virtual void drawBackground (Graphics&) {}
    virtual void drawDisplay (Graphics&, juce::Rectangle<float>) {}
    virtual void drawOverlay (Graphics&) {}
    virtual void displayMove (juce::Point<float>) {}
    virtual bool displayDown (juce::Point<float>, const juce::MouseEvent&) { return false; }
    virtual void displayDrag (juce::Point<float>, const juce::MouseEvent&) {}
    virtual void displayUp (juce::Point<float>) {}
    virtual void displayDoubleClick (juce::Point<float>) {}
    virtual void displayWheel (juce::Point<float>, float, const juce::MouseEvent&) {}
    virtual void tick() {}

    void timerCallback() override { tick(); repaint(); }

    juce::RangedAudioParameter* param (const String& id) const { return apvts.getParameter (id); }
    float value (const String& id) const { return apvts.getRawParameterValue (id)->load(); }
    bool flag (const String& id) const { return value (id) > 0.5f; }

    void setValue (const String& id, float v)
    {
        auto* p = param (id);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }

    void toggle (const String& id) { setValue (id, flag (id) ? 0.0f : 1.0f); }

    // Ziehen in der Anzeige: Geste öffnen, Werte setzen, Geste schließen
    void beginEdit (const String& id) { param (id)->beginChangeGesture(); }
    void edit (const String& id, float v) { auto* p = param (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); }
    void endEdit (const String& id) { param (id)->endChangeGesture(); }

    void addBypass()
    {
        buttons.push_back ({ { W - 96, 13, 80, 20 }, [] { return String ("BYPASS"); },
                             [this] { return flag ("bypass"); }, [this] { toggle ("bypass"); }, modern::warm() });
    }

    // Tooltip-Zeile oben in der Mitte
    void headerInfo (Graphics& g, const String& text)
    {
        g.setFont (font (11, false)); g.setColour (modern::dim());
        ctext (g, text, W * 0.5f, 17, 11);
    }

    void section (Graphics& g, juce::Rectangle<float> r, const String& title = {})
    {
        modern::panelRect (g, r.getX(), r.getY(), r.getWidth(), r.getHeight());
        if (title.isNotEmpty())
        {
            g.setFont (font (9)); g.setColour (modern::dim().withAlpha (0.8f));
            ltext (g, title, r.getX() + 12, r.getY() + 8, 9);
        }
    }

    void drawKnob (Graphics& g, int i)
    {
        const auto& k = knobs[(size_t) i];
        auto* p = param (k.id);
        if (p == nullptr) return;
        const float pi = juce::MathConstants<float>::pi, a0 = -0.75f * pi;
        const float cx = k.cx, cy = k.cy, r = k.r;
        const float n = juce::jlimit (0.0f, 1.0f, p->getValue());
        const float a = a0 + n * 1.5f * pi;
        const bool hv = (drag >= 0 ? drag : hot) == i;
        const float w = r > 20 ? 5.0f : 4.0f;
        g.setColour (mc::track()); arc (g, cx, cy, r - w * 0.5f, a0, -a0, w);
        if (k.bipolar) { g.setColour (k.c); arc (g, cx, cy, r - w * 0.5f, std::min (0.0f, a), std::max (0.0f, a), w); }
        else if (n > 0.002f) { g.setColour (k.c); arc (g, cx, cy, r - w * 0.5f, a0, a, w); }
        const float ri = r - w - 2;
        g.setColour (rgb (24, 27, 34)); g.fillEllipse (cx - ri, cy - ri, 2 * ri, 2 * ri);
        g.setColour (hv ? modern::dim() : rgb (56, 62, 75)); g.drawEllipse (cx - ri, cy - ri, 2 * ri, 2 * ri, 1.0f);
        g.setColour (k.c);
        line (g, cx + std::sin (a) * ri * 0.35f, cy - std::cos (a) * ri * 0.35f, cx + std::sin (a) * ri * 0.8f, cy - std::cos (a) * ri * 0.8f, 2.2f);
        g.setFont (font (r > 20 ? 9.5f : 9.0f)); g.setColour (modern::dim());
        ctext (g, k.label, cx, cy - r - 14, 10);
        g.setFont (font (r > 20 ? 10.5f : 9.5f, hv)); g.setColour (hv ? juce::Colours::white : modern::text().withAlpha (0.88f));
        const float val = p->convertFrom0to1 (n);
        ctext (g, k.format ? k.format (val) : p->getCurrentValueAsText(), cx, cy + r + 3, 11);
    }

    int hitKnob (juce::Point<float> b) const
    {
        for (int i = 0; i < (int) knobs.size(); ++i)
        {
            const auto& k = knobs[(size_t) i];
            const float dx = b.x - k.cx, dy = b.y - k.cy, rr = k.r + 6;
            if (dx * dx + dy * dy < rr * rr) return i;
        }
        return -1;
    }

    juce::Point<float> toBase (juce::Point<float> p) const { return { (p.x - ox) / s, (p.y - oy) / s }; }

    juce::AudioProcessorValueTreeState& apvts;
    String name;
    const float W, H;
    std::vector<MKnob> knobs;
    std::vector<MButton> buttons;
    juce::Rectangle<float> display;
    juce::Point<float> mouseBase { -1000.0f, -1000.0f };
    int hot = -1, drag = -1;

private:
    float s = 1.0f, ox = 0.0f, oy = 0.0f;
    float dragNorm = 0.0f, lastY = 0.0f;
    bool dragDisplay = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModernEditor)
};

// Pegel-Balken (vertikal), -60..+6 dB
inline void drawMeter (Graphics& g, juce::Rectangle<float> r, float db, Colour c, bool fromTop = false, float lo = -60.0f, float hi = 6.0f)
{
    g.setColour (rgb (24, 27, 34)); g.fillRoundedRectangle (r, 2);
    const float n = juce::jlimit (0.0f, 1.0f, (db - lo) / (hi - lo));
    if (fromTop)
    {
        const float h = r.getHeight() * juce::jlimit (0.0f, 1.0f, -db / -lo);
        g.setColour (c); g.fillRoundedRectangle (r.getX(), r.getY(), r.getWidth(), h, 2);
    }
    else
    {
        const float h = r.getHeight() * n;
        g.setColour (db > 0 ? mc::red() : c); g.fillRoundedRectangle (r.getX(), r.getBottom() - h, r.getWidth(), h, 2);
    }
}
} // namespace onno
