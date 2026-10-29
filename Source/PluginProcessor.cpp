#include "PluginProcessor.h"
#include "PluginEditor.h"

ImpactAudioProcessor::ImpactAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    , parameters (*this, nullptr, "Parameters",
        {
            std::make_unique<juce::AudioParameterFloat> ("lowAttack", "Low Attack",
                -1.0f, 1.0f, 0.0f),
            std::make_unique<juce::AudioParameterFloat> ("lowSustain", "Low Sustain",
                -1.0f, 1.0f, 0.0f),
            std::make_unique<juce::AudioParameterFloat> ("midAttack", "Mid Attack",
                -1.0f, 1.0f, 0.0f),
            std::make_unique<juce::AudioParameterFloat> ("midSustain", "Mid Sustain",
                -1.0f, 1.0f, 0.0f),
            std::make_unique<juce::AudioParameterFloat> ("highAttack", "High Attack",
                -1.0f, 1.0f, 0.0f),
            std::make_unique<juce::AudioParameterFloat> ("highSustain", "High Sustain",
                -1.0f, 1.0f, 0.0f),
            std::make_unique<juce::AudioParameterFloat> ("lowLength", "Low Transient Length",
                juce::NormalisableRange<float> (5.0f, 120.0f, 0.0f, 0.5f), 62.5f),
            std::make_unique<juce::AudioParameterFloat> ("midLength", "Mid Transient Length",
                juce::NormalisableRange<float> (5.0f, 120.0f, 0.0f, 0.5f), 62.5f),
            std::make_unique<juce::AudioParameterFloat> ("highLength", "High Transient Length",
                juce::NormalisableRange<float> (5.0f, 120.0f, 0.0f, 0.5f), 62.5f),
            std::make_unique<juce::AudioParameterFloat> ("punch", "Punch",
                0.0f, 1.0f, 0.0f),
            std::make_unique<juce::AudioParameterChoice> ("punchMode", "Punch Mode",
                juce::StringArray ({ "Attack", "Snap", "Thump", "Crisp" }), 0),
            std::make_unique<juce::AudioParameterFloat> ("crossoverLow", "Low Crossover",
                juce::NormalisableRange<float> (80.0f, 1000.0f, 0.0f, 0.4f), 200.0f),
            std::make_unique<juce::AudioParameterFloat> ("crossoverHigh", "High Crossover",
                juce::NormalisableRange<float> (1000.0f, 12000.0f, 0.0f, 0.4f), 3000.0f),
            std::make_unique<juce::AudioParameterFloat> ("mix", "Mix",
                0.0f, 1.0f, 1.0f),
            std::make_unique<juce::AudioParameterFloat> ("output", "Output",
                0.0f, 2.0f, 1.0f),
            std::make_unique<juce::AudioParameterBool> ("autoGain", "Auto Gain",
                true)
        })
{
}

ImpactAudioProcessor::~ImpactAudioProcessor() = default;

const juce::String ImpactAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool ImpactAudioProcessor::acceptsMidi() const  { return false; }
bool ImpactAudioProcessor::producesMidi() const { return false; }
bool ImpactAudioProcessor::isMidiEffect() const { return false; }

double ImpactAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int ImpactAudioProcessor::getNumPrograms()    { return 1; }
int ImpactAudioProcessor::getCurrentProgram() { return 0; }

void ImpactAudioProcessor::setCurrentProgram (int index) { juce::ignoreUnused (index); }

const juce::String ImpactAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void ImpactAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

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

    const auto param = [&] (const char* id)
    {
        return parameters.getRawParameterValue (id)->load();
    };

    impact::Params p;
    p.lowAttack   = param ("lowAttack");
    p.lowSustain  = param ("lowSustain");
    p.midAttack   = param ("midAttack");
    p.midSustain  = param ("midSustain");
    p.highAttack  = param ("highAttack");
    p.highSustain = param ("highSustain");
    p.lowLength   = param ("lowLength");
    p.midLength   = param ("midLength");
    p.highLength  = param ("highLength");
    p.punch       = param ("punch");
    p.punchMode   = juce::roundToInt (param ("punchMode"));
    p.crossoverLow  = param ("crossoverLow");
    p.crossoverHigh = param ("crossoverHigh");
    p.mix         = param ("mix");
    p.output      = param ("output");
    p.autoGain    = param ("autoGain") > 0.5f;

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
