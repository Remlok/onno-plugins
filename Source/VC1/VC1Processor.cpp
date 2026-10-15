#include "VC1Processor.h"
#include "VC1Editor.h"

const char* const VC1Processor::knobIds[numKnobs] = { "thr", "ratio", "attack", "release", "knee", "output", "blend" };

juce::AudioProcessorValueTreeState::ParameterLayout VC1Processor::createLayout()
{
    using P = juce::AudioParameterFloat;
    using A = juce::AudioParameterFloatAttributes;
    auto db = A().withLabel ("dB");
    auto ms = A().withLabel ("ms");
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<P> (juce::ParameterID { "thr", 1 }, "Threshold", juce::NormalisableRange<float> (-60.0f, 0.0f), -18.0f, db));
    p.push_back (std::make_unique<P> (juce::ParameterID { "ratio", 1 }, "Ratio", onno::logRange (1.0f, 20.0f), 4.0f, A().withLabel (":1")));
    p.push_back (std::make_unique<P> (juce::ParameterID { "attack", 1 }, "Attack", onno::logRange (0.1f, 100.0f), 10.0f, ms));
    p.push_back (std::make_unique<P> (juce::ParameterID { "release", 1 }, "Release", onno::logRange (10.0f, 1000.0f), 120.0f, ms));
    p.push_back (std::make_unique<P> (juce::ParameterID { "knee", 1 }, "Knee", juce::NormalisableRange<float> (0.0f, 24.0f), 6.0f, db));
    p.push_back (std::make_unique<P> (juce::ParameterID { "output", 1 }, "Output Gain", juce::NormalisableRange<float> (-12.0f, 24.0f), 0.0f, db));
    p.push_back (std::make_unique<P> (juce::ParameterID { "blend", 1 }, "Blend", juce::NormalisableRange<float> (0.0f, 100.0f), 100.0f, A().withLabel ("%")));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "meter", 1 }, "Meter", juce::StringArray { "Input", "Output", "G.C." }, 2));
    onno::Analog::addParams (p);
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 }, "Bypass", false));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "view", 1 }, "Ansicht", juce::StringArray { "Konsole", "Modern" }, 0,
                                                               juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    return { p.begin(), p.end() };
}

VC1Processor::VC1Processor() : ModernProcessor ("Onno VC-1", "VC1", stereoBuses(), createLayout())
{
    analog.attach (*this);
}

void VC1Processor::prepare (double sampleRate, int)
{
    sr = sampleRate;
    env = 0.0f;
    analog.prepare (sr);
}

float VC1Processor::gainComputer (float x, float thr, float ratio, float knee)
{
    const float d = x - thr;
    if (2 * d < -knee) return x;
    if (knee > 0 && 2 * std::abs (d) <= knee)
        return x + (1 / ratio - 1) * (d + knee * 0.5f) * (d + knee * 0.5f) / (2 * knee);
    return thr + d / ratio;
}

void VC1Processor::process (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto val = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    const float thr = val ("thr");
    const float ratio = std::max (1.0f, val ("ratio"));
    const float knee = std::max (0.0f, val ("knee"));
    const float att = (float) std::exp (-1.0 / (std::max (0.01f, val ("attack")) * 0.001 * sr));
    const float rel = (float) std::exp (-1.0 / (std::max (1.0f, val ("release")) * 0.001 * sr));
    const float makeup = juce::Decibels::decibelsToGain (val ("output"));
    const float mix = val ("blend") * 0.01f;
    const bool bypass = val ("bypass") > 0.5f;

    const int nch = std::min (2, getMainBusNumInputChannels()), n = buf.getNumSamples();
    if (nch == 0) return;
    analog.beginBlock();
    float inPk = 0, outPk = 0, grPk = 0;
    auto* L = buf.getWritePointer (0);
    auto* R = buf.getWritePointer (nch > 1 ? 1 : 0);

    for (int i = 0; i < n; ++i)
    {
        const float lvl = std::max (std::abs (L[i]), std::abs (R[i]));
        const float xdb = 8.685889638f * std::log (std::max (lvl, 1.0e-9f));
        const float gr = gainComputer (xdb, thr, ratio, knee) - xdb;
        env = gr < env ? att * env + (1 - att) * gr : rel * env + (1 - rel) * gr;
        float wet = 1.0f;
        if (! bypass)
        {
            const float g = std::exp (env * 0.11512925f) * makeup;
            wet = mix * g + 1 - mix;
            grPk = std::min (grPk, env);
        }
        float ol = L[i] * wet, orr = R[i] * wet;
        if (! bypass && analog.active())
        {
            // Färbung nach Kompression und Blend
            analog.advance();
            ol = analog.tick (ol, 0); orr = analog.tick (orr, 1);
        }
        L[i] = ol;
        if (nch > 1) R[i] = orr;
        inPk = std::max (inPk, lvl);
        outPk = std::max (outPk, std::max (std::abs (L[i]), std::abs (R[i])));
    }
    onno::atomicMax (inPeak, inPk);
    onno::atomicMax (outPeak, outPk);
    onno::atomicMin (grPeak, grPk);
}

// Startpunkte für Metal-Produktionen (Analog: 0 Clean, 1 Brit, 2 Console, 3 Tube, 4 FET)
std::vector<onno::ModernProcessor::FactoryPreset> VC1Processor::factoryPresets() const
{
    return {
        { "Drum Bus Glue", { { "thr", -20 }, { "ratio", 4 }, { "attack", 10 }, { "release", 100 }, { "knee", 6 }, { "blend", 60 }, { "output", 2 }, { "analog", 1 }, { "drive", 30 } } },
        { "Kick Punch", { { "thr", -18 }, { "ratio", 4 }, { "attack", 30 }, { "release", 80 }, { "knee", 3 }, { "output", 2 } } },
        { "Snare Smack", { { "thr", -22 }, { "ratio", 6 }, { "attack", 3 }, { "release", 60 }, { "blend", 70 }, { "output", 2 }, { "analog", 4 }, { "drive", 25 } } },
        { "Room Crush", { { "thr", -40 }, { "ratio", 20 }, { "attack", 0.3f }, { "release", 50 }, { "knee", 0 }, { "blend", 30 }, { "output", 6 }, { "analog", 4 }, { "drive", 50 } } },
        { "Bass Steady", { { "thr", -24 }, { "ratio", 6 }, { "attack", 5 }, { "release", 120 }, { "output", 4 }, { "analog", 3 }, { "drive", 30 } } },
        { "Vocal Ride", { { "thr", -20 }, { "ratio", 3 }, { "attack", 5 }, { "release", 150 }, { "knee", 12 }, { "output", 3 } } },
        { "Mix Bus 2:1", { { "thr", -12 }, { "ratio", 2 }, { "attack", 30 }, { "release", 300 }, { "knee", 12 }, { "output", 1 }, { "analog", 2 }, { "drive", 20 } } },
    };
}

juce::AudioProcessorEditor* VC1Processor::createEditor() { return new VC1Editor (*this); }

#ifndef ONNO_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VC1Processor(); }
#endif
