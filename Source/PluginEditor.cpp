#include "PluginEditor.h"

// =============================================================================
// Constructor
// =============================================================================
BassEnhancerAudioProcessorEditor::BassEnhancerAudioProcessorEditor (
    BassEnhancerAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    // Plugin window size
    setSize (480, 300);

    // --- Set up the three sliders -------------------------------------------
    setupSlider (intensitySlider, "Intensity");
    setupSlider (mixSlider,       "Mix");
    setupSlider (focusSlider,     "Focus");

    // Bind sliders to APVTS
    intensityAttach = std::make_unique<SliderAttachment> (p.apvts, "intensity",
                                                          intensitySlider.slider);
    mixAttach       = std::make_unique<SliderAttachment> (p.apvts, "mix",
                                                          mixSlider.slider);
    focusAttach     = std::make_unique<SliderAttachment> (p.apvts, "focus",
                                                          focusSlider.slider);

    // Timer to refresh analysis meters at ~24 fps
    startTimerHz (24);
}

BassEnhancerAudioProcessorEditor::~BassEnhancerAudioProcessorEditor()
{
    stopTimer();
}

// =============================================================================
// Timer — pull analysis data from processor
// =============================================================================
void BassEnhancerAudioProcessorEditor::timerCallback()
{
    const auto& p = processorRef.getLatestDSPParams();

    // Smooth display values so bars move gracefully
    const float alpha  = 0.2f;
    displayHarmonic    = displayHarmonic    + alpha * (p.harmonicAmount    - displayHarmonic);
    displayCompression = displayCompression + alpha * (p.compressionAmount - displayCompression);
    displayTransient   = displayTransient   + alpha * (p.transientPreserve - displayTransient);

    repaint();
}

// =============================================================================
// Paint
// =============================================================================
void BassEnhancerAudioProcessorEditor::paint (juce::Graphics& g)
{
    // 'auto' (non-const) so we can call removeFromTop() which mutates the rect
    auto bounds = getLocalBounds();

    // Background
    g.fillAll (juce::Colour (kBackground));

    // Subtle gradient overlay
    juce::ColourGradient grad (juce::Colour (0xff1e1e3a), 0.f, 0.f,
                               juce::Colour (kBackground), 0.f, (float) getHeight(),
                               false);
    g.setGradientFill (grad);
    g.fillRect (bounds);

    // Title — FontOptions API (JUCE 8+)
    g.setFont (juce::FontOptions ("Helvetica Neue", 13.f, juce::Font::bold));
    g.setColour (juce::Colour (kAccent));
    g.drawText ("ADAPTIVE BASS ENHANCER",
                bounds.removeFromTop (28).reduced (12, 4),
                juce::Justification::centredLeft);

    // Thin separator
    g.setColour (juce::Colour (kDim));
    g.fillRect (bounds.removeFromTop (1));

    // Analysis bars at the bottom
    auto bottomStrip = getLocalBounds().removeFromBottom (56).reduced (16, 8);

    // Divider above analysis area
    g.setColour (juce::Colour (kDim));
    g.fillRect (bottomStrip.removeFromTop (1));
    bottomStrip.removeFromTop (4);

    auto barRow = bottomStrip;
    int  bw     = barRow.getWidth() / 3;

    drawAnalysisBar (g, "Harmonic",   displayHarmonic,    barRow.removeFromLeft (bw),
                     juce::Colour (kAccent));
    drawAnalysisBar (g, "Compress",   displayCompression, barRow.removeFromLeft (bw),
                     juce::Colour (0xff44eeaa));
    drawAnalysisBar (g, "Transient",  displayTransient,   barRow,
                     juce::Colour (kWarm));
}

// =============================================================================
// Resized
// =============================================================================
void BassEnhancerAudioProcessorEditor::resized()
{
    // Slider area: leave 28px title + 1px sep at top, 56px analysis at bottom
    auto area = getLocalBounds();
    area.removeFromTop  (29);
    area.removeFromBottom (56);
    area.reduce (8, 8);

    const int sliderW = area.getWidth() / 3;

    for (auto* ls : { &intensitySlider, &mixSlider, &focusSlider })
    {
        auto col = area.removeFromLeft (sliderW);
        ls->label.setBounds  (col.removeFromBottom (18));
        ls->slider.setBounds (col.reduced (4, 4));
    }
}

// =============================================================================
// Helpers
// =============================================================================
void BassEnhancerAudioProcessorEditor::setupSlider (LabelledSlider& ls,
                                                    const juce::String& labelText)
{
    ls.slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    ls.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);

    // Custom look-and-feel colour overrides
    ls.slider.setColour (juce::Slider::rotarySliderFillColourId,
                         juce::Colour (kAccent));
    ls.slider.setColour (juce::Slider::rotarySliderOutlineColourId,
                         juce::Colour (kDim));
    ls.slider.setColour (juce::Slider::thumbColourId,
                         juce::Colour (kWarm));
    ls.slider.setColour (juce::Slider::textBoxTextColourId,
                         juce::Colour (kText));
    ls.slider.setColour (juce::Slider::textBoxOutlineColourId,
                         juce::Colours::transparentBlack);
    ls.slider.setColour (juce::Slider::backgroundColourId,
                         juce::Colour (kBackground));

    addAndMakeVisible (ls.slider);

    ls.label.setText (labelText, juce::dontSendNotification);
    ls.label.setJustificationType (juce::Justification::centred);
    ls.label.setFont (juce::FontOptions ("Helvetica Neue", 11.f, juce::Font::plain));
    ls.label.setColour (juce::Label::textColourId, juce::Colour (kText));
    addAndMakeVisible (ls.label);
}

void BassEnhancerAudioProcessorEditor::drawAnalysisBar (
    juce::Graphics& g,
    const juce::String& label,
    float value,
    juce::Rectangle<int> area,
    juce::Colour barColour) const
{
    // Label
    g.setFont (juce::FontOptions ("Helvetica Neue", 10.f, juce::Font::plain));
    g.setColour (juce::Colour (kText).withAlpha (0.7f));
    g.drawText (label, area.removeFromTop (13), juce::Justification::centredLeft);

    // Track
    auto trackArea = area.removeFromTop (8).reduced (0, 1);
    g.setColour (juce::Colour (kDim));
    g.fillRoundedRectangle (trackArea.toFloat(), 3.f);

    // Fill
    auto fillArea = trackArea;
    fillArea.setWidth (juce::roundToInt (trackArea.getWidth() * value));
    g.setColour (barColour.withAlpha (0.85f));
    g.fillRoundedRectangle (fillArea.toFloat(), 3.f);
}
