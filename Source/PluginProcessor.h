#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "FeatureExtractor.h"
#include "DecisionEngine.h"
#include "AdaptiveProcessor.h"

/**
 * BassEnhancerAudioProcessor
 *
 * Top-level JUCE AudioProcessor.  Owns the three DSP components and the
 * AudioProcessorValueTreeState that exposes user-facing parameters.
 *
 * Parameter list
 * --------------
 *  "intensity"  – Overall harmonic/compression drive  [0, 1]   default 0.5
 *  "mix"        – Dry/wet blend                       [0, 1]   default 0.5
 *  "focus"      – Frequency emphasis sub↔low-mid      [0, 1]   default 0.5
 */
class BassEnhancerAudioProcessor : public juce::AudioProcessor
{
public:
    BassEnhancerAudioProcessor();
    ~BassEnhancerAudioProcessor() override;

    // =========================================================================
    // AudioProcessor interface
    // =========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool   acceptsMidi()  const override { return false; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // =========================================================================
    // Public access (used by the editor to bind controls)
    // =========================================================================
    juce::AudioProcessorValueTreeState apvts;

    /** Expose analysis data for optional metering in the editor. */
    const AudioFeatures& getLatestFeatures() const noexcept { return lastFeatures; }
    const DSPParameters& getLatestDSPParams() const noexcept { return lastDSPParams; }

private:
    // =========================================================================
    // Parameter layout helper — called in the APVTS constructor
    // =========================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // =========================================================================
    // DSP pipeline components
    // =========================================================================
    FeatureExtractor  featureExtractor;
    DecisionEngine    decisionEngine;
    AdaptiveProcessor adaptiveProcessor;

    // Cached for metering / debug display in editor
    AudioFeatures lastFeatures;
    DSPParameters lastDSPParams;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassEnhancerAudioProcessor)
};
