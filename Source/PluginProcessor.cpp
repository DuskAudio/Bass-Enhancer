#include "PluginProcessor.h"
#include "PluginEditor.h"

// =============================================================================
// Parameter layout
// =============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
BassEnhancerAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // "Intensity" — scales the harmonic and compression amounts
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "intensity",                          // parameterID
        "Intensity",                          // parameter name
        juce::NormalisableRange<float> (0.f, 1.f, 0.001f),
        0.5f,                                 // default
        juce::AudioParameterFloatAttributes{}
            .withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) {
                return juce::String (static_cast<int> (v * 100)) + " %";
            })
    ));

    // "Mix" — dry/wet blend
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "mix",
        "Mix",
        juce::NormalisableRange<float> (0.f, 1.f, 0.001f),
        0.5f,
        juce::AudioParameterFloatAttributes{}
            .withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) {
                return juce::String (static_cast<int> (v * 100)) + " %";
            })
    ));

    // "Focus" — shifts frequency emphasis: 0 = sub (20-80 Hz), 1 = low-mid (80-200 Hz)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "focus",
        "Focus",
        juce::NormalisableRange<float> (0.f, 1.f, 0.001f),
        0.5f,
        juce::AudioParameterFloatAttributes{}
            .withLabel ("")
            .withStringFromValueFunction ([] (float v, int) {
                if (v < 0.33f) return juce::String ("Sub");
                if (v > 0.66f) return juce::String ("Low-Mid");
                return juce::String ("Balanced");
            })
    ));

    return layout;
}

// =============================================================================
// Constructor / Destructor
// =============================================================================
BassEnhancerAudioProcessor::BassEnhancerAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BassEnhancerState", createParameterLayout())
{
}

BassEnhancerAudioProcessor::~BassEnhancerAudioProcessor() {}

// =============================================================================
// Lifecycle
// =============================================================================
void BassEnhancerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Denormal protection
    juce::FloatVectorOperations::disableDenormalisedNumberSupport();

    featureExtractor .prepare (sampleRate, samplesPerBlock);
    decisionEngine   .prepare (sampleRate, samplesPerBlock);
    adaptiveProcessor.prepare (sampleRate, samplesPerBlock,
                               getTotalNumInputChannels());
}

void BassEnhancerAudioProcessor::releaseResources() {}

// =============================================================================
// Bus layout
// =============================================================================
#ifndef JucePlugin_PreferredChannelConfigurations
bool BassEnhancerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Support mono→mono and stereo→stereo only
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet();
}
#endif

// =============================================================================
// processBlock — the heart of the plugin
// =============================================================================
void BassEnhancerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    // Clear any output buses we don't use
    for (int i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // -------------------------------------------------------------------------
    // 1. Read user parameters from APVTS
    // -------------------------------------------------------------------------
    const float intensity = apvts.getRawParameterValue ("intensity")->load();
    const float mix       = apvts.getRawParameterValue ("mix")      ->load();
    const float focus     = apvts.getRawParameterValue ("focus")    ->load();

    // -------------------------------------------------------------------------
    // 2. Feature extraction — analyses the input audio
    // -------------------------------------------------------------------------
    featureExtractor.process (buffer);
    lastFeatures = featureExtractor.getFeatures();

    // -------------------------------------------------------------------------
    // 3. Decision engine — maps features → DSP parameters
    // -------------------------------------------------------------------------
    decisionEngine.update (lastFeatures, intensity, focus);
    lastDSPParams = decisionEngine.getParameters();

    // -------------------------------------------------------------------------
    // 4. Adaptive processor — applies enhancement in place
    // -------------------------------------------------------------------------
    adaptiveProcessor.process (buffer, lastDSPParams, mix);
}

// =============================================================================
// Editor
// =============================================================================
juce::AudioProcessorEditor* BassEnhancerAudioProcessor::createEditor()
{
    return new BassEnhancerAudioProcessorEditor (*this);
}

// =============================================================================
// State persistence
// =============================================================================
void BassEnhancerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void BassEnhancerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// =============================================================================
// Plugin entry point (required by JUCE)
// =============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BassEnhancerAudioProcessor();
}
