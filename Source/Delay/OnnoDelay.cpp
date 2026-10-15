#include "OnnoDelay.h"

using namespace onno;
using juce::String;

const juce::StringArray DelayProcessor::divNames { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2", "1/2D", "1/1" };
const float DelayProcessor::divBeats[] = { 0.125f, 1.0f / 6, 0.25f, 0.375f, 1.0f / 3, 0.5f, 0.75f, 2.0f / 3, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };

juce::AudioProcessorValueTreeState::ParameterLayout DelayProcessor::layout()
{
    prm::Layout p;
    const float times[] = { 250, 375, 500 };
    const int divs[] = { 8, 6, 10 };
    const float pans[] = { 0, -60, 60 };
    for (int t = 0; t < numTaps; ++t)
    {
        const String n = "Tap " + String (t + 1) + " ";
        prm::bl (p, id (t, "on"), n + "On", t == 0);
        prm::bl (p, id (t, "sync"), n + "Sync", true);
        prm::lg (p, id (t, "time"), n + "Time", 1, maxMs, times[t], "ms");
        prm::ch (p, id (t, "div"), n + "Division", divNames, divs[t]);
        prm::lin (p, id (t, "fb"), n + "Feedback", 0, 98, t == 0 ? 35.0f : 20.0f, "%");
        prm::lin (p, id (t, "level"), n + "Level", -60, 6, t == 0 ? 0.0f : -6.0f, "dB");
        prm::lin (p, id (t, "pan"), n + "Pan", -100, 100, pans[t]);
        prm::lg (p, id (t, "hp"), n + "Low Cut", 20, 2000, 20, "Hz");
        prm::lg (p, id (t, "lp"), n + "High Cut", 500, 20000, 20000, "Hz");
        prm::bl (p, id (t, "pp"), n + "Ping-Pong", false);
    }
    prm::lin (p, "dry", "Dry", -60, 6, 0, "dB");
    prm::lin (p, "wet", "Wet", -60, 6, -6, "dB");
    prm::lin (p, "duck", "Ducking", 0, 100, 0, "%");
    Analog::addParams (p);
    prm::bl (p, "bypass", "Bypass", false);
    return prm::done (p);
}

DelayProcessor::DelayProcessor() : ModernProcessor ("Onno Delay", "OnnoDelay", stereoBuses(), layout(), 12.0)
{
    static const char* const names[] = { "on", "sync", "time", "div", "fb", "level", "pan", "hp", "lp", "pp" };
    for (int t = 0; t < numTaps; ++t)
        for (int k = 0; k < numP; ++k) taps[(size_t) t].p[(size_t) k] = raw (id (t, names[k]));
    pDry = raw ("dry"); pWet = raw ("wet"); pDuck = raw ("duck"); pBypass = raw ("bypass");
    for (auto& t : taps) t.analog.attach (*this);
}

float DelayProcessor::tapMs (int t, double tempo) const
{
    const auto& p = taps[(size_t) t].p;
    if (p[pSync]->load() > 0.5f)
    {
        const int d = juce::jlimit (0, divNames.size() - 1, (int) p[pDiv]->load());
        return (float) std::min ((double) maxMs, divBeats[d] * 60000.0 / std::max (20.0, tempo));
    }
    return p[pTime]->load();
}

void DelayProcessor::prepare (double sampleRate, int)
{
    sr = sampleRate;
    for (auto& t : taps)
    {
        for (auto& b : t.buf) b.assign ((size_t) (maxMs * 0.001 * sr) + 8, 0.0f);
        t.w = 0; t.cur = -1; t.hpF = t.lpF = -1;
        t.hp.reset(); t.lp.reset();
        t.analog.prepare (sr);
    }
    duckEnv = 0; duckS = 1;
    dryS.reset (sr, 0.03); dryS.setCurrentAndTargetValue (pDry->load() <= -59.9f ? 0.0f : dsp::db2a (pDry->load()));
    wetS.reset (sr, 0.03); wetS.setCurrentAndTargetValue (pWet->load() <= -59.9f ? 0.0f : dsp::db2a (pWet->load()));
}

void DelayProcessor::process (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buf.getNumSamples();
    const int nch = std::min (2, getMainBusNumInputChannels());
    if (nch == 0) return;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) bpm.store ((float) *b);
    const double tempo = bpm.load();

    float* L = buf.getWritePointer (0);
    float* R = buf.getWritePointer (nch > 1 ? 1 : 0);
    if (pBypass->load() > 0.5f) return;

    dryS.setTargetValue (pDry->load() <= -59.9f ? 0.0f : dsp::db2a (pDry->load()));
    wetS.setTargetValue (pWet->load() <= -59.9f ? 0.0f : dsp::db2a (pWet->load()));
    const float duck = pDuck->load() * 0.01f;
    const float dAtt = dsp::coef (5.0f, sr), dRel = dsp::coef (250.0f, sr), glide = 1.0f - dsp::coef (60.0f, sr);

    struct TapNow { bool on, pp; float target, fb, gl, gr; };
    std::array<TapNow, numTaps> tn;
    for (int t = 0; t < numTaps; ++t)
    {
        auto& tp = taps[(size_t) t];
        auto& p = tp.p;
        auto& x = tn[(size_t) t];
        x.on = p[pOn]->load() > 0.5f;
        x.pp = p[pPp]->load() > 0.5f;
        x.target = juce::jlimit (1.0f, (float) tp.buf[0].size() - 4.0f, tapMs (t, tempo) * 0.001f * (float) sr);
        if (tp.cur < 0) tp.cur = x.target;
        x.fb = p[pFb]->load() * 0.01f;
        const float lv = p[pLevel]->load() <= -59.9f ? 0.0f : dsp::db2a (p[pLevel]->load());
        const float pan = p[pPan]->load() * 0.01f;
        x.gl = lv * (pan <= 0 ? 1.0f : 1.0f - pan);
        x.gr = lv * (pan >= 0 ? 1.0f : 1.0f + pan);
        const float hf = p[pHp]->load(), lf = p[pLp]->load();
        if (hf != tp.hpF) { tp.hpF = hf; tp.hp.highPass (sr, std::min ((double) hf, sr * 0.45), 0.7071); }
        if (lf != tp.lpF) { tp.lpF = lf; if (lf < 19900.0f && lf < sr * 0.45) tp.lp.lowPass (sr, lf, 0.7071); else tp.lp.identity(); }
        tp.analog.beginBlock();
        if (! x.on && tp.wasOn) { for (auto& b : tp.buf) std::fill (b.begin(), b.end(), 0.0f); tp.hp.reset(); tp.lp.reset(); }
        tp.wasOn = x.on;
    }

    float outPk = 0, duckMin = 1;
    for (int i = 0; i < n; ++i)
    {
        const float xl = L[i], xr = R[i];
        float wl = 0, wr = 0;
        for (int t = 0; t < numTaps; ++t)
        {
            const auto& x = tn[(size_t) t];
            if (! x.on) continue;
            auto& tp = taps[(size_t) t];
            tp.cur += (x.target - tp.cur) * glide;
            const int len = (int) tp.buf[0].size();
            const int di = (int) tp.cur; const float fr = tp.cur - (float) di;
            int i1 = tp.w - di; if (i1 < 0) i1 += len;
            int i0 = i1 - 1; if (i0 < 0) i0 += len;
            float yl = tp.buf[0][(size_t) i1] + fr * (tp.buf[0][(size_t) i0] - tp.buf[0][(size_t) i1]);
            float yr = tp.buf[1][(size_t) i1] + fr * (tp.buf[1][(size_t) i0] - tp.buf[1][(size_t) i1]);
            if (tp.hpF > 20.5f) { yl = tp.hp.process (yl, 0); yr = tp.hp.process (yr, 1); }
            yl = tp.lp.process (yl, 0); yr = tp.lp.process (yr, 1);
            if (tp.analog.active())
            {
                // wie ein Bandecho: jede Wiederholung läuft erneut durch die Sättigung
                tp.analog.advance();
                yl = tp.analog.tick (yl, 0, false); yr = tp.analog.tick (yr, 1, false);
            }
            if (x.pp)
            {
                tp.buf[0][(size_t) tp.w] = 0.5f * (xl + xr) + x.fb * yr;
                tp.buf[1][(size_t) tp.w] = x.fb * yl;
            }
            else
            {
                tp.buf[0][(size_t) tp.w] = xl + x.fb * yl;
                tp.buf[1][(size_t) tp.w] = xr + x.fb * yr;
            }
            if (++tp.w >= len) tp.w = 0;
            wl += yl * x.gl; wr += yr * x.gr;
        }
        // Ducking: Echo leiser, solange das Signal spielt
        const float in = std::max (std::abs (xl), std::abs (xr));
        duckEnv = in > duckEnv ? dAtt * duckEnv + (1 - dAtt) * in : dRel * duckEnv + (1 - dRel) * in;
        const float target = 1.0f - duck * juce::jlimit (0.0f, 1.0f, (dsp::a2db (duckEnv) + 42.0f) / 30.0f);
        duckS += (target - duckS) * 0.002f;
        duckMin = std::min (duckMin, duckS);
        const float dg = dryS.getNextValue(), wg = wetS.getNextValue() * duckS;
        const float ol = xl * dg + wl * wg, orr = xr * dg + wr * wg;
        L[i] = ol;
        if (nch > 1) R[i] = orr;
        else L[i] = 0.5f * (ol + orr);
        outPk = std::max (outPk, std::max (std::abs (ol), std::abs (orr)));
    }
    atomicMax (outPeak, outPk);
    duckGain.store (duckMin);
}

juce::AudioProcessorEditor* DelayProcessor::createEditor() { return new DelayEditor (*this); }

#ifndef ONNO_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DelayProcessor(); }
#endif

// ======================================================================== Editor
namespace
{
const juce::Rectangle<float> rDisplay { 16, 50, 868, 236 }, rTime { 36, 70, 828, 186 };
}

DelayEditor::DelayEditor (DelayProcessor& p) : ModernEditor (p, p.apvts, "DELAY", 900, 470), proc (p)
{
    display = rDisplay;
    addHelp ({ { "TIME", "Verzögerungszeit. Mit SYNC als Notenwert zum Songtempo, sonst in ms." },
               { "FEEDBACK", "Wie viele Wiederholungen. Hoch = lange Echofahne." },
               { "LEVEL", "Lautstärke dieses Taps." },
               { "PAN", "Position des Taps im Stereobild." },
               { "LOW CUT", "Nimmt Bässe aus den Echos, damit es unten nicht matscht." },
               { "HIGH CUT", "Nimmt Höhen aus den Echos: dunkler, klingt wie analoges Band." },
               { "DRY", "Pegel des Originalsignals. Auf einem Send-Bus ganz runter (AUS)." },
               { "WET", "Pegel der Echos." },
               { "DUCKING", "Echos werden leiser, solange gespielt oder gesungen wird, und kommen in den Pausen." },
               { "TAP AN", "Tap ist aktiv. Klicken = Tap aus." },
               { "TAP AUS", "Tap ist aus. Klicken = Tap an." },
               { "SYNC", "Zeit als Notenwert zum Songtempo statt in ms." },
               { "PING-PONG", "Echos springen abwechselnd links und rechts." } });
    for (int t = 0; t < DelayProcessor::numTaps; ++t)
        buttons.push_back ({ { 28.0f + t * 84.0f, 306, 78, 20 },
                             [this, t] {
                                 const auto id = DelayProcessor::id (t, flag (DelayProcessor::id (t, "sync")) ? "div" : "time");
                                 return "TAP " + String (t + 1) + (flag (DelayProcessor::id (t, "on")) ? "  " + (flag (DelayProcessor::id (t, "sync")) ? param (id)->getCurrentValueAsText() : String (juce::roundToInt (value (id))) + "ms") : String ("  AUS"));
                             },
                             [this, t] { return sel == t; }, [this, t] { selectTap (t); }, mc::band (t * 2 + 3) });
    buttons.push_back ({ { 290, 306, 70, 20 }, [this] { return String (flag (DelayProcessor::id (sel, "on")) ? "TAP AN" : "TAP AUS"); },
                         [this] { return flag (DelayProcessor::id (sel, "on")); }, [this] { toggle (DelayProcessor::id (sel, "on")); }, mc::green() });
    buttons.push_back ({ { 366, 306, 70, 20 }, [] { return String ("SYNC"); }, [this] { return flag (DelayProcessor::id (sel, "sync")); },
                         [this] { toggle (DelayProcessor::id (sel, "sync")); selectTap (sel); }, modern::accent() });
    buttons.push_back ({ { 442, 306, 80, 20 }, [] { return String ("PING-PONG"); }, [this] { return flag (DelayProcessor::id (sel, "pp")); },
                         [this] { toggle (DelayProcessor::id (sel, "pp")); }, mc::purple() });
    knobs.reserve (12);
    addAnalogStrip();
    stripReady = true;
    selectTap (0);
}

void DelayEditor::selectTap (int t)
{
    sel = t;
    selSync = flag (DelayProcessor::id (t, "sync"));
    const auto c = mc::band (t * 2 + 3);
    knobs.clear();
    const float y = 372;
    if (selSync)
        knobs.push_back ({ DelayProcessor::id (t, "div"), "TIME", 62, y, 22, c, [] (float v) { return DelayProcessor::divNames[juce::jlimit (0, 12, juce::roundToInt (v))]; } });
    else
        knobs.push_back ({ DelayProcessor::id (t, "time"), "TIME", 62, y, 22, c, [] (float v) { return fmt::ms (v); } });
    knobs.push_back ({ DelayProcessor::id (t, "fb"), "FEEDBACK", 138, y, 22, c, [] (float v) { return fmt::pct (v); } });
    knobs.push_back ({ DelayProcessor::id (t, "level"), "LEVEL", 214, y, 22, c, [] (float v) { return v <= -59.9f ? String ("AUS") : fmt::db (v); } });
    knobs.push_back ({ DelayProcessor::id (t, "pan"), "PAN", 290, y, 22, c, [] (float v) { return fmt::pan (v); }, true });
    knobs.push_back ({ DelayProcessor::id (t, "hp"), "LOW CUT", 366, y, 22, mc::blue(), [] (float v) { return v < 20.5f ? String ("AUS") : fmt::hz (v); } });
    knobs.push_back ({ DelayProcessor::id (t, "lp"), "HIGH CUT", 442, y, 22, mc::blue(), [] (float v) { return v > 19900 ? String ("AUS") : fmt::hz (v); } });
    knobs.push_back ({ "dry", "DRY", 620, y, 24, modern::accent(), [] (float v) { return v <= -59.9f ? String ("AUS") : fmt::db (v); } });
    knobs.push_back ({ "wet", "WET", 700, y, 24, modern::accent(), [] (float v) { return v <= -59.9f ? String ("AUS") : fmt::db (v); } });
    knobs.push_back ({ "duck", "DUCKING", 780, y, 24, mc::amber(), [] (float v) { return fmt::pct (v); } });
    if (stripReady) { MKnob d { "drive", "DRIVE", 548, H - 27, 11, modern::warm(), [] (float v) { return fmt::pct (v); } }; d.compact = true; knobs.push_back (d); }
    repaint();
}

void DelayEditor::tick()
{
    proc.outPeak.exchange (0.0f);
    if (flag (DelayProcessor::id (sel, "sync")) != selSync && drag < 0) selectTap (sel);
    // Zeitachse folgt sanft dem längsten Tap
    float mx = 0;
    for (int t = 0; t < DelayProcessor::numTaps; ++t)
        if (flag (DelayProcessor::id (t, "on"))) mx = std::max (mx, proc.tapMs (t, proc.bpm.load()));
    const float target = juce::jlimit (400.0f, 4000.0f, mx * 4.2f);
    if (dragTap < 0) span += (target - span) * 0.2f;
}

float DelayEditor::spanMs() const { return span; }
float DelayEditor::msToX (float ms) const { return rTime.getX() + rTime.getWidth() * ms / spanMs(); }
float DelayEditor::xToMs (float x) const { return (x - rTime.getX()) / rTime.getWidth() * spanMs(); }
float DelayEditor::levelToY (float db) const { return rTime.getBottom() - rTime.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 54.0f); }

juce::Point<float> DelayEditor::headPos (int t) const
{
    return { msToX (proc.tapMs (t, proc.bpm.load())), levelToY (value (DelayProcessor::id (t, "level"))) };
}

int DelayEditor::hitHead (juce::Point<float> p) const
{
    for (int k = 0; k < DelayProcessor::numTaps; ++k)
    {
        const int t = (sel + k) % DelayProcessor::numTaps;
        if (! flag (DelayProcessor::id (t, "on"))) continue;
        const auto h = headPos (t);
        if (std::abs (p.x - h.x) < 10 && p.y > h.y - 12 && p.y < rTime.getBottom() + 4) return t;
    }
    return -1;
}

void DelayEditor::drawBackground (juce::Graphics& g)
{
    section (g, { 16, 296, 868, 126 });
    g.setColour (rgb (255, 255, 255, 0.05f));
    g.fillRect (560.0f, 310.0f, 1.0f, 100.0f);
}

void DelayEditor::drawDisplay (juce::Graphics& g, juce::Rectangle<float>)
{
    const auto r = rTime;
    const double tempo = proc.bpm.load();
    const float beatMs = (float) (60000.0 / std::max (20.0, tempo));

    // Raster: Viertel (hell) und Achtel
    g.setFont (font (9, false));
    for (int k = 0; k * beatMs * 0.5f <= spanMs(); ++k)
    {
        const float x = msToX (k * beatMs * 0.5f);
        g.setColour (k % 2 == 0 ? rgb (255, 255, 255, 0.10f) : mc::grid());
        g.fillRect (x, r.getY(), 1.0f, r.getHeight());
        const int every = spanMs() / beatMs > 12 ? 2 : 1;
        if (k % 2 == 0 && k > 0 && (k / 2) % every == 0)
        {
            g.setColour (mc::gridText()); ltext (g, String (k / 2), x + 3, r.getBottom() + 4, 9);
        }
    }
    for (int d = 0; d >= -48; d -= 12)
    {
        g.setColour (mc::grid()); g.fillRect (r.getX(), levelToY ((float) d), r.getWidth(), 1.0f);
        g.setColour (mc::gridText()); rtext (g, String (d), r.getX() - 4, levelToY ((float) d) - 6, 9);
    }
    g.setColour (rgb (255, 255, 255, 0.25f)); g.fillRect (r.getX(), r.getBottom(), r.getWidth(), 1.0f);

    // Trockensignal bei 0
    const float dry = value ("dry");
    if (dry > -59.9f)
    {
        g.setColour (modern::text().withAlpha (0.5f));
        g.fillRoundedRectangle (msToX (0) - 2, levelToY (dry), 4, r.getBottom() - levelToY (dry), 2);
    }

    // Echos: erster Schlag kräftig, Wiederholungen mit Feedback abfallend
    for (int k = 0; k < DelayProcessor::numTaps; ++k)
    {
        const int t = (sel + 1 + k) % DelayProcessor::numTaps;     // gewählter Tap zuletzt (oben)
        if (! flag (DelayProcessor::id (t, "on"))) continue;
        const auto c = mc::band (t * 2 + 3);
        const float ms = proc.tapMs (t, tempo);
        const float lvl = value (DelayProcessor::id (t, "level")) + value ("wet");
        const float fb = value (DelayProcessor::id (t, "fb")) * 0.01f;
        const bool pp = flag (DelayProcessor::id (t, "pp"));
        float db = value (DelayProcessor::id (t, "level"));
        for (int rep = 0; rep < 64; ++rep)
        {
            const float x = msToX (ms * (rep + 1));
            if (x > r.getRight() || db < -48) break;
            const float y = levelToY (db);
            const float w = rep == 0 ? 8.0f : 4.0f;
            g.setColour (c.withAlpha (rep == 0 ? 0.95f : 0.55f));
            g.fillRoundedRectangle (x - w * 0.5f, y, w, r.getBottom() - y, 2);
            if (pp && rep > 0)
            {
                g.setFont (font (8)); g.setColour (c.withAlpha (0.7f));
                ctext (g, rep % 2 == 0 ? "L" : "R", x, y - 12, 8);
            }
            db += dsp::a2db (std::max (fb, 1.0e-4f));
        }
        const auto h = headPos (t);
        const bool hv = t == hoverTap || t == dragTap;
        g.setColour (c.withAlpha (hv ? 0.35f : 0.2f)); g.fillEllipse (h.x - 13, h.y - 13, 26, 26);
        g.setColour (c); g.fillEllipse (h.x - 9, h.y - 9, 18, 18);
        g.setColour (modern::bg()); g.setFont (font (10));
        g.drawText (String (t + 1), juce::Rectangle<float> (h.x - 9, h.y - 9, 18, 18), juce::Justification::centred, false);
        juce::ignoreUnused (lvl);
    }

    // Tempo und Ducking
    g.setFont (font (11)); g.setColour (modern::dim());
    rtext (g, String (tempo, 1) + " BPM", r.getRight() - 4, r.getY() - 12, 11);
    const float dg = proc.duckGain.load();
    if (value ("duck") > 0.5f)
    {
        g.setColour (mc::amber().withAlpha (0.85f));
        rtext (g, "DUCK " + String (dsp::a2db (dg), 1) + " dB", r.getRight() - 90, r.getY() - 12, 11);
    }
}

void DelayEditor::drawOverlay (juce::Graphics& g)
{
    const int t = dragTap >= 0 ? dragTap : hoverTap;
    if (t >= 0)
    {
        String s = "Tap " + String (t + 1) + "   " + fmt::ms (proc.tapMs (t, proc.bpm.load()));
        if (flag (DelayProcessor::id (t, "sync"))) s << " (" << param (DelayProcessor::id (t, "div"))->getCurrentValueAsText() << ")";
        s << "   " << fmt::db (value (DelayProcessor::id (t, "level")));
        headerInfo (g, s);
    }
    else headerInfo (g, "Tap ziehen: links/rechts = Zeit, hoch/runter = Pegel");
}

void DelayEditor::displayMove (juce::Point<float> p) { hoverTap = display.contains (p) ? hitHead (p) : -1; }

bool DelayEditor::displayDown (juce::Point<float> p, const juce::MouseEvent&)
{
    const int t = hitHead (p);
    if (t < 0) return false;
    if (t != sel) selectTap (t);
    dragTap = t;
    beginEdit (DelayProcessor::id (t, flag (DelayProcessor::id (t, "sync")) ? "div" : "time"));
    beginEdit (DelayProcessor::id (t, "level"));
    return true;
}

void DelayEditor::displayDrag (juce::Point<float> p, const juce::MouseEvent&)
{
    if (dragTap < 0) return;
    const float ms = juce::jlimit (1.0f, DelayProcessor::maxMs, xToMs (p.x));
    if (flag (DelayProcessor::id (dragTap, "sync")))
    {
        // nächstliegende Notenlänge
        const float beats = ms / (60000.0f / std::max (20.0f, proc.bpm.load()));
        int best = 0;
        for (int d = 1; d < DelayProcessor::divNames.size(); ++d)
            if (std::abs (std::log (DelayProcessor::divBeats[d] / beats)) < std::abs (std::log (DelayProcessor::divBeats[best] / beats))) best = d;
        edit (DelayProcessor::id (dragTap, "div"), (float) best);
    }
    else edit (DelayProcessor::id (dragTap, "time"), ms);
    const float db = -48.0f + 54.0f * (rTime.getBottom() - p.y) / rTime.getHeight();
    edit (DelayProcessor::id (dragTap, "level"), juce::jlimit (-60.0f, 6.0f, db));
}

void DelayEditor::displayUp (juce::Point<float>)
{
    if (dragTap < 0) return;
    endEdit (DelayProcessor::id (dragTap, flag (DelayProcessor::id (dragTap, "sync")) ? "div" : "time"));
    endEdit (DelayProcessor::id (dragTap, "level"));
    dragTap = -1;
}
