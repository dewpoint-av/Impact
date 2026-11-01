#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
    {
        using FloatP  = juce::AudioParameterFloat;
        using BoolP   = juce::AudioParameterBool;
        using ChoiceP = juce::AudioParameterChoice;
        using IntP    = juce::AudioParameterInt;
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        // Per-band: Bitwig-style Attack Gain / Attack Time / Sustain Gain /
        // Sustain Time (length) + enable. 4 bands.
        const char* bandName[4] = { "Low", "Low-Mid", "Mid", "High" };
        for (int b = 1; b <= 4; ++b)
        {
            const juce::String s  = juce::String (b);
            const juce::String nm = bandName[b - 1];
            layout.add (std::make_unique<FloatP> ("b" + s + "atk", nm + " Attack Gain", -1.0f, 1.0f, 0.0f));
            layout.add (std::make_unique<FloatP> ("b" + s + "atkTime", nm + " Attack Time",
                juce::NormalisableRange<float> (0.1f, 50.0f, 0.0f, 0.4f), 5.0f));
            layout.add (std::make_unique<FloatP> ("b" + s + "sus", nm + " Sustain Gain", -1.0f, 1.0f, 0.0f));
            layout.add (std::make_unique<FloatP> ("b" + s + "susTime", nm + " Sustain Time",
                juce::NormalisableRange<float> (10.0f, 500.0f, 0.0f, 0.5f), 150.0f));
            layout.add (std::make_unique<BoolP>  ("b" + s + "on", nm + " Enable", true));
        }

        // Movable LR4 crossovers (ST4b-style low-mid punch band by default).
        layout.add (std::make_unique<FloatP> ("xover1", "Crossover 1",
            juce::NormalisableRange<float> (40.0f, 1000.0f, 0.0f, 0.4f), 120.0f));
        layout.add (std::make_unique<FloatP> ("xover2", "Crossover 2",
            juce::NormalisableRange<float> (150.0f, 3000.0f, 0.0f, 0.4f), 400.0f));
        layout.add (std::make_unique<FloatP> ("xover3", "Crossover 3",
            juce::NormalisableRange<float> (1000.0f, 16000.0f, 0.0f, 0.4f), 3000.0f));

        layout.add (std::make_unique<FloatP>  ("punch", "Punch", 0.0f, 1.0f, 0.0f));
        layout.add (std::make_unique<ChoiceP> ("solo", "Solo",
            juce::StringArray { "Off", "Transient", "Body" }, 0));
        layout.add (std::make_unique<FloatP>  ("mix", "Mix", 0.0f, 1.0f, 1.0f));
        layout.add (std::make_unique<FloatP>  ("output", "Output", 0.0f, 2.0f, 1.0f));
        layout.add (std::make_unique<BoolP>   ("autoGain", "Auto Gain", true));

        // UI: which band the editor is currently editing (1..4).
        layout.add (std::make_unique<IntP>    ("band", "Band", 1, 4, 1));

        return layout;
    }
}

ImpactAudioProcessor::ImpactAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    , parameters (*this, nullptr, "Parameters", makeLayout())
{
}

ImpactAudioProcessor::~ImpactAudioProcessor() = default;

const juce::String ImpactAudioProcessor::getName() const { return JucePlugin_Name; }

bool ImpactAudioProcessor::acceptsMidi() const  { return false; }
bool ImpactAudioProcessor::producesMidi() const { return false; }
bool ImpactAudioProcessor::isMidiEffect() const { return false; }

double ImpactAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int ImpactAudioProcessor::getNumPrograms()    { return 1; }
int ImpactAudioProcessor::getCurrentProgram() { return 0; }
void ImpactAudioProcessor::setCurrentProgram (int index) { juce::ignoreUnused (index); }
const juce::String ImpactAudioProcessor::getProgramName (int index) { juce::ignoreUnused (index); return {}; }
void ImpactAudioProcessor::changeProgramName (int index, const juce::String& n) { juce::ignoreUnused (index, n); }

void ImpactAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
}

void ImpactAudioProcessor::releaseResources()
{
    engine.reset();
}

bool ImpactAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return in == out;
}

void ImpactAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int totalIn  = getTotalNumInputChannels();
    const int totalOut = getTotalNumOutputChannels();

    for (int ch = totalIn; ch < totalOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    if (totalIn == 0 || totalOut == 0)
        return;

    const float* inL = buffer.getReadPointer (0);
    const float* inR = totalIn > 1 ? buffer.getReadPointer (1) : inL;
    float* outL = buffer.getWritePointer (0);
    float* outR = totalOut > 1 ? buffer.getWritePointer (1) : outL;

    const auto param = [&] (const char* id) { return parameters.getRawParameterValue (id)->load(); };

    impact::Params p;
    for (int b = 0; b < impact::kNumBands; ++b)
    {
        const juce::String s = juce::String (b + 1);
        auto& bp = p.band[(size_t) b];
        bp.attackGain  = param (("b" + s + "atk").toRawUTF8());
        bp.attackTime  = param (("b" + s + "atkTime").toRawUTF8());
        bp.sustainGain = param (("b" + s + "sus").toRawUTF8());
        bp.sustainTime = param (("b" + s + "susTime").toRawUTF8());
        bp.enabled     = param (("b" + s + "on").toRawUTF8()) > 0.5f;
    }
    p.xover1   = param ("xover1");
    p.xover2   = param ("xover2");
    p.xover3   = param ("xover3");
    p.punch    = param ("punch");
    p.solo     = juce::roundToInt (param ("solo"));
    p.mix      = param ("mix");
    p.output   = param ("output");
    p.autoGain = param ("autoGain") > 0.5f;

    engine.setParams (p);
    engine.process (inL, inR, outL, outR, numSamples);
}

bool ImpactAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* ImpactAudioProcessor::createEditor()
{
    return new ImpactAudioProcessorEditor (*this);
}

void ImpactAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void ImpactAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (parameters.state.getType()))
        parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ImpactAudioProcessor();
}
