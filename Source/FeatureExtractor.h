#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

/**
 * FeatureExtractor
 *
 * Analyses incoming audio on a per-block basis using lightweight IIR filters.
 * Computes perceptual features consumed by the DecisionEngine.
 *
 * All analysis is performed on a mono sum of the input.
 *
 * ALLOCATION POLICY
 * -----------------
 * No heap allocations occur inside process(). All working buffers are
 * pre-allocated in prepare() and reused every block.
 */
struct AudioFeatures
{
    float envelope       = 0.f;  ///< Smoothed amplitude envelope [0, 1]
    float attackStrength = 0.f;  ///< Relative envelope rise this block [0, 1]
    float subEnergy      = 0.f;  ///< Sub band RATIO: rawSub / (rawSub + rawLowMid) [0, 1]
    float lowMidEnergy   = 0.f;  ///< Low-mid band RATIO: rawLowMid / (rawSub + rawLowMid) [0, 1]
    float crestFactor    = 1.f;  ///< Peak / RMS (clamped [0, 10]; high = percussive)
    float zcr            = 0.f;  ///< Zero Crossing Rate, normalised by (n-1) [0, 1]
};

class FeatureExtractor
{
public:
    FeatureExtractor() = default;

    /** Call once on initialisation or when sampleRate / block size changes. */
    void prepare (double sampleRate, int maxBlockSize);

    /**
     * Analyse a stereo (or mono) buffer.
     * All features are derived from the mono sum.
     * Must be called from the audio thread only.
     */
    void process (const juce::AudioBuffer<float>& buffer);

    /** Returns the most recently computed features (audio thread only). */
    const AudioFeatures& getFeatures() const noexcept { return features; }

private:
    // -----------------------------------------------------------------------
    // IIR filter instances
    // -----------------------------------------------------------------------
    using FilterCoeffs = juce::dsp::IIR::Coefficients<float>;
    using IIRFilter    = juce::dsp::IIR::Filter<float>;

    IIRFilter subFilter;       ///< Low-pass  @ 80 Hz   → sub band
    IIRFilter lowMidFilterHP;  ///< High-pass @ 80 Hz   |
    IIRFilter lowMidFilterLP;  ///< Low-pass  @ 200 Hz  | → low-mid band

    // -----------------------------------------------------------------------
    // Pre-allocated work buffers (avoid audio-thread heap allocation)
    // -----------------------------------------------------------------------
    std::vector<float> monoBuf;    ///< Mono sum of input channels
    std::vector<float> subBuf;     ///< Sub-band signal (copy of mono, then filtered)
    std::vector<float> lowMidBuf;  ///< Low-mid signal (copy of mono, then filtered)

    // -----------------------------------------------------------------------
    // Envelope follower state
    // -----------------------------------------------------------------------
    float envSmoothed = 0.f;
    float attackCoeff = 0.f;  ///< One-pole coefficient for fast (attack) path
    float decayCoeff  = 0.f;  ///< One-pole coefficient for slow  (decay)  path

    AudioFeatures features;

    // -----------------------------------------------------------------------
    // Static helpers — operate on raw pointers, zero allocation
    // -----------------------------------------------------------------------
    static float computeRMS  (const float* data, int n) noexcept;
    static float computePeak (const float* data, int n) noexcept;
    static float computeZCR  (const float* data, int n) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FeatureExtractor)
};
