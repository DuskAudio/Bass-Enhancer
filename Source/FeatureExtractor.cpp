#include "FeatureExtractor.h"

// =============================================================================
void FeatureExtractor::prepare (double sampleRate, int maxBlockSize)
{
    // --- Pre-allocate work buffers -------------------------------------------
    monoBuf   .assign (static_cast<size_t> (maxBlockSize), 0.f);
    subBuf    .assign (static_cast<size_t> (maxBlockSize), 0.f);
    lowMidBuf .assign (static_cast<size_t> (maxBlockSize), 0.f);

    // --- Sub filter: Butterworth LP @ 80 Hz ----------------------------------
    {
        auto coeffs = FilterCoeffs::makeLowPass (sampleRate, 80.0);
        subFilter.coefficients = coeffs;
        subFilter.reset();
    }

    // --- Low-mid bandpass: HP @ 80 Hz then LP @ 200 Hz ----------------------
    {
        auto hpCoeffs = FilterCoeffs::makeHighPass (sampleRate, 80.0);
        lowMidFilterHP.coefficients = hpCoeffs;
        lowMidFilterHP.reset();

        auto lpCoeffs = FilterCoeffs::makeLowPass (sampleRate, 200.0);
        lowMidFilterLP.coefficients = lpCoeffs;
        lowMidFilterLP.reset();
    }

    // --- Envelope follower time constants ------------------------------------
    // FIX 6 VERIFICATION: Coefficients are computed from sampleRate here in
    // prepare(), which is called whenever the host changes the sample rate.
    // They are NEVER hardcoded or computed at a fixed rate — confirmed safe.
    //
    // Formula: coeff = exp(-1 / (sampleRate * timeInSeconds))
    //   attackCoeff at 44100 Hz → exp(-1 / 220.5)  ≈ 0.9955  (5  ms, fast)
    //   decayCoeff  at 44100 Hz → exp(-1 / 4410.0) ≈ 0.9998  (100 ms, slow)
    //   At 96000 Hz the values update automatically — no fix required.
    const float sr = static_cast<float> (sampleRate);
    attackCoeff = std::exp (-1.f / (sr * 0.005f));   // 5 ms attack
    decayCoeff  = std::exp (-1.f / (sr * 0.100f));   // 100 ms decay

    envSmoothed = 0.f;
    features    = {};
}

// =============================================================================
void FeatureExtractor::process (const juce::AudioBuffer<float>& buffer)
{
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples == 0 || numChannels == 0)
        return;

    // Safety: clamp to pre-allocated size (should never differ in a correct host)
    const int n = std::min (numSamples, static_cast<int> (monoBuf.size()));

    // -------------------------------------------------------------------------
    // 1. Build mono sum into pre-allocated buffer
    // -------------------------------------------------------------------------
    const float chanGain = 1.f / static_cast<float> (numChannels);

    // Start with scaled first channel
    const float* ch0 = buffer.getReadPointer (0);
    for (int i = 0; i < n; ++i)
        monoBuf[i] = ch0[i] * chanGain;

    // Accumulate remaining channels
    for (int ch = 1; ch < numChannels; ++ch)
    {
        const float* chData = buffer.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
            monoBuf[i] += chData[i] * chanGain;
    }

    // -------------------------------------------------------------------------
    // 2. RMS, Peak, Crest Factor
    // -------------------------------------------------------------------------
    const float rms  = computeRMS  (monoBuf.data(), n);
    const float peak = computePeak (monoBuf.data(), n);

    // FIX 2: Stable crest factor.
    // Adding epsilon to the denominator prevents division-by-zero and wild
    // swings on near-silence.  Clamped to [0, 10] — values above 10 carry no
    // additional perceptual meaning and would distort the decision engine.
    features.crestFactor = juce::jlimit (0.f, 10.f, peak / (rms + 1e-6f));

    // -------------------------------------------------------------------------
    // 3. Envelope follower
    //
    //    Standard one-pole design with separate attack and decay coefficients:
    //
    //      if |x| > env  →  env = attackCoeff * env + (1 - attackCoeff) * |x|
    //      else          →  env = decayCoeff  * env + (1 - decayCoeff)  * |x|
    //
    //    attackCoeff is close to 0 (fast, ~5 ms)   → env rises quickly
    //    decayCoeff  is close to 1 (slow, ~100 ms)  → env falls slowly
    //
    //    FIX 3: The coefficient formula and branching are already correct.
    //    We also snapshot the pre-block envelope to derive attackStrength,
    //    which measures how much the envelope rose this block relative to
    //    its own level — a reliable transient indicator.
    // -------------------------------------------------------------------------
    const float envBeforeBlock = envSmoothed;

    for (int i = 0; i < n; ++i)
    {
        const float absVal = std::abs (monoBuf[i]);
        // Attack path: fast rise; decay path: slow fall
        const float coeff  = (absVal > envSmoothed) ? attackCoeff : decayCoeff;
        envSmoothed        = coeff * envSmoothed + (1.f - coeff) * absVal;
    }

    features.envelope = envSmoothed;

    // attackStrength: normalised envelope rise this block [0, 1].
    // Positive only — a falling envelope gives 0, not negative.
    const float envRise = envSmoothed - envBeforeBlock;
    features.attackStrength = (envSmoothed > 1e-6f)
                                  ? juce::jlimit (0.f, 1.f, envRise / envSmoothed)
                                  : 0.f;

    // -------------------------------------------------------------------------
    // 4. Sub-band energy (LP @ 80 Hz) — filter the pre-allocated subBuf
    // -------------------------------------------------------------------------
    for (int i = 0; i < n; ++i)
        subBuf[i] = monoBuf[i];  // copy first, then filter in-place

    for (int i = 0; i < n; ++i)
        subBuf[i] = subFilter.processSample (subBuf[i]);

    const float rawSub = computeRMS (subBuf.data(), n);

    // -------------------------------------------------------------------------
    // 5. Low-mid energy (HP 80 Hz → LP 200 Hz)
    // -------------------------------------------------------------------------
    for (int i = 0; i < n; ++i)
        lowMidBuf[i] = monoBuf[i];

    for (int i = 0; i < n; ++i)
    {
        lowMidBuf[i] = lowMidFilterHP.processSample (lowMidBuf[i]);
        lowMidBuf[i] = lowMidFilterLP.processSample (lowMidBuf[i]);
    }

    const float rawLowMid = computeRMS (lowMidBuf.data(), n);

    // -------------------------------------------------------------------------
    // FIX 3: Ratio-based normalisation of sub vs low-mid energy.
    //
    // The old approach multiplied raw RMS by an arbitrary factor of 4 and
    // clamped to [0,1].  This was input-level dependent: a quiet signal always
    // read near zero regardless of its spectral content, so the DecisionEngine
    // would fail to activate on quietly-played bass.
    //
    // Instead we compute the RATIO of each band to their combined energy:
    //
    //   normalizedSub    = rawSub    / (rawSub + rawLowMid + epsilon)
    //   normalizedLowMid = rawLowMid / (rawSub + rawLowMid + epsilon)
    //
    // Properties:
    //   - Both values are always in [0, 1] — no clamping needed.
    //   - They represent spectral BALANCE, not absolute level.
    //   - A signal that is 90% sub content reads subEnergy ≈ 0.9
    //     regardless of whether it is loud or quiet.
    //   - Epsilon prevents NaN on silence.
    // -------------------------------------------------------------------------
    constexpr float kEps = 1e-8f;
    const float bandSum  = rawSub + rawLowMid + kEps;

    features.subEnergy    = rawSub    / bandSum;
    features.lowMidEnergy = rawLowMid / bandSum;

    // -------------------------------------------------------------------------
    // 6. Zero Crossing Rate
    // -------------------------------------------------------------------------
    features.zcr = computeZCR (monoBuf.data(), n);
}

// =============================================================================
// Static helpers
// =============================================================================

float FeatureExtractor::computeRMS (const float* data, int n) noexcept
{
    if (n <= 0) return 0.f;
    float sum = 0.f;
    for (int i = 0; i < n; ++i)
        sum += data[i] * data[i];
    return std::sqrt (sum / static_cast<float> (n));
}

float FeatureExtractor::computePeak (const float* data, int n) noexcept
{
    float peak = 0.f;
    for (int i = 0; i < n; ++i)
        peak = std::max (peak, std::abs (data[i]));
    return peak;
}

float FeatureExtractor::computeZCR (const float* data, int n) noexcept
{
    // FIX 1: Normalised Zero Crossing Rate.
    //
    // We count sign changes between adjacent samples and divide by (n - 1),
    // the maximum number of crossings possible.  This gives a value in [0, 1]
    // that is INDEPENDENT of block size — a 64-sample block and a 512-sample
    // block of the same signal produce the same ZCR reading.
    //
    // A 1 kHz sine at 44.1 kHz has a theoretical ZCR of ~0.045.
    // A 10 kHz sine has ZCR of ~0.45.
    // Sub-bass content (< 80 Hz) typically produces ZCR < 0.004.

    if (n <= 1) return 0.f;

    int crossings = 0;
    for (int i = 1; i < n; ++i)
        if ((data[i] >= 0.f) != (data[i - 1] >= 0.f))
            ++crossings;

    return static_cast<float> (crossings) / static_cast<float> (n - 1);
}
