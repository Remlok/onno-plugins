#include "VC1Processor.h"
#include "VC1Editor.h"

const char* const VC1Processor::knobIds[numKnobs] = { "thr", "ratio", "attack", "release", "knee", "output", "blend" };

static juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float a, float b, float n) { return a * std::exp (n * std::log (b / a)); },
             [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
             [] (float, float, float v) { return v; } };
}

juce::AudioProcessorValueTreeState::ParameterLayout VC1Processor::createLayout()
{
    using P = juce::AudioParameterFloat;
    using A = juce::AudioParameterFloatAttributes;
    auto db = A().withLabel ("dB");
    auto ms = A().withLabel ("ms");
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<P> (juce::ParameterID { "thr", 1 }, "Threshold", juce::NormalisableRange<float> (-60.0f, 0.0f), -18.0f, db));
    p.push_back (std::make_unique<P> (juce::ParameterID { "ratio", 1 }, "Ratio", logRange (1.0f, 20.0f), 4.0f, A().withLabel (":1")));
    p.push_back (std::make_unique<P> (juce::ParameterID { "attack", 1 }, "Attack", logRange (0.1f, 100.0f), 10.0f, ms));
    p.push_back (std::make_unique<P> (juce::ParameterID { "release", 1 }, "Release", logRange (10.0f, 1000.0f), 120.0f, ms));
    p.push_back (std::make_unique<P> (juce::ParameterID { "knee", 1 }, "Knee", juce::NormalisableRange<float> (0.0f, 24.0f), 6.0f, db));
    p.push_back (std::make_unique<P> (juce::ParameterID { "output", 1 }, "Output Gain", juce::NormalisableRange<float> (-12.0f, 24.0f), 0.0f, db));
    p.push_back (std::make_unique<P> (juce::ParameterID { "blend", 1 }, "Blend", juce::NormalisableRange<float> (0.0f, 100.0f), 100.0f, A().withLabel ("%")));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "meter", 1 }, "Meter", juce::StringArray { "Input", "Output", "G.C." }, 2));
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 }, "Bypass", false));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "view", 1 }, "Ansicht", juce::StringArray { "Konsole", "Modern" }, 0,
                                                               juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    return { p.begin(), p.end() };
}

VC1Processor::VC1Processor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VC1", createLayout())
{
}

bool VC1Processor::isBusesLayoutSupported (const BusesLayout& l) const
{
    auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        && l.getMainInputChannelSet() == out;
}

void VC1Processor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    env = 0.0f;
}

float VC1Processor::gainComputer (float x, float thr, float ratio, float knee)
{
    const float d = x - thr;
    if (2 * d < -knee) return x;
    if (knee > 0 && 2 * std::abs (d) <= knee)
        return x + (1 / ratio - 1) * (d + knee * 0.5f) * (d + knee * 0.5f) / (2 * knee);
    return thr + d / ratio;
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

void VC1Processor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
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

    const int nch = buf.getNumChannels(), n = buf.getNumSamples();
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
        L[i] *= wet;
        if (nch > 1) R[i] *= wet;
        inPk = std::max (inPk, lvl);
        outPk = std::max (outPk, std::max (std::abs (L[i]), std::abs (R[i])));
    }
    atomicMax (inPeak, inPk);
    atomicMax (outPeak, outPk);
    atomicMin (grPeak, grPk);
}

juce::AudioProcessorEditor* VC1Processor::createEditor() { return new VC1Editor (*this); }

void VC1Processor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void VC1Processor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

#ifndef ONNO_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VC1Processor(); }
#endif
