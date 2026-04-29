#include "DecisionEngine.h"

// =============================================================================
void DecisionEngine::prepare (double /*sampleRate*/, int /*blockSize*/)
{
    // FIX 4: Parameter smoothing with a fixed one-pole coefficient.
    //
    // The spec requires:  smoothed = 0.9 * previous + 0.1 * new
    // This is a one-pole IIR with α = 0.9.  At a typical 44.1 kHz / 512
    // block size (~86 calls/sec) this gives a -3dB time constant of roughly
    // 110 ms — long enough to prevent zipper noise, short enough to track
    // musical events.  A fixed coefficient is also host-agnostic (no need to
    // recompute when sample rate or block size change).
    smoothCoeff = 0.9f;

    smoothed = {};
    target   = {};
}

// =============================================================================
void DecisionEngine::update (const AudioFeatures& f, float intensity, float focus)
{
    // -------------------------------------------------------------------------
    // 1. Percussiveness score [0, 1]
    //    High when: crest factor large + attack fast + ZCR high.
    //    Crest factor is now clamped to [0, 10] (Fix 2), so we normalise
    //    over that range.
    // -------------------------------------------------------------------------
    const float crestNorm = juce::jlimit (0.f, 1.f, f.crestFactor / 10.f);
    const float percScore = juce::jlimit (0.f, 1.f,
                                          crestNorm        * 0.5f
                                        + f.attackStrength * 0.3f
                                        + f.zcr            * 0.2f);

    // -------------------------------------------------------------------------
    // 2. Sustained bass score [0, 1]
    //
    //    FIX 7: Focus parameter is now used as a proper interpolation weight
    //    between subEnergy and lowMidEnergy inside the score itself.
    //
    //      focus = 0.0 → processing is entirely driven by sub energy (20–80 Hz)
    //      focus = 0.5 → equal weighting (balanced)
    //      focus = 1.0 → processing is entirely driven by low-mid energy (80–200 Hz)
    //
    //    This means Focus genuinely shifts which frequency band activates the
    //    enhancement, rather than just adding a small bonus after the fact.
    // -------------------------------------------------------------------------
    const float focusedEnergy = (1.f - focus) * f.subEnergy
                               +        focus  * f.lowMidEnergy;

    const float sustainedScore = juce::jlimit (0.f, 1.f,
                                               (1.f - f.zcr)    * 0.4f
                                             + (1.f - crestNorm) * 0.3f
                                             + focusedEnergy      * 0.3f);

    // -------------------------------------------------------------------------
    // 3. Low-mid saturation penalty — avoid adding mud to an already dense mix.
    //    Only applied when focus is NOT already pointing at low-mid territory,
    //    because in that mode the user has intentionally asked for it.
    // -------------------------------------------------------------------------
    const float mudWeight  = 1.f - focus * 0.5f;   // full penalty at focus=0, halved at focus=1
    const float mudPenalty = juce::jlimit (0.f, 0.6f, f.lowMidEnergy * 0.8f * mudWeight);

    // -------------------------------------------------------------------------
    // 4. Derive raw DSP targets
    // -------------------------------------------------------------------------

    // Harmonic amount: boosted by focused sustained energy, suppressed by perc & mud
    float rawHarmonic = sustainedScore * 0.9f
                      - percScore      * 0.5f
                      - mudPenalty;
    rawHarmonic = juce::jlimit (0.f, 1.f, rawHarmonic);

    // -------------------------------------------------------------------------
    // IMPROVEMENT 5: Non-linear (power-law) response curve for harmonicAmount.
    //
    // A linear mapping makes the control feel too aggressive at low settings
    // and leaves the upper range compressed into a small knob arc.
    // Applying pow(x, 1.3) curves the lower portion downward so gentle
    // settings are truly subtle, while the middle range opens up as a
    // wider, more usable "sweet spot".
    //
    //   rawHarmonic = 0.0 → 0.00  (no change at zero)
    //   rawHarmonic = 0.3 → 0.22  (softer at low settings)
    //   rawHarmonic = 0.6 → 0.50  (natural mid-range feel)
    //   rawHarmonic = 1.0 → 1.00  (full drive unchanged at ceiling)
    // -------------------------------------------------------------------------
    rawHarmonic = std::pow (rawHarmonic, 1.3f);

    // Compression: follows sustained content; backed off on transients
    float rawCompression = sustainedScore * 0.7f
                         - percScore      * 0.4f;
    rawCompression = juce::jlimit (0.f, 1.f, rawCompression);

    // Transient preserve: highest on percussive material
    float rawTransient = percScore * 0.95f;
    rawTransient = juce::jlimit (0.f, 1.f, rawTransient);

    // -------------------------------------------------------------------------
    // 5. Apply user intensity — scales harmonic and compression.
    //    transientPreserve is intentionally NOT scaled: we always protect transients
    //    regardless of how high the user sets Intensity.
    // -------------------------------------------------------------------------
    target.harmonicAmount    = rawHarmonic    * intensity;
    target.compressionAmount = rawCompression * intensity;
    target.transientPreserve = rawTransient;

    // -------------------------------------------------------------------------
    // IMPROVEMENT 1: Expose context scores so AdaptiveProcessor can adapt
    // waveshaper character to the signal type without duplicating analysis.
    //
    // These are block-rate values — the AdaptiveProcessor's own per-sample
    // SmoothedValues handle intra-block interpolation on top of these.
    // -------------------------------------------------------------------------
    target.bassScore = sustainedScore;
    target.kickScore = percScore;

    // -------------------------------------------------------------------------
    // 6. FIX 4: One-pole smoothing with α = 0.9
    //
    //    smoothed = 0.9 * smoothed + 0.1 * target
    //
    //    Applied to ALL parameters so no abrupt jumps reach AdaptiveProcessor.
    //    bassScore and kickScore use a slightly faster coefficient (0.7) since
    //    they drive waveshaper character — tighter tracking feels more musical.
    // -------------------------------------------------------------------------
    smoothed.harmonicAmount    = smooth (smoothed.harmonicAmount,    target.harmonicAmount,    smoothCoeff);
    smoothed.compressionAmount = smooth (smoothed.compressionAmount, target.compressionAmount, smoothCoeff);
    smoothed.transientPreserve = smooth (smoothed.transientPreserve, target.transientPreserve, smoothCoeff);
    smoothed.bassScore         = smooth (smoothed.bassScore,         target.bassScore,         0.7f);
    smoothed.kickScore         = smooth (smoothed.kickScore,         target.kickScore,         0.7f);
}
