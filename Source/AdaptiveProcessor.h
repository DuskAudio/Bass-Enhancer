#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DecisionEngine.h"

/**
 * AdaptiveProcessor
 *
 * Applies perceptual bass enhancement to the audio using DSPParameters.
 *
 * Signal chain per channel:
 *
 *   Input
 *     |
 *     |---- Dry path --> [4-sample delay] --------------------------------|
 *     |                                                                   |
 *     `---- Bass extract (LP ~120 Hz)                                     |
 *                |                                                        |
 *           Musical waveshaper (tanh + asymmetry + cubic blend)           |
 *                |    ^ drive modulated by bassScore/kickScore             |
 *                |    ^ dynamic drive reduction at high amplitudes         |
 *                |                                                        |
 *           Post-shaper LP (~8000 Hz)  [alias reduction]                 |
 *                |                                                        |
 *           Soft compressor (adaptive threshold)                          |
 *                |                                                        |
 *           Transient preserve blend                                      |
 *                `----------------------------------------------------->  |
 *                                                             Equal-power Sum
 *                                                                    |
 *                                                                 Output
 *
 * Waveshaper model:
 *   tanhPart = tanh(d*x) / tanh(d)                  [normalised symmetric]
 *   asymPart = tanhPart + 0.08 * x^2 * sign(x)      [adds 2nd harmonic, warm]
 *   output   = lerp(tanhPart, asymPart, 0.3)         [30% analog character]
 *
 * Dry path delay (4 samples) approximates the group delay of the bass
 * extraction LP filter at very low frequencies, reducing comb-filtering
 * when dry and wet signals are summed.
 */
class AdaptiveProcessor
{
public:
    AdaptiveProcessor() = default;

    /** Call on plugin initialisation / sample rate change. */
    void prepare (double sampleRate, int blockSize, int numChannels);

    /**
     * Process the buffer in place.
     *
     * @param buffer  Stereo (or mono) audio buffer — modified in place
     * @param params  Smoothed parameters from DecisionEngine
     * @param mix     User "Mix" dry/wet control [0, 1]
     */
    void process (juce::AudioBuffer<float>& buffer,
                  const DSPParameters& params,
                  float mix);

private:
    // --- Bass extraction filter (one per channel) ---------------------------
    using FilterCoeffs = juce::dsp::IIR::Coefficients<float>;
    using IIRFilter    = juce::dsp::IIR::Filter<float>;
    std::vector<IIRFilter> bassFilters;

    // --- Post-waveshaper low-pass filter (one per channel) ------------------
    // Raised to 8000 Hz (was 3500 Hz) — removes harsh digital aliases while
    // keeping the musical 2nd/3rd harmonic content that lives below ~1 kHz.
    std::vector<IIRFilter> postShaperFilters;

    // --- IMPROVEMENT 3: Dry path delay buffer for phase coherence -----------
    // A short circular delay on the dry signal approximates the group delay
    // of the bass extraction LP filter, reducing comb-filter cancellation
    // in the low end when dry and wet are summed together.
    // kDryDelayLen must be a power-of-two for the bitmask wrap trick.
    static constexpr int kDryDelayLen = 8;   // 8 samples — power of 2
    static constexpr int kDryDelaySamples = 4; // delay amount (< kDryDelayLen)
    std::vector<std::array<float, kDryDelayLen>> dryDelayBuf;
    std::vector<int> dryDelayWrite;   // write-head per channel

    // --- Soft compressor envelope per channel --------------------------------
    std::vector<float> compEnv;
    float compAttackCoeff  = 0.f;
    float compReleaseCoeff = 0.f;

    double sampleRate = 44100.0;

    // --- Parameter smoothers (one ramp per parameter) -----------------------
    juce::SmoothedValue<float> mixSmooth;
    juce::SmoothedValue<float> harmonicSmooth;
    juce::SmoothedValue<float> compSmooth;
    juce::SmoothedValue<float> transientSmooth;

    // --- Per-sample smoother scratch buffers --------------------------------
    // Smoothed values are advanced ONCE per sample position (outside the
    // channel loop) and stored here. All channels read identically.
    std::vector<float> smoothedMix;
    std::vector<float> smoothedHarmonic;
    std::vector<float> smoothedComp;
    std::vector<float> smoothedTransient;

    // -------------------------------------------------------------------------
    // Waveshaper: tanh soft saturation with musical asymmetric character.
    //
    // FIXED: At drive=0 the function now returns exactly x (true bypass).
    // Previously d=1+drive*5 meant d=1 at drive=0, giving tanh(x)/tanh(1)
    // which has +2.37dB gain even at zero drive — now guarded with an early
    // return so (shaped - bass) = 0 exactly when Intensity = 0%.
    //
    // drive [0,1] → internal factor d [0,6]
    //   tanhPart = tanh(d*x) / tanh(d)       normalised symmetric (odd harmonics)
    //   asymBend = x*|x|                      generates 2nd harmonic (warm/even)
    //   asymPart = tanhPart + 0.08*asymBend   asymmetric blend
    //   output   = 0.7*tanhPart + 0.3*asymPart
    // -------------------------------------------------------------------------
    static float waveshape (float x, float drive) noexcept
    {
        // True bypass at zero drive — no gain, no colouration
        if (drive < 1e-4f) return x;

        const float d  = drive * 6.f;            // [0, 6] — zero at zero drive
        const float td = std::tanh (d);
        if (td < 1e-6f) return x;

        const float tanhPart = std::tanh (d * x) / td;
        const float asymBend = x * std::abs (x);
        const float asymPart = tanhPart + 0.08f * asymBend;

        return tanhPart * 0.7f + asymPart * 0.3f;
    }

    // -------------------------------------------------------------------------
    // Feed-forward compressor gain.
    // Returns a gain factor in (0, 1] that reduces level above threshold.
    // -------------------------------------------------------------------------
    static float compGain (float envValue, float threshold, float ratio) noexcept
    {
        if (envValue <= threshold || threshold <= 0.f)
            return 1.f;

        const float over      = envValue - threshold;
        const float reduction = over * (1.f - (1.f / ratio));
        const float target    = envValue - reduction;
        return juce::jlimit (0.1f, 1.f, target / envValue);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AdaptiveProcessor)
};
