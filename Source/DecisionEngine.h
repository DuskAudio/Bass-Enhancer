#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "FeatureExtractor.h"

/**
 * DSPParameters
 *
 * Output of the DecisionEngine.  All values are in [0, 1].
 *
 *  harmonicAmount    – how strongly to drive the waveshaper
 *  compressionAmount – soft-knee gain-reduction depth
 *  transientPreserve – blend toward dry signal on transients
 *
 *  -- IMPROVEMENT 1: Context scores for adaptive harmonic generation --
 *  bassScore  – sustained bass likelihood (high = richer harmonics)
 *  kickScore  – percussive transient likelihood (high = gentler drive)
 *
 *  These are passed straight from DecisionEngine's internal scoring so
 *  AdaptiveProcessor can modulate waveshaper character per-sample without
 *  needing to know anything about the feature analysis.
 */
struct DSPParameters
{
    float harmonicAmount    = 0.5f;
    float compressionAmount = 0.3f;
    float transientPreserve = 0.0f;

    // Context scores — read-only inputs to AdaptiveProcessor
    float bassScore = 0.0f;   ///< sustainedScore from DecisionEngine [0, 1]
    float kickScore = 0.0f;   ///< percScore from DecisionEngine      [0, 1]
};

/**
 * DecisionEngine
 *
 * Maps real-time AudioFeatures + user parameter values onto DSPParameters.
 *
 * Rules (all blended smoothly, never stepped):
 *
 *  PERCUSSIVE  (high crest factor AND fast attack AND high ZCR)
 *      → transientPreserve ↑, harmonicAmount ↓, compressionAmount ↓
 *
 *  SUSTAINED BASS  (low ZCR AND low-ish crest factor AND sub energy present)
 *      → harmonicAmount ↑, compressionAmount ↑, transientPreserve ↓
 *
 *  LOW-MID ALREADY HOT  (lowMidEnergy high)
 *      → harmonicAmount ↓  (avoid mud)
 *
 * User parameters scale the output without overriding the adaptive logic.
 */
class DecisionEngine
{
public:
    DecisionEngine() = default;

    /** Call when sample rate / block size changes. */
    void prepare (double sampleRate, int blockSize);

    /**
     * Update internal targets from features and user controls, then smooth
     * toward those targets.  Call once per processBlock.
     *
     * @param features   Output from FeatureExtractor
     * @param intensity  User "Intensity" parameter [0,1]
     * @param focus      User "Focus" parameter [0,1]  (0 = sub, 1 = low-mid)
     */
    void update (const AudioFeatures& features, float intensity, float focus);

    /** Retrieve smoothed parameters ready for the AdaptiveProcessor. */
    const DSPParameters& getParameters() const noexcept { return smoothed; }

private:
    DSPParameters target;   // Where we want to be
    DSPParameters smoothed; // What we actually output (first-order LP)

    // Smoothing coefficient — ~50 ms time constant
    float smoothCoeff = 0.f;

    /** Simple one-pole smooth toward a target. */
    static float smooth (float current, float target, float coeff) noexcept
    {
        return current + (1.f - coeff) * (target - current);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DecisionEngine)
};
