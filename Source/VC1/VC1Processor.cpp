#include "VC1Processor.h"
#include "VC1Editor.h"

const char* const VC1Processor::knobIds[numKnobs] = { "thr", "ratio", "attack", "release", "knee", "output", "blend" };

juce::AudioProcessorValueTreeState::ParameterLayout VC1Processor::createLayout()
{
    onno::prm::Layout p;
    onno::prm::fromCore (p, onno::core::VC1Core::makeParams());
    return onno::prm::done (p);
}

VC1Processor::VC1Processor() : ModernProcessor ("Onno VC-1", "VC1", stereoBuses(), createLayout())
{
    link.attach (apvts, core);
}

void VC1Processor::prepare (double sampleRate, int blockSize)
{
    link.pull (core);
    core.prepare (sampleRate, blockSize);
}

void VC1Processor::process (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int nch = std::min (2, getMainBusNumInputChannels()), n = buf.getNumSamples();
    if (nch == 0) return;
    link.pull (core);
    core.process (buf.getWritePointer (0), buf.getWritePointer (nch > 1 ? 1 : 0), n, nch, nullptr, nullptr);
    onno::atomicMax (inPeak, core.inPk);
    onno::atomicMax (outPeak, core.outPk);
    onno::atomicMin (grPeak, core.grMin);
    core.resetDisplay();
}

juce::AudioProcessorEditor* VC1Processor::createEditor() { return new VC1Editor (*this); }

#ifndef ONNO_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VC1Processor(); }
#endif
