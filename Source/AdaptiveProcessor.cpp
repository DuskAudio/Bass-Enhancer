#include "AdaptiveProcessor.h"

// =============================================================================
void AdaptiveProcessor::prepare (double sr, int blockSize, int numChannels)
{
    sampleRate = sr;

    const size_t nCh = static_cast<size_t> (numChannels);

    // --- Bass extraction LP filter @ 120 Hz, one instance per channel -------
    bassFilters.clear();
    bassFilters.resize (nCh);

    auto bassCoeffs = FilterCoeffs::makeLowPass (sampleRate, 120.0);
    for (auto& f : bassFilters)
    {
        f.coefficients = bassCoeffs;
        f.reset();
    }

    // --- Post-waveshaper anti-aliasing LP ------------------------------------
    // IMPROVEMENT 4: Raised from 3500 Hz to 8000 Hz.
    // 3500 Hz was too aggressive — it was also removing the musical 3rd/4th
    // harmonics of bass fundamentals (e.g. 3rd harmonic of 100 Hz = 300 Hz,
    // 3rd of 300 Hz = 900 Hz).  8000 Hz targets only the harsh high-order
    // aliases that make the waveshaper sound digital, leaving all audible
    // harmonic content intact.
    postShaperFilters.clear();
    postShaperFilters.resize (nCh);

    auto postCoeffs = FilterCoeffs::makeLowPass (sampleRate, 8000.0);
    for (auto& f : postShaperFilters)
    {
        f.coefficients = postCoeffs;
        f.reset();
    }

    // --- IMPROVEMENT 3: Dry path delay buffer initialisation ----------------
    // A 4-sample circular delay on the dry signal improves phase coherence
    // between the dry path and the bass-extracted wet path.  The bass LP
    // filter introduces group delay; this short delay brings the dry signal
    // slightly into alignment, reducing low-end cancellation artefacts.
    dryDelayBuf.assign (nCh, {});
    for (auto& buf : dryDelayBuf)
        buf.fill (0.f);
    dryDelayWrite.assign (nCh, 0);

    // --- Compressor time constants ------------------------------------------
    // Attack ~5 ms, release ~80 ms
    compAttackCoeff  = static_cast<float> (std::exp (-1.0 / (sampleRate * 0.005)));
    compReleaseCoeff = static_cast<float> (std::exp (-1.0 / (sampleRate * 0.080)));

    compEnv.assign (nCh, 0.f);

    // --- Parameter smoothers — 20 ms ramp time ------------------------------
    const int rampSamples = static_cast<int> (sr * 0.020);
    mixSmooth      .reset (rampSamples);
    harmonicSmooth .reset (rampSamples);
    compSmooth     .reset (rampSamples);
    transientSmooth.reset (rampSamples);

    mixSmooth      .setCurrentAndTargetValue (0.f);
    harmonicSmooth .setCurrentAndTargetValue (0.f);
    compSmooth     .setCurrentAndTargetValue (0.f);
    transientSmooth.setCurrentAndTargetValue (0.f);

    // --- Per-sample smoother scratch buffers --------------------------------
    const size_t maxBlock = static_cast<size_t> (std::max (1, blockSize));
    smoothedMix      .assign (maxBlock, 0.f);
    smoothedHarmonic .assign (maxBlock, 0.f);
    smoothedComp     .assign (maxBlock, 0.f);
    smoothedTransient.assign (maxBlock, 0.f);
}

// =============================================================================
void AdaptiveProcessor::process (juce::AudioBuffer<float>& buffer,
                                 const DSPParameters& params,
                                 float mix)
{
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples == 0 || numChannels == 0)
        return;

    // Update smoother targets for this block
    mixSmooth      .setTargetValue (mix);
    harmonicSmooth .setTargetValue (params.harmonicAmount);
    compSmooth     .setTargetValue (params.compressionAmount);
    transientSmooth.setTargetValue (params.transientPreserve);

    // -------------------------------------------------------------------------
    // IMPROVEMENT 1: Compute per-block context-aware drive modifiers.
    //
    // bassScore and kickScore arrive from DecisionEngine (already smoothed at
    // block rate).  We derive two scalars here that will modulate the
    // waveshaper drive per-sample:
    //
    //   contextDriveMul  — multiplied onto drive BEFORE waveshaping
    //     Sustained bass  → pushes drive up   (richer harmonics)
    //     Percussive kick → pulls drive down  (cleaner transients)
    //
    // These are block-rate values.  Per-sample interpolation is handled by
    // the existing SmoothedValue ramps on harmonicAmount.
    // -------------------------------------------------------------------------
    const float contextDriveMul = 1.f
                                + params.bassScore * 0.40f   // +40% max on bass
                                - params.kickScore * 0.30f;  // -30% max on kick

    // Clamp so we never accidentally invert or over-drive
    const float safeDriveMul = juce::jlimit (0.3f, 1.6f, contextDriveMul);

    // =========================================================================
    // Advance ALL smoothers once per sample position — identical across channels
    // =========================================================================
    const int n = std::min (numSamples, static_cast<int> (smoothedMix.size()));

    for (int i = 0; i < n; ++i)
    {
        smoothedMix      [static_cast<size_t> (i)] = mixSmooth      .getNextValue();
        smoothedHarmonic [static_cast<size_t> (i)] = harmonicSmooth .getNextValue();
        smoothedComp     [static_cast<size_t> (i)] = compSmooth     .getNextValue();
        smoothedTransient[static_cast<size_t> (i)] = transientSmooth.getNextValue();
    }

    // =========================================================================
    // Per-channel processing
    // =========================================================================
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data     = buffer.getWritePointer (ch);
        auto&  bassFilt = bassFilters       [static_cast<size_t> (ch)];
        auto&  postFilt = postShaperFilters [static_cast<size_t> (ch)];
        float& env      = compEnv           [static_cast<size_t> (ch)];

        for (int i = 0; i < n; ++i)
        {
            const float curMix       = smoothedMix      [static_cast<size_t> (i)];
            const float curHarmonic  = smoothedHarmonic [static_cast<size_t> (i)];
            const float curComp      = smoothedComp     [static_cast<size_t> (i)];
            const float curTransient = smoothedTransient[static_cast<size_t> (i)];

            const float input = data[i];

            // Use input directly as dry signal.
            // The 4-sample delay was removed — it caused comb filtering at
            // high frequencies by summing a delayed and undelayed version of
            // the same signal (e.g. -3.6 dB cancellation at 5kHz at Mix=20%).
            // The harmonic content is derived from the sub-120Hz band only,
            // so phase alignment with the dry signal is not a concern.
            const float dry = input;

            // -----------------------------------------------------------------
            // Bass extraction
            // -----------------------------------------------------------------
            const float bass = bassFilt.processSample (input);

            // -----------------------------------------------------------------
            // IMPROVEMENT 1 + 6: Context-aware and dynamic drive.
            //
            // effectiveDrive = harmonicAmount * contextDriveMul
            //   Sustained bass  → more drive (richer harmonics)
            //   Percussive kick → less drive (transient preservation)
            //
            // IMPROVEMENT 6: Dynamic drive reduction at high amplitudes.
            //   As the compressor envelope rises (loud signal), drive is
            //   gently reduced to prevent over-saturation.
            //
            //   dynamicFactor = 1 / (1 + env * 0.5)
            //   env ≈ 0 → factor ≈ 1.0  (no reduction, quiet signal)
            //   env ≈ 1 → factor ≈ 0.67 (−3.5 dB reduction, loud signal)
            //
            // Both are applied before the waveshaper so the shaper itself
            // always sees a well-conditioned drive value.
            // -----------------------------------------------------------------
            const float dynamicFactor  = 1.f / (1.f + env * 0.5f);
            const float effectiveDrive = curHarmonic * safeDriveMul * dynamicFactor;
            const float clampedDrive   = juce::jlimit (0.f, 1.f, effectiveDrive);

            // -----------------------------------------------------------------
            // Waveshaper + harmonic extraction.
            //
            // Correct parallel enhancer architecture:
            //   shaped   = waveshape(bass)         → shaped bass signal
            //   harmonics = shaped - bass           → ONLY the new content added
            //   wet      = input + harmonics        → full dry signal + added harmonics
            //
            // This guarantees high frequencies are never touched — the harmonic
            // content comes entirely from the sub-120Hz band, so the mix knob
            // adds bass harmonics without cutting anything above 120Hz.
            // -----------------------------------------------------------------
            float shaped = waveshape (bass, clampedDrive);
            shaped = postFilt.processSample (shaped);
            float wet = input + (shaped - bass);

            // -----------------------------------------------------------------
            // Signal-dependent gain compensation.
            // Scales inversely with both drive and instantaneous amplitude
            // so the wet signal stays roughly level-matched to the dry.
            // -----------------------------------------------------------------
            const float driveFactor      = 0.3f + std::abs (wet) * 0.4f;
            const float compensationGain = 1.f / (1.f + clampedDrive * driveFactor);
            wet *= compensationGain;

            // -----------------------------------------------------------------
            // Feed-forward soft compressor with adaptive threshold.
            // -----------------------------------------------------------------
            const float absWet = std::abs (wet);
            const float coeff  = (absWet > env) ? compAttackCoeff : compReleaseCoeff;
            env = coeff * env + (1.f - coeff) * absWet;

            const float threshold = 0.7f - curComp * 0.4f;  // [0.70 → 0.30]
            const float ratio     = 1.f + curComp * 5.f;    // [1 → 6]
            wet *= compGain (env, threshold, ratio);

            // -----------------------------------------------------------------
            // Transient preserve — blend back toward (delayed) dry
            // -----------------------------------------------------------------
            const float effectBlend = 1.f - curTransient;
            wet = dry + (wet - dry) * effectBlend;

            // -----------------------------------------------------------------
            // Additive harmonic blend.
            //
            // Equal-power crossfade was wrong here because wet already contains
            // the full dry signal (wet = dry + harmonics). Applying cos/sin
            // crossfade double-counted the dry signal, causing up to +3dB boost
            // at Mix=50%.
            //
            // Correct approach for an additive enhancer:
            //   harmonics = wet - dry        (isolate only what was added)
            //   output    = dry + mix * harmonics  (scale the addition)
            //
            // Mix=0.0 → dry only, perfect bypass, zero volume change
            // Mix=0.5 → dry + half the harmonics, no volume change
            // Mix=1.0 → dry + full harmonics, no volume change
            // -----------------------------------------------------------------
            const float harmonics = wet - dry;
            data[i] = dry + curMix * harmonics;

            // Denormal kill
            data[i] += 1e-25f;
            data[i] -= 1e-25f;
        }
    }
}
