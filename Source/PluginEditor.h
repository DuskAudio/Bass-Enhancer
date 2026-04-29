#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"

/**
 * BassEnhancerAudioProcessorEditor
 *
 * Minimal, professional UI:
 *  - Three rotary sliders (Intensity, Mix, Focus) with labels
 *  - A small "analysis" read-out showing the computed DSP params in real time
 *    (helps users understand adaptive behaviour during development/use)
 *
 * Layout:
 *
 *   ┌────────────────────────────────────────────────────┐
 *   │         ADAPTIVE BASS ENHANCER                     │
 *   │                                                    │
 *   │    [Intensity]    [Mix]    [Focus]                  │
 *   │                                                    │
 *   │  Harmonic: ████░░░░░  Compress: ███░░░░  Trans: ░░ │
 *   └────────────────────────────────────────────────────┘
 */
class BassEnhancerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit BassEnhancerAudioProcessorEditor (BassEnhancerAudioProcessor&);
    ~BassEnhancerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // =========================================================================
    // Reference to the processor (for reading analysis data)
    // =========================================================================
    BassEnhancerAudioProcessor& processorRef;

    // =========================================================================
    // Controls
    // =========================================================================
    struct LabelledSlider
    {
        juce::Slider slider;
        juce::Label  label;
    };

    LabelledSlider intensitySlider;
    LabelledSlider mixSlider;
    LabelledSlider focusSlider;

    // APVTS attachments keep the sliders in sync with parameter state
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> intensityAttach;
    std::unique_ptr<SliderAttachment> mixAttach;
    std::unique_ptr<SliderAttachment> focusAttach;

    // =========================================================================
    // Analysis display — updated via Timer
    // =========================================================================
    float displayHarmonic    = 0.f;
    float displayCompression = 0.f;
    float displayTransient   = 0.f;

    void timerCallback() override;

    // =========================================================================
    // Helpers
    // =========================================================================
    void setupSlider (LabelledSlider& ls, const juce::String& labelText);
    void drawAnalysisBar (juce::Graphics& g, const juce::String& label,
                          float value, juce::Rectangle<int> area,
                          juce::Colour barColour) const;

    // Palette
    static constexpr auto kBackground = 0xff1a1a2e;
    static constexpr auto kAccent     = 0xff00d4ff;
    static constexpr auto kWarm       = 0xffff6b35;
    static constexpr auto kText       = 0xffe0e0e0;
    static constexpr auto kDim        = 0xff555577;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassEnhancerAudioProcessorEditor)
};
