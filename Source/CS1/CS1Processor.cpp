#include "CS1Processor.h"
#include "CS1Editor.h"

using namespace onno::dsp;

const char* const CS1Processor::presetNames[numPresets] = {
    "KICK", "SNARE", "TOMS", "BASS", "RHY GTR", "LEAD GTR", "VOCAL", "SCREAM", "OVERHEAD", "BUS"
};

static juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float a, float b, float n) { return a * std::exp (n * std::log (b / a)); },
             [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
             [] (float, float, float v) { return v; } };
}

juce::AudioProcessorValueTreeState::ParameterLayout CS1Processor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using B = juce::AudioParameterBool;
    using A = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;
    auto lin = [&] (const char* id, const char* name, float lo, float hi, float def, const char* unit) {
        v.push_back (std::make_unique<F> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (lo, hi), def, A().withLabel (unit)));
    };
    auto lg = [&] (const char* id, const char* name, float lo, float hi, float def, const char* unit) {
        v.push_back (std::make_unique<F> (juce::ParameterID { id, 1 }, name, logRange (lo, hi), def, A().withLabel (unit)));
    };
    auto bl = [&] (const char* id, const char* name, bool def) {
        v.push_back (std::make_unique<B> (juce::ParameterID { id, 1 }, name, def));
    };

    lin ("trim", "Input Trim", -24, 24, 0, "dB");
    lin ("drive", "Drive", 0, 100, 0, "%");
    lg ("hpf", "High-Pass", 16, 400, 16, "Hz");
    lg ("lpf", "Low-Pass", 3000, 22000, 22000, "Hz");

    bl ("gateOn", "Gate On", false);
    lin ("gateThr", "Gate Threshold", -80, 0, -50, "dB");
    lin ("gateRange", "Gate Range", 0, 60, 30, "dB");
    lg ("gateRel", "Gate Release", 10, 1000, 150, "ms");

    bl ("eqOn", "EQ On", true);
    lin ("lfGain", "LF Gain", -18, 18, 0, "dB");   lg ("lfFreq", "LF Freq", 30, 450, 100, "Hz");     bl ("lfBell", "LF Bell", false);
    lin ("lmfGain", "LMF Gain", -18, 18, 0, "dB"); lg ("lmfFreq", "LMF Freq", 200, 2500, 600, "Hz"); lg ("lmfQ", "LMF Q", 0.4f, 5, 1, "");
    lin ("hmfGain", "HMF Gain", -18, 18, 0, "dB"); lg ("hmfFreq", "HMF Freq", 600, 7000, 2500, "Hz"); lg ("hmfQ", "HMF Q", 0.4f, 5, 1, "");
    lin ("hfGain", "HF Gain", -18, 18, 0, "dB");   lg ("hfFreq", "HF Freq", 1500, 16000, 8000, "Hz"); bl ("hfBell", "HF Bell", false);

    bl ("compOn", "Comp On", false);
    lin ("compThr", "Comp Threshold", -50, 0, -20, "dB");
    lg ("compRatio", "Comp Ratio", 1, 20, 4, ":1");
    lg ("compAtt", "Comp Attack", 0.1f, 30, 10, "ms");
    lg ("compRel", "Comp Release", 30, 1500, 150, "ms");
    lin ("compMix", "Comp Mix", 0, 100, 100, "%");
    bl ("compPre", "Comp Pre EQ", false);

    bl ("dsOn", "De-Esser On", false);
    lg ("dsFreq", "De-Esser Freq", 3000, 12000, 6500, "Hz");
    lin ("dsThr", "De-Esser Threshold", -50, 0, -30, "dB");
    lin ("dsRange", "De-Esser Range", 0, 20, 6, "dB");

    lin ("out", "Output", -24, 24, 0, "dB");
    bl ("autoGain", "Auto Gain", false);
    bl ("bypass", "Bypass", false);
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "view", 1 }, "Ansicht", juce::StringArray { "Konsole", "Modern" }, 0,
                                                               juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    return { v.begin(), v.end() };
}

CS1Processor::CS1Processor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "CS1", createLayout())
{
}

bool CS1Processor::isBusesLayoutSupported (const BusesLayout& l) const
{
    auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        && l.getMainInputChannelSet() == out;
}

void CS1Processor::prepareToPlay (double sampleRate, int blockSize)
{
    sr = sampleRate;
    oversampling.reset();
    oversampling.initProcessing ((size_t) blockSize);
    setLatencySamples (juce::roundToInt (oversampling.getLatencyInSamples()));
    for (auto* b : { &hp1, &hp2, &lp, &lf, &lmf, &hmf, &hf, &dsLo1, &dsLo2, &dsHi1, &dsHi2, &dcBlock }) b->reset();
    dcBlock.highPass1 (sr, 5.0);
    gateEnv = -120; gateGain = 0; gateHold = 0; compEnv = 0; dsEnv = 0;
    autoIn = autoOut = 0; autoG = 1;
}

void CS1Processor::updateFilters()
{
    const float hpf = p ("hpf"), lpf = p ("lpf");
    hpOn = hpf > 16.5f;
    lpOn = lpf < 21500.0f;
    // Hochpass 18 dB/Okt (Butterworth 3. Ordnung), Tiefpass 12 dB/Okt
    hp1.highPass1 (sr, hpf);
    hp2.highPass (sr, hpf, 1.0);
    lp.lowPass (sr, std::min ((double) lpf, sr * 0.45), 0.7071);
    lf.lowShelf (sr, p ("lfFreq"), 0.7071, p ("lfGain"));
    if (on ("lfBell")) lf.peak (sr, p ("lfFreq"), 0.9, p ("lfGain"));
    lmf.peak (sr, p ("lmfFreq"), p ("lmfQ"), p ("lmfGain"));
    hmf.peak (sr, std::min ((double) p ("hmfFreq"), sr * 0.45), p ("hmfQ"), p ("hmfGain"));
    hf.highShelf (sr, std::min ((double) p ("hfFreq"), sr * 0.45), 0.7071, p ("hfGain"));
    if (on ("hfBell")) hf.peak (sr, std::min ((double) p ("hfFreq"), sr * 0.45), 0.9, p ("hfGain"));
    // Linkwitz-Riley-Weiche 24 dB/Okt: beide Hälften summieren sich ohne Pegelfehler
    const double dsf = std::min ((double) p ("dsFreq"), sr * 0.45);
    dsLo1.lowPass (sr, dsf, 0.7071); dsLo2.lowPass (sr, dsf, 0.7071);
    dsHi1.highPass (sr, dsf, 0.7071); dsHi2.highPass (sr, dsf, 0.7071);
}

float CS1Processor::eqResponseDb (float f) const
{
    Biquad b; double sum = 0;
    const float hpf = p ("hpf"), lpf = p ("lpf");
    if (hpf > 16.5f) { b.highPass1 (sr, hpf); sum += b.magDb (sr, f); b.highPass (sr, hpf, 1.0); sum += b.magDb (sr, f); }
    if (lpf < 21500.0f) { b.lowPass (sr, std::min ((double) lpf, sr * 0.45), 0.7071); sum += b.magDb (sr, f); }
    if (on ("eqOn"))
    {
        if (on ("lfBell")) b.peak (sr, p ("lfFreq"), 0.9, p ("lfGain")); else b.lowShelf (sr, p ("lfFreq"), 0.7071, p ("lfGain"));
        sum += b.magDb (sr, f);
        b.peak (sr, p ("lmfFreq"), p ("lmfQ"), p ("lmfGain")); sum += b.magDb (sr, f);
        b.peak (sr, std::min ((double) p ("hmfFreq"), sr * 0.45), p ("hmfQ"), p ("hmfGain")); sum += b.magDb (sr, f);
        const double hff = std::min ((double) p ("hfFreq"), sr * 0.45);
        if (on ("hfBell")) b.peak (sr, hff, 0.9, p ("hfGain")); else b.highShelf (sr, hff, 0.7071, p ("hfGain"));
        sum += b.magDb (sr, f);
    }
    return (float) sum;
}

static void atomicMax (std::atomic<float>& a, float v)
{
    float cur = a.load();
    while (v > cur && ! a.compare_exchange_weak (cur, v)) {}
}

static void atomicMin (std::atomic<float>& a, float v)
{
    float cur = a.load();
    while (v < cur && ! a.compare_exchange_weak (cur, v)) {}
}

void CS1Processor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int nch = std::min (2, buf.getNumChannels()), n = buf.getNumSamples();
    if (nch == 0 || n == 0) return;
    auto* L = buf.getWritePointer (0);
    auto* R = buf.getWritePointer (nch > 1 ? 1 : 0);

    // --- Auto-Trim: Eingangspegel vor dem Trim messen (nur Passagen mit Signal) ---
    if (autoTrimArmed.load())
    {
        double e = 0;
        for (int i = 0; i < n; ++i) e += 0.5 * (L[i] * L[i] + R[i] * R[i]);
        if (e / n > 1.0e-6)  // über -60 dBFS
        {
            trimSum += e; trimCount += n;
            autoTrimProgress = (float) std::min (1.0, trimCount / (3.0 * sr));
            if (trimCount >= (long) (3.0 * sr))
            {
                const float rmsDb = 10.0f * std::log10 ((float) (trimSum / trimCount));
                autoTrimResult = juce::jlimit (-24.0f, 24.0f, -18.0f - rmsDb);
                autoTrimArmed = false;
                autoTrimDone = true;
                trimSum = 0; trimCount = 0;
            }
        }
    }
    else { trimSum = 0; trimCount = 0; }

    if (on ("bypass"))
    {
        float pk = 0;
        for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i])));
        atomicMax (inPeak, pk); atomicMax (outPeak, pk);
        return;
    }

    updateFilters();

    // --- Trim ---
    const float trim = db2a (p ("trim"));
    for (int c = 0; c < nch; ++c) buf.applyGain (c, 0, n, trim);

    // Pegel nach dem Trim für Meter und Auto-Gain
    float inPk = 0;
    for (int i = 0; i < n; ++i) inPk = std::max (inPk, std::max (std::abs (L[i]), std::abs (R[i])));
    atomicMax (inPeak, inPk);
    // Auto-Gain: Lautheit des getrimmten Eingangs (Zeitkonstante 1,5 s)
    const bool autoGain = on ("autoGain");
    const float aMs = coef (1500.0f, sr), aSm = coef (50.0f, sr);
    if (autoGain)
        for (int i = 0; i < n; ++i) autoIn = aMs * autoIn + (1 - aMs) * 0.5f * (L[i] * L[i] + R[i] * R[i]);

    // --- Drive: weiche Sättigung mit leichter Asymmetrie, 2x überabgetastet ---
    {
        juce::dsp::AudioBlock<float> block (buf.getArrayOfWritePointers(), (size_t) nch, (size_t) n);
        auto up = oversampling.processSamplesUp (block);
        const float drive = p ("drive") * 0.01f;
        if (drive > 0.001f)
        {
            const float k = 1.0f + 9.0f * drive * drive, bias = 0.15f * drive;
            const float tb = std::tanh (bias), norm = 1.0f / (k * (1 - tb * tb));
            for (size_t c = 0; c < up.getNumChannels(); ++c)
            {
                auto* d = up.getChannelPointer (c);
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                    d[i] = (std::tanh (k * d[i] + bias) - tb) * norm;
            }
        }
        oversampling.processSamplesDown (block);
    }

    // --- Parameter für die Schleife ---
    const bool gateOn = on ("gateOn"), eqOn = on ("eqOn"), compOn = on ("compOn"), compPre = on ("compPre"), dsOn = on ("dsOn");
    const bool driveOn = p ("drive") > 0.1f;
    const float gThr = p ("gateThr"), gRange = p ("gateRange");
    const float gRel = coef (p ("gateRel"), sr), gAtt = coef (0.3f, sr), gDet = coef (20.0f, sr);
    const int gHoldN = (int) (0.015 * sr);
    const float cThr = p ("compThr"), cRatio = std::max (1.0f, p ("compRatio")), cKnee = 6.0f;
    const float cAtt = coef (p ("compAtt"), sr), cRel = coef (p ("compRel"), sr), cMix = p ("compMix") * 0.01f;
    const float dThr = p ("dsThr"), dRange = p ("dsRange"), dAtt = coef (1.0f, sr), dRel = coef (60.0f, sr);
    const float outG = db2a (p ("out"));
    float gMin = 0, cMin = 0, dMin = 0, outPk = 0;

    auto eq = [&] (float& l, float& r) {
        l = hf.process (hmf.process (lmf.process (lf.process (l, 0), 0), 0), 0);
        r = hf.process (hmf.process (lmf.process (lf.process (r, 1), 1), 1), 1);
    };
    auto comp = [&] (float& l, float& r) {
        const float xdb = a2db (std::max (std::abs (l), std::abs (r)));
        const float d = xdb - cThr;
        float y = xdb;
        if (2 * d >= -cKnee)
            y = (2 * std::abs (d) <= cKnee) ? xdb + (1 / cRatio - 1) * (d + cKnee * 0.5f) * (d + cKnee * 0.5f) / (2 * cKnee)
                                           : cThr + d / cRatio;
        const float gr = y - xdb;
        compEnv = gr < compEnv ? cAtt * compEnv + (1 - cAtt) * gr : cRel * compEnv + (1 - cRel) * gr;
        const float g = cMix * db2a (compEnv) + 1 - cMix;
        l *= g; r *= g;
        cMin = std::min (cMin, compEnv);
    };

    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R[i];
        if (driveOn) { l = dcBlock.process (l, 0); r = dcBlock.process (r, 1); }

        // Filter
        if (hpOn) { l = hp2.process (hp1.process (l, 0), 0); r = hp2.process (hp1.process (r, 1), 1); }
        if (lpOn) { l = lp.process (l, 0); r = lp.process (r, 1); }

        // Gate / Expander (1:3 unter der Schwelle, begrenzt auf Range, mit Hold)
        if (gateOn)
        {
            const float lvl = a2db (std::max (std::abs (l), std::abs (r)));
            gateEnv = lvl > gateEnv ? lvl : gDet * gateEnv + (1 - gDet) * lvl;
            float target = 0;
            if (gateEnv >= gThr) gateHold = gHoldN;
            else if (gateHold > 0) --gateHold;
            else target = std::max (-gRange, (gateEnv - gThr) * 2.0f);
            gateGain = target > gateGain ? gAtt * gateGain + (1 - gAtt) * target : gRel * gateGain + (1 - gRel) * target;
            const float g = db2a (gateGain);
            l *= g; r *= g;
            gMin = std::min (gMin, gateGain);
        }

        if (compPre && compOn) comp (l, r);
        if (eqOn) eq (l, r);
        if (! compPre && compOn) comp (l, r);

        // De-Esser: nur der Bereich über der Frequenz wird abgesenkt
        if (dsOn)
        {
            const float hl = dsHi2.process (dsHi1.process (l, 0), 0), hr = dsHi2.process (dsHi1.process (r, 1), 1);
            const float ll = dsLo2.process (dsLo1.process (l, 0), 0), lr = dsLo2.process (dsLo1.process (r, 1), 1);
            const float lvl = a2db (std::max (std::abs (hl), std::abs (hr)));
            const float target = lvl > dThr ? std::max (-dRange, -(lvl - dThr) * 0.75f) : 0.0f;
            dsEnv = target < dsEnv ? dAtt * dsEnv + (1 - dAtt) * target : dRel * dsEnv + (1 - dRel) * target;
            const float g = db2a (dsEnv);
            l = ll + g * hl; r = lr + g * hr;
            dMin = std::min (dMin, dsEnv);
        }

        // Auto-Gain: Ausgang auf die Lautheit des (getrimmten) Eingangs angleichen
        if (autoGain)
        {
            autoOut = aMs * autoOut + (1 - aMs) * 0.5f * (l * l + r * r);
            if (autoIn > 1.0e-7f && autoOut > 1.0e-9f)
            {
                const float t = juce::jlimit (0.25f, 4.0f, std::sqrt (autoIn / autoOut));
                autoG = aSm * autoG + (1 - aSm) * t;
            }
            l *= autoG; r *= autoG;
        }
        else autoG = 1.0f;

        l *= outG; r *= outG;
        L[i] = l;
        if (nch > 1) R[i] = r;
        outPk = std::max (outPk, std::max (std::abs (l), std::abs (r)));
    }

    atomicMax (outPeak, outPk);
    atomicMin (gateGr, gMin);
    atomicMin (compGr, cMin);
    atomicMin (dsGr, dMin);
    autoGainDb = a2db (autoG);
}

// ----------------------------------------------------------------- Workflow
void CS1Processor::setParam (const juce::String& id, float value)
{
    if (auto* prm = apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        prm->endChangeGesture();
    }
}

namespace
{
struct PV { const char* id; float v; };
// Startpunkte für typische Metal-Produktionen; alles Weitere bleibt auf Neutral
const std::vector<PV> presets[CS1Processor::numPresets] = {
    // KICK
    { { "hpf", 30 }, { "drive", 15 }, { "gateOn", 1 }, { "gateThr", -40 }, { "gateRange", 30 }, { "gateRel", 120 },
      { "lfGain", 4 }, { "lfFreq", 60 }, { "lfBell", 1 }, { "lmfGain", -5 }, { "lmfFreq", 350 }, { "lmfQ", 1.5f },
      { "hmfGain", 5 }, { "hmfFreq", 4000 }, { "hmfQ", 1.2f }, { "hfGain", 1 }, { "hfFreq", 10000 },
      { "compOn", 1 }, { "compThr", -20 }, { "compRatio", 4 }, { "compAtt", 8 }, { "compRel", 120 } },
    // SNARE
    { { "hpf", 70 }, { "drive", 20 }, { "gateOn", 1 }, { "gateThr", -35 }, { "gateRange", 20 }, { "gateRel", 180 },
      { "lfGain", 3 }, { "lfFreq", 200 }, { "lfBell", 1 }, { "lmfGain", -3 }, { "lmfFreq", 500 }, { "lmfQ", 1.4f },
      { "hmfGain", 3 }, { "hmfFreq", 5000 }, { "hfGain", 3 }, { "hfFreq", 12000 },
      { "compOn", 1 }, { "compThr", -22 }, { "compRatio", 4 }, { "compAtt", 10 }, { "compRel", 150 }, { "compMix", 80 } },
    // TOMS
    { { "hpf", 50 }, { "gateOn", 1 }, { "gateThr", -38 }, { "gateRange", 30 }, { "gateRel", 250 },
      { "lfGain", 3 }, { "lfFreq", 100 }, { "lfBell", 1 }, { "lmfGain", -5 }, { "lmfFreq", 400 }, { "lmfQ", 1.2f },
      { "hmfGain", 4 }, { "hmfFreq", 3500 }, { "hfGain", 1 }, { "hfFreq", 10000 },
      { "compOn", 1 }, { "compThr", -20 }, { "compRatio", 4 }, { "compAtt", 10 }, { "compRel", 200 } },
    // BASS
    { { "hpf", 30 }, { "drive", 35 }, { "lfGain", 2 }, { "lfFreq", 80 }, { "lmfGain", -3 }, { "lmfFreq", 250 },
      { "hmfGain", 3 }, { "hmfFreq", 1500 }, { "compOn", 1 }, { "compThr", -24 }, { "compRatio", 6 }, { "compAtt", 5 }, { "compRel", 80 } },
    // RHY GTR
    { { "hpf", 90 }, { "lpf", 10000 }, { "lfGain", -2 }, { "lfFreq", 120 }, { "lfBell", 1 },
      { "lmfGain", -3 }, { "lmfFreq", 300 }, { "lmfQ", 1.2f }, { "hmfGain", -2.5f }, { "hmfFreq", 3200 }, { "hmfQ", 2.5f },
      { "hfGain", -3 }, { "hfFreq", 7500 }, { "hfBell", 1 },
      { "compOn", 1 }, { "compThr", -12 }, { "compRatio", 2 }, { "compAtt", 20 }, { "compRel", 150 } },
    // LEAD GTR
    { { "hpf", 100 }, { "lpf", 12000 }, { "lmfGain", -2 }, { "lmfFreq", 500 }, { "hmfGain", 2.5f }, { "hmfFreq", 2200 },
      { "hfGain", 1 }, { "hfFreq", 10000 }, { "compOn", 1 }, { "compThr", -18 }, { "compRatio", 3 }, { "compAtt", 15 }, { "compRel", 150 } },
    // VOCAL
    { { "hpf", 90 }, { "drive", 10 }, { "lmfGain", -2 }, { "lmfFreq", 300 }, { "hmfGain", 2 }, { "hmfFreq", 3000 },
      { "hfGain", 3 }, { "hfFreq", 12000 }, { "compOn", 1 }, { "compThr", -20 }, { "compRatio", 4 }, { "compAtt", 5 }, { "compRel", 100 },
      { "dsOn", 1 }, { "dsFreq", 6500 }, { "dsThr", -30 }, { "dsRange", 6 } },
    // SCREAM
    { { "hpf", 120 }, { "lpf", 14000 }, { "drive", 25 }, { "lmfGain", -3 }, { "lmfFreq", 400 },
      { "hmfGain", -2 }, { "hmfFreq", 2500 }, { "hmfQ", 2 }, { "hfGain", 2 }, { "hfFreq", 10000 },
      { "compOn", 1 }, { "compThr", -24 }, { "compRatio", 6 }, { "compAtt", 2 }, { "compRel", 60 },
      { "dsOn", 1 }, { "dsFreq", 5500 }, { "dsThr", -28 }, { "dsRange", 8 } },
    // OVERHEAD
    { { "hpf", 180 }, { "lmfGain", -2 }, { "lmfFreq", 500 }, { "hmfGain", -1.5f }, { "hmfFreq", 3500 }, { "hmfQ", 2 },
      { "hfGain", 2 }, { "hfFreq", 12000 }, { "compOn", 1 }, { "compThr", -16 }, { "compRatio", 2 }, { "compAtt", 25 }, { "compRel", 200 }, { "compMix", 60 } },
    // BUS
    { { "hpf", 25 }, { "drive", 10 }, { "lfGain", 1 }, { "lfFreq", 60 }, { "hfGain", 1 }, { "hfFreq", 12000 },
      { "compOn", 1 }, { "compThr", -14 }, { "compRatio", 2 }, { "compAtt", 30 }, { "compRel", 300 } },
};

// Diese Parameter bleiben beim Preset-Wechsel stehen: Pegelanpassung und Bedienung
bool keepOnPreset (const juce::String& id)
{
    return id == "trim" || id == "out" || id == "autoGain" || id == "bypass" || id == "view";
}
}

void CS1Processor::applyPreset (int index)
{
    if (index < 0 || index >= numPresets) return;
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (! keepOnPreset (rp->getParameterID()) && std::abs (rp->getValue() - rp->getDefaultValue()) > 1.0e-6f)
            {
                rp->beginChangeGesture();
                rp->setValueNotifyingHost (rp->getDefaultValue());
                rp->endChangeGesture();
            }
    for (auto& pv : presets[index]) setParam (pv.id, pv.v);
    presetName = presetNames[index];
}

void CS1Processor::selectSlot (int s)
{
    if (s == slot || s < 0 || s > 1) return;
    const float view = p ("view");
    slots[(size_t) slot] = apvts.copyState();
    slots[(size_t) slot].setProperty ("preset", presetName, nullptr);
    if (slots[(size_t) s].isValid())
    {
        // Werte über die Parameter setzen, damit der Host die Änderung sieht
        for (auto* prm : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            {
                auto child = slots[(size_t) s].getChildWithProperty ("id", rp->getParameterID());
                if (child.isValid() && rp->getParameterID() != "view")
                {
                    rp->beginChangeGesture();
                    rp->setValueNotifyingHost (rp->convertTo0to1 ((float) child.getProperty ("value")));
                    rp->endChangeGesture();
                }
            }
        presetName = slots[(size_t) s].getProperty ("preset").toString();
    }
    slot = s;
    setParam ("view", view);
}

juce::AudioProcessorEditor* CS1Processor::createEditor() { return new CS1Editor (*this); }

void CS1Processor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", presetName, nullptr);
    state.setProperty ("slot", slot, nullptr);
    juce::ValueTree ab ("AB");
    for (int i = 0; i < 2; ++i)
        if (slots[(size_t) i].isValid() && i != slot)
            ab.appendChild (slots[(size_t) i].createCopy().setProperty ("slotIndex", i, nullptr), nullptr);
    state.removeChild (state.getChildWithName ("AB"), nullptr);
    state.appendChild (ab, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void CS1Processor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            auto ab = state.getChildWithName ("AB");
            slots = {};
            for (auto child : ab)
                slots[(size_t) (int) child.getProperty ("slotIndex")] = child.createCopy();
            state.removeChild (ab, nullptr);
            slot = state.getProperty ("slot", 0);
            presetName = state.getProperty ("preset").toString();
            apvts.replaceState (state);
        }
}

#ifndef ONNO_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CS1Processor(); }
#endif
