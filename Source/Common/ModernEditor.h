#pragma once
// Gemeinsame Oberfläche der modernen Onno-Suite: festes Raster, das mit dem Fenster skaliert,
// flache Bogen-Regler, Pill-Buttons und eine große Anzeige, die jedes Plugin selbst zeichnet.
// Bedienung überall gleich: ziehen (Shift = fein), Mausrad, Doppelklick = Standardwert,
// Wert anklicken = eintippen, Strg+Z/Strg+Y. Kopfzeile in jedem Plugin gleich:
// Name | Preset < Name > | A B Kopie | Rückgängig Wiederholen | MATCH | BYPASS. Hilfe erscheint beim Darüberfahren.
#include <juce_audio_processors/juce_audio_processors.h>
#include "Common/OnnoLook.h"
#include "Common/ModernProcessor.h"
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
    bool compact = false;                   // Beschriftung links, Wert rechts (kleine Regler in Leisten)
};

struct MButton
{
    juce::Rectangle<float> r;
    std::function<String()> label;
    std::function<bool()> on;
    std::function<void()> click;
    Colour c = modern::accent();
    std::function<bool()> visible;          // leer = immer sichtbar
    String help;                            // Hilfe beim Darüberfahren (leer = aus der Hilfetabelle)
    std::function<void (Graphics&, juce::Rectangle<float>, Colour)> icon;   // statt Beschriftung
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

// ---------------------------------------------------------------- Hilfetexte
// Allgemeine Erklärungen nach Beschriftung; Plugins ergänzen oder überschreiben sie in `help`
inline const std::map<String, String>& genericHelp()
{
    static const std::map<String, String> m = [] {
        std::map<String, String> h;
        auto add = [&h] (const char* k, const char* v) { h[k] = String::fromUTF8 (v); };
        add ("THRESHOLD", "Ab diesem Pegel greift die Regelung. Tiefer = mehr Wirkung.");
        add ("RATIO", "Wie stark über dem Threshold gedrückt wird. 2:1 sanft, 4:1 deutlich, ab 10:1 wie ein Limiter.");
        add ("KNEE", "Weicher Übergang am Threshold. Höher = unauffälligerer Einsatz.");
        add ("ATTACK", "Wie schnell die Regelung einsetzt. Kurz fängt Spitzen, länger lässt den Anschlag durch.");
        add ("RELEASE", "Wie schnell die Regelung wieder loslässt. Kurz = lebendig, lang = gleichmäßig; zu kurz pumpt.");
        add ("LOOKAHEAD", "Schaut kurz voraus, damit auch schnelle Spitzen sauber erwischt werden. Erhöht die Latenz.");
        add ("RMS", "Mittelt den Pegel über diese Zeit. PEAK = reagiert auf Spitzen, höher = auf Lautheit.");
        add ("DET. HPF", "Filtert Bässe aus dem Steuersignal, damit Kick und Bass nicht alles runterdrücken.");
        add ("DET. LPF", "Filtert Höhen aus dem Steuersignal, z. B. damit Becken das Gate nicht öffnen.");
        add ("MIX", "Anteil des bearbeiteten Signals. Unter 100 % bleibt das Original parallel erhalten.");
        add ("OUTPUT", "Ausgangspegel nach der Bearbeitung.");
        add ("DRIVE", "Wie stark die analoge Klangfarbe sättigt. Mehr = mehr Obertöne und Dichte.");
        add ("CLEAN", "Keine analoge Färbung, völlig transparent.");
        add ("BRIT", "Übertrager wie in britischen Konsolen: dichte Bässe, weiche Höhen.");
        add ("CONSOLE", "Saubere Konsolen-Sättigung mit etwas Glanz in den Höhen.");
        add ("TUBE", "Röhre: warme, geradzahlige Obertöne, etwas runder.");
        add ("FET", "FET-Sättigung wie ein 1176: bissig, mehr Präsenz.");
        add ("MATCH", "Lautheitsausgleich: Ausgang so laut wie Eingang. So vergleichst du A, B und Bypass fair, ohne dass lauter besser klingt.");
        add ("BYPASS", "Plugin umgehen. Mit MATCH gleich laut wie bearbeitet.");
        add ("A", "Einstellung A. Umschalten auf B merkt sich A.");
        add ("B", "Einstellung B. Umschalten auf A merkt sich B.");
        add ("A>B", "Kopiert die aktuelle Einstellung auf den anderen Platz.");
        add ("B>A", "Kopiert die aktuelle Einstellung auf den anderen Platz.");
        add ("UNDO", "Rückgängig (Strg+Z).");
        add ("REDO", "Wiederholen (Strg+Y oder Strg+Shift+Z).");
        add ("PRESET", "Preset wählen, eigenes speichern oder den Preset-Ordner öffnen.");
        add ("PREV", "Vorheriges Preset.");
        add ("NEXT", "Nächstes Preset.");
        add ("SIDECHAIN", "Der Detektor hört auf den Sidechain-Eingang statt auf die Spur selbst.");
        add ("DETEKTOR: MAIN", "Der Detektor hört auf die Spur selbst. Klicken = Sidechain-Eingang nutzen.");
        return h;
    }();
    return m;
}

// ---------------------------------------------------------------- Editor-Basis
class ModernEditor : public juce::AudioProcessorEditor, protected juce::Timer
{
public:
    ModernEditor (ModernProcessor& p, juce::AudioProcessorValueTreeState& state, const String& pluginName, float width, float height)
        : AudioProcessorEditor (p), apvts (state), name (pluginName), W (width), H (height), base (p)
    {
        setResizable (true, true);
        setResizeLimits ((int) (W * 0.5f), (int) (H * 0.5f), (int) (W * 2.4f), (int) (H * 2.4f));
        getConstrainer()->setFixedAspectRatio (W / H);
        setSize ((int) W, (int) H);
        setWantsKeyboardFocus (true);
        addHeader();
        valueEd.setJustification (juce::Justification::centred);
        valueEd.setColour (juce::TextEditor::backgroundColourId, rgb (14, 16, 21));
        valueEd.setColour (juce::TextEditor::textColourId, juce::Colours::white);
        valueEd.setColour (juce::TextEditor::outlineColourId, modern::accent());
        valueEd.setColour (juce::TextEditor::focusedOutlineColourId, modern::accent());
        valueEd.onReturnKey = [this] { commitValue(); };
        valueEd.onEscapeKey = [this] { closeValue(); };
        valueEd.onFocusLost = [this] { commitValue(); };
        addChildComponent (valueEd);
        startTimerHz (30);
    }

    // für Tests
    void tickMeters() { timerCallback(); }
    void setHot (int k) { hot = k; }
    void showTipFor (juce::Point<float> p) { mouseBase = p; tipKey = hoverKey (p); tipShown = tipKey.isNotEmpty(); }

    void paint (Graphics& g) override
    {
        s = std::max (0.25f, std::min (getWidth() / W, getHeight() / H));
        ox = std::floor ((getWidth() - W * s) * 0.5f);
        oy = std::floor ((getHeight() - H * s) * 0.5f);
        g.fillAll (modern::bg());
        g.addTransform (juce::AffineTransform::scale (s).translated (ox, oy));

        modern::header (g, name);
        info.clear();
        drawBackground (g);
        if (analogStrip) drawAnalogStrip (g);
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
            {
                const bool on = b.on && b.on(), hv = b.r.contains (mouseBase);
                drawPill (g, b.r, on, hv, b.icon ? String() : b.label(), b.c);
                if (b.icon) b.icon (g, b.r, on ? modern::bg() : (hv ? modern::text() : modern::dim()));
            }
        drawOverlay (g);
        drawInfo (g);
        drawTip (g);
    }

    // ------------------------------------------------------------ Tastatur
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (! k.getModifiers().isCommandDown()) return false;
        const int c = k.getKeyCode();
        if (c == 'Z' || c == 'z') { k.getModifiers().isShiftDown() ? base.redo() : base.undo(); repaint(); return true; }
        if (c == 'Y' || c == 'y') { base.redo(); repaint(); return true; }
        return false;
    }

    // ------------------------------------------------------------ Maus
    void mouseMove (const juce::MouseEvent& e) override
    {
        mouseBase = toBase (e.position);
        hot = hitKnob (mouseBase);
        if (display.contains (mouseBase)) displayMove (mouseBase);
        updateHover();
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        mouseBase = { -1000, -1000 };
        if (drag < 0) hot = -1;
        displayMove (mouseBase);
        updateHover();
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto b = toBase (e.position);
        tipShown = false; tipBlocked = true;
        if (valueEd.isVisible()) commitValue();
        for (auto& bt : buttons)
            if ((! bt.visible || bt.visible()) && bt.r.contains (b))
            {
                if (bt.click)
                {
                    if (! isHeaderButton (bt)) base.pushUndo();
                    bt.click();
                }
                repaint();
                return;
            }
        const int vk = hitValue (b);
        if (vk >= 0 && ! e.mods.isPopupMenu()) { openValue (vk); return; }
        hot = hitKnob (b);
        if (hot >= 0)
        {
            base.pushUndo();
            drag = hot;
            auto* p = param (knobs[(size_t) drag].id);
            dragNorm = p->getValue();
            lastY = b.y;
            p->beginChangeGesture();
            return;
        }
        if (display.contains (b)) { base.pushUndo(); dragDisplay = displayDown (b, e); }
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
            base.pushUndo();
            auto* p = param (knobs[(size_t) k].id);
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
        else if (display.contains (b)) { base.pushUndo(); displayDoubleClick (b); }
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const auto b = toBase (e.position);
        const float d = (w.isReversed ? -w.deltaY : w.deltaY);
        const int k = hitKnob (b);
        // Mausrad-Schritte kurz hintereinander zählen als ein Rückgängig-Schritt
        const auto now = juce::Time::getMillisecondCounter();
        const String key = k >= 0 ? "k" + String (k) : "d";
        if (key != wheelKey || now - wheelTime > 800) base.pushUndo();
        wheelKey = key; wheelTime = now;
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

    void timerCallback() override
    {
        tick();
        if (tipKey.isNotEmpty() && ! tipBlocked && drag < 0 && ! dragDisplay
            && juce::Time::getMillisecondCounter() - tipSince > 450)
            tipShown = true;
        repaint();
    }

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

    void addHelp (std::initializer_list<std::pair<const char*, const char*>> items)
    {
        for (auto& [k, v] : items) help[String::fromUTF8 (k)] = String::fromUTF8 (v);
    }

    // Hinweis/Live-Wert zur Anzeige: erscheint oben im Display, solange die Maus dort ist
    void headerInfo (Graphics&, const String& text) { info = text; }

    // Leiste "ANALOG" am unteren Rand: Klangfarbe wählen und Drive einstellen
    void addAnalogStrip()
    {
        analogStrip = true;
        const float y = H - 37;
        for (int m = 0; m < 5; ++m)
        {
            static const char* const lab[] = { "CLEAN", "BRIT", "CONSOLE", "TUBE", "FET" };
            buttons.push_back ({ { 100.0f + m * 74.0f, y, 68, 20 }, [m] { return String (lab[m]); },
                                 [this, m] { return (int) value ("analog") == m; }, [this, m] { setValue ("analog", (float) m); },
                                 m == 0 ? modern::dim() : modern::warm() });
        }
        MKnob k { "drive", "DRIVE", 548, H - 27, 11, modern::warm(), [] (float v) { return fmt::pct (v); } };
        k.compact = true;
        knobs.push_back (k);
    }

    void drawAnalogStrip (Graphics& g)
    {
        section (g, { 16, H - 44, W - 32, 34 });
        g.setFont (font (10)); g.setColour (modern::warm());
        ltext (g, "ANALOG", 30, H - 33, 10);
        const int m = (int) value ("analog");
        g.setFont (font (10, false)); g.setColour (modern::dim());
        static const char* const d[] = { "transparent, keine Färbung", "Übertrager: dichte Bässe, weiche Höhen",
                                         "Konsole: sauber, ungerade Obertöne, Glanz", "Röhre: warme geradzahlige Obertöne",
                                         "FET wie 1176: bissig, Präsenz" };
        g.drawFittedText (String::fromUTF8 (d[juce::jlimit (0, 4, m)]),
                          juce::Rectangle<float> (616, H - 37, W - 646, 20).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
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
        const bool valHover = hitValue (mouseBase) == i;
        if (k.compact)
        {
            line (g, cx + std::sin (a) * ri * 0.2f, cy - std::cos (a) * ri * 0.2f, cx + std::sin (a) * ri * 0.9f, cy - std::cos (a) * ri * 0.9f, 1.8f);
            g.setFont (font (9.5f)); g.setColour (modern::dim());
            rtext (g, k.label, cx - r - 8, cy - 6, 10);
            g.setFont (font (10, hv || valHover)); g.setColour (hv || valHover ? juce::Colours::white : modern::text().withAlpha (0.88f));
            ltext (g, k.format ? k.format (p->convertFrom0to1 (n)) : p->getCurrentValueAsText(), cx + r + 8, cy - 6, 10);
            if (valHover) { g.setColour (rgb (255, 255, 255, 0.25f)); g.drawRoundedRectangle (valueRect (k), 3, 1); }
            return;
        }
        line (g, cx + std::sin (a) * ri * 0.35f, cy - std::cos (a) * ri * 0.35f, cx + std::sin (a) * ri * 0.8f, cy - std::cos (a) * ri * 0.8f, 2.2f);
        g.setFont (font (r > 20 ? 9.5f : 9.0f)); g.setColour (modern::dim());
        ctext (g, k.label, cx, cy - r - 14, 10);
        g.setFont (font (r > 20 ? 10.5f : 9.5f, hv || valHover)); g.setColour (hv || valHover ? juce::Colours::white : modern::text().withAlpha (0.88f));
        const float val = p->convertFrom0to1 (n);
        ctext (g, k.format ? k.format (val) : p->getCurrentValueAsText(), cx, cy + r + 3, 11);
        if (valHover) { g.setColour (rgb (255, 255, 255, 0.25f)); g.drawRoundedRectangle (valueRect (k), 3, 1); }
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

    // Feld des Zahlenwerts unter (bzw. neben) einem Regler
    static juce::Rectangle<float> valueRect (const MKnob& k)
    {
        if (k.compact) return { k.cx + k.r + 4, k.cy - 9, 58, 18 };
        return { k.cx - 34, k.cy + k.r + 1, 68, 16 };
    }

    int hitValue (juce::Point<float> b) const
    {
        for (int i = 0; i < (int) knobs.size(); ++i)
            if (valueRect (knobs[(size_t) i]).contains (b)) return i;
        return -1;
    }

    juce::Point<float> toBase (juce::Point<float> p) const { return { (p.x - ox) / s, (p.y - oy) / s }; }

    juce::AudioProcessorValueTreeState& apvts;
    String name;
    const float W, H;
    std::vector<MKnob> knobs;
    std::vector<MButton> buttons;
    std::map<String, String> help;          // Hilfe nach Beschriftung (Regler und Buttons), ergänzt genericHelp()
    juce::Rectangle<float> display;
    juce::Point<float> mouseBase { -1000.0f, -1000.0f };
    int hot = -1, drag = -1;

private:
    // ------------------------------------------------------------ Kopfzeile
    void addHeader()
    {
        const float y = 13, h = 20;
        float x = W - 16 - 72;
        buttons.push_back ({ { x, y, 72, h }, [] { return String ("BYPASS"); }, [this] { return flag ("bypass"); },
                             [this] { toggle ("bypass"); }, modern::warm() });
        x -= 8 + 58;
        buttons.push_back ({ { x, y, 58, h }, [] { return String ("MATCH"); }, [this] { return base.matchOn.load(); },
                             [this] { base.matchOn = ! base.matchOn.load(); }, mc::green() });
        x -= 12 + 26;
        MButton redo { { x, y, 26, h }, [] { return String ("REDO"); }, [] { return false; }, [this] { base.redo(); }, modern::accent() };
        redo.icon = [this] (Graphics& g, juce::Rectangle<float> r, Colour c) { drawArrow (g, r, c.withMultipliedAlpha (base.canRedo() ? 1.0f : 0.35f), true); };
        x -= 4 + 26;
        MButton undo { { x, y, 26, h }, [] { return String ("UNDO"); }, [] { return false; }, [this] { base.undo(); }, modern::accent() };
        undo.icon = [this] (Graphics& g, juce::Rectangle<float> r, Colour c) { drawArrow (g, r, c.withMultipliedAlpha (base.canUndo() ? 1.0f : 0.35f), false); };
        buttons.push_back (undo);
        buttons.push_back (redo);
        x -= 12 + 40;
        buttons.push_back ({ { x, y, 40, h }, [this] { return String (base.slot() == 0 ? "A>B" : "B>A"); }, [] { return false; },
                             [this] { base.copyToOtherSlot(); }, modern::accent() });
        x -= 4 + 26;
        buttons.push_back ({ { x, y, 26, h }, [] { return String ("B"); }, [this] { return base.slot() == 1; },
                             [this] { base.selectSlot (1); }, modern::accent() });
        x -= 4 + 26;
        buttons.push_back ({ { x, y, 26, h }, [] { return String ("A"); }, [this] { return base.slot() == 0; },
                             [this] { base.selectSlot (0); }, modern::accent() });
        const float right = x - 12;
        const float left = std::max (170.0f, 36 + font (18).getStringWidthFloat ("ONNO " + name) + 20);
        const float wName = juce::jlimit (90.0f, 260.0f, right - left - 52);
        buttons.push_back ({ { left, y, 22, h }, [] { return String ("PREV"); }, [] { return false; }, [this] { base.stepPreset (-1); },
                             modern::accent(), {}, {}, [] (Graphics& g, juce::Rectangle<float> r, Colour c) { drawTri (g, r, c, false); } });
        presetRect = { left + 26, y, wName, h };
        buttons.push_back ({ presetRect, [this] { return base.presetName.toUpperCase(); }, [] { return false; },
                             [this] { showPresetMenu(); }, modern::accent() });
        buttons.back().help = genericHelp().at ("PRESET");
        buttons.push_back ({ { left + 30 + wName, y, 22, h }, [] { return String ("NEXT"); }, [] { return false; }, [this] { base.stepPreset (1); },
                             modern::accent(), {}, {}, [] (Graphics& g, juce::Rectangle<float> r, Colour c) { drawTri (g, r, c, true); } });
        headerButtons = (int) buttons.size();
    }

    bool isHeaderButton (const MButton& b) const { return &b - buttons.data() < headerButtons; }

    static void drawTri (Graphics& g, juce::Rectangle<float> r, Colour c, bool right)
    {
        juce::Path p;
        const float cx = r.getCentreX(), cy = r.getCentreY();
        if (right) p.addTriangle (cx - 3, cy - 5, cx - 3, cy + 5, cx + 4, cy);
        else       p.addTriangle (cx + 3, cy - 5, cx + 3, cy + 5, cx - 4, cy);
        g.setColour (c); g.fillPath (p);
    }

    // gebogener Pfeil für Rückgängig (nach links) bzw. Wiederholen (nach rechts)
    static void drawArrow (Graphics& g, juce::Rectangle<float> r, Colour c, bool redo)
    {
        const float pi = juce::MathConstants<float>::pi;
        const float cx = r.getCentreX(), cy = r.getCentreY() + 1.0f, rad = 5.0f, d = redo ? 1.0f : -1.0f;
        juce::Path p;
        p.addCentredArc (cx, cy, rad, rad, 0, -d * 0.55f * pi, d * 0.5f * pi, true);   // über oben auf die Gegenseite
        g.setColour (c); g.strokePath (p, juce::PathStrokeType (1.6f));
        const float ex = cx + d * rad, ey = cy;
        juce::Path h;
        h.addTriangle (ex - 3.5f, ey - 0.5f, ex + 3.5f, ey - 0.5f, ex, ey + 4.0f);
        g.fillPath (h);
    }

    void showPresetMenu()
    {
        const auto names = base.presetNames();
        const int nf = base.numFactoryPresets(), cur = base.presetIndex();
        juce::PopupMenu m;
        m.addItem (1, "Init", true, cur == 0);
        if (nf > 0)
        {
            m.addSectionHeader ("Werk");
            for (int i = 1; i <= nf; ++i) m.addItem (1 + i, names[i], true, cur == i);
        }
        if (names.size() > nf + 1)
        {
            m.addSectionHeader ("Eigene");
            for (int i = nf + 1; i < names.size(); ++i) m.addItem (1 + i, names[i], true, cur == i);
        }
        m.addSeparator();
        m.addItem (9001, String::fromUTF8 ("Speichern unter …"));
        m.addItem (9002, String::fromUTF8 ("Preset-Ordner öffnen"));
        juce::Component::SafePointer<ModernEditor> safe (this);
        const auto area = localAreaToGlobal (juce::Rectangle<float> (presetRect.getX() * s + ox, presetRect.getY() * s + oy,
                                                                     presetRect.getWidth() * s, presetRect.getHeight() * s).toNearestInt());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (area).withParentComponent (this),
                         [safe] (int r)
                         {
                             if (safe == nullptr || r == 0) return;
                             auto& b = safe->base;
                             if (r == 9001) safe->askPresetName();
                             else if (r == 9002) { b.userPresetDir().createDirectory(); b.userPresetDir().startAsProcess(); }
                             else b.loadPreset (r - 1);
                             safe->repaint();
                         });
    }

    void askPresetName()
    {
        auto* w = new juce::AlertWindow ("Preset speichern", "Name:", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", base.presetName == "Init" ? String() : base.presetName);
        w->addButton ("Speichern", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Abbrechen", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<ModernEditor> safe (this);
        juce::Component::SafePointer<juce::AlertWindow> sw (w);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, sw] (int r)
        {
            if (r == 1 && safe != nullptr && sw != nullptr) safe->base.saveUserPreset (sw->getTextEditorContents ("name"));
            if (safe != nullptr) safe->repaint();
        }), true);
    }

    // ------------------------------------------------------------ Wert eintippen
    void openValue (int k)
    {
        valueKnob = k;
        auto* p = param (knobs[(size_t) k].id);
        const auto r = valueRect (knobs[(size_t) k]).expanded (6, 3);
        valueEd.setBounds (juce::Rectangle<float> (r.getX() * s + ox, r.getY() * s + oy, r.getWidth() * s, r.getHeight() * s).toNearestInt());
        valueEd.setFont (font (11 * s, false));
        const auto& kn = knobs[(size_t) k];
        valueEd.setText (kn.format ? kn.format (p->convertFrom0to1 (p->getValue())) : p->getCurrentValueAsText(), false);
        valueEd.setVisible (true);
        valueEd.grabKeyboardFocus();
        valueEd.selectAll();
    }

    void closeValue() { valueKnob = -1; valueEd.setVisible (false); }

    void commitValue()
    {
        if (valueKnob < 0) { valueEd.setVisible (false); return; }
        auto* p = param (knobs[(size_t) valueKnob].id);
        const String text = valueEd.getText();
        closeValue();
        if (p == nullptr || text.trim().isEmpty()) return;
        base.pushUndo();
        p->beginChangeGesture();
        p->setValueNotifyingHost (parseValue (*p, text));
        p->endChangeGesture();
        repaint();
    }

public:
    // "2k", "-3 dB", "250 ms", "1.5 s", "L30", "inf", "aus" -> normierter Wert
    static float parseValue (juce::RangedAudioParameter& p, const String& raw)
    {
        String t = raw.trim().toLowerCase().replaceCharacter (',', '.');
        if (dynamic_cast<juce::AudioParameterChoice*> (&p) != nullptr || dynamic_cast<juce::AudioParameterBool*> (&p) != nullptr)
            return p.getValueForText (raw.trim());
        const auto range = p.getNormalisableRange();
        if (t.startsWith ("inf")) return 1.0f;
        if (t == "aus" || t == "off") return p.getDefaultValue();
        float v = t.getFloatValue();
        if (t.startsWithChar ('l')) v = -t.substring (1).getFloatValue();
        else if (t.startsWithChar ('r')) v = t.substring (1).getFloatValue();
        else if (t == "c") v = 0;
        if (t.containsChar ('k')) v *= 1000.0f;
        if (p.getLabel() == "ms" && t.endsWithChar ('s') && ! t.endsWith ("ms")) v *= 1000.0f;
        return range.convertTo0to1 (juce::jlimit (range.start, range.end, v));
    }

private:

    // ------------------------------------------------------------ Hilfe beim Darüberfahren
    String hoverKey (juce::Point<float> b) const
    {
        for (int i = 0; i < (int) buttons.size(); ++i)
        {
            const auto& bt = buttons[(size_t) i];
            if ((! bt.visible || bt.visible()) && bt.r.contains (b)) return "b" + String (i);
        }
        const int k = hitKnob (b);
        if (k >= 0) return "k" + String (k);
        const int v = hitValue (b);
        if (v >= 0) return "v" + String (v);
        return {};
    }

    void updateHover()
    {
        const auto key = hoverKey (mouseBase);
        if (key != tipKey) { tipKey = key; tipSince = juce::Time::getMillisecondCounter(); tipShown = false; tipBlocked = false; }
    }

    String helpFor (const String& label) const
    {
        if (auto it = help.find (label); it != help.end()) return it->second;
        if (auto it = genericHelp().find (label); it != genericHelp().end()) return it->second;
        return {};
    }

    void drawTip (Graphics& g)
    {
        if (! tipShown || tipKey.isEmpty()) return;
        const int idx = tipKey.substring (1).getIntValue();
        String title, body, hint;
        juce::Rectangle<float> anchor;
        if (tipKey[0] == 'b')
        {
            if (! juce::isPositiveAndBelow (idx, (int) buttons.size())) return;
            const auto& bt = buttons[(size_t) idx];
            const auto lab = bt.label();
            body = bt.help.isNotEmpty() ? bt.help : helpFor (lab);
            anchor = bt.r;
        }
        else
        {
            if (! juce::isPositiveAndBelow (idx, (int) knobs.size())) return;
            const auto& k = knobs[(size_t) idx];
            title = k.label;
            body = helpFor (k.label);
            hint = String::fromUTF8 (tipKey[0] == 'v' ? "Klicken = Wert eintippen (z. B. 2k, -3, 250 ms)"
                                                      : "Ziehen, Shift = fein, Doppelklick = Standard, Wert anklicken = eintippen");
            anchor = k.compact ? juce::Rectangle<float> (k.cx - k.r, k.cy - k.r, 2 * k.r, 2 * k.r)
                               : juce::Rectangle<float> (k.cx - k.r, k.cy - k.r - 16, 2 * k.r, 2 * k.r + 36);
        }
        if (body.isEmpty() && hint.isEmpty()) return;
        juce::AttributedString as;
        as.setWordWrap (juce::AttributedString::byWord);
        if (title.isNotEmpty()) as.append (title + "\n", font (10.5f), juce::Colours::white);
        if (body.isNotEmpty()) as.append (body, font (10.5f, false), modern::text());
        if (hint.isNotEmpty()) as.append ((body.isNotEmpty() ? "\n" : "") + hint, font (9.5f, false), modern::dim());
        const float tw = 250;
        juce::TextLayout tl;
        tl.createLayout (as, tw);
        const float bw = std::min (tw, tl.getWidth()) + 20, bh = tl.getHeight() + 14;
        float x = juce::jlimit (8.0f, W - bw - 8, anchor.getCentreX() - bw * 0.5f);
        float y = anchor.getBottom() + 6;
        if (y + bh > H - 6) y = anchor.getY() - bh - 6;
        y = juce::jlimit (6.0f, H - bh - 6, y);
        const juce::Rectangle<float> box (x, y, bw, bh);
        g.setColour (juce::Colours::black.withAlpha (0.35f)); g.fillRoundedRectangle (box.translated (0, 2), 7);
        g.setColour (rgb (44, 49, 60)); g.fillRoundedRectangle (box, 7);
        g.setColour (rgb (255, 255, 255, 0.10f)); g.drawRoundedRectangle (box, 7, 1);
        tl.draw (g, box.reduced (10, 7));
    }

    // Live-Wert oder Bedienhinweis der Anzeige, oben mittig im Display
    void drawInfo (Graphics& g)
    {
        if (info.isEmpty() || display.isEmpty()) return;
        if (! display.contains (mouseBase) && ! dragDisplay) return;
        g.setFont (font (10.5f, false));
        const float tw = g.getCurrentFont().getStringWidthFloat (info) + 20;
        const juce::Rectangle<float> r (display.getCentreX() - tw * 0.5f, display.getY() + 8, tw, 20);
        g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillRoundedRectangle (r, 6);
        g.setColour (modern::text().withAlpha (0.9f));
        g.drawText (info, r, juce::Justification::centred, false);
    }

    ModernProcessor& base;
    juce::TextEditor valueEd;
    int valueKnob = -1, headerButtons = 0;
    juce::Rectangle<float> presetRect;
    String info, tipKey, wheelKey;
    juce::uint32 tipSince = 0, wheelTime = 0;
    bool tipShown = false, tipBlocked = false;
    float s = 1.0f, ox = 0.0f, oy = 0.0f;
    float dragNorm = 0.0f, lastY = 0.0f;
    bool dragDisplay = false;
    bool analogStrip = false;

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
