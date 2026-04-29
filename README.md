Here's what the plugin actually does, end to end:

**Analysis (FeatureExtractor — runs every audio block)**
	•	Sums stereo input to mono for analysis
	•	Tracks a smoothed amplitude envelope with fast attack (5ms) and slow decay (100ms)
	•	Measures how quickly the envelope rose this block — used as a transient detector
	•	Splits the signal into two bands via IIR filters: sub (20–80 Hz) and low-mid (80–200 Hz)
	•	Computes the ratio between those two bands — so a signal that's 90% sub reads as 0.9 regardless of volume
	•	Computes Crest Factor (peak ÷ RMS) — high values mean percussive/spiky content
	•	Computes Zero Crossing Rate — low-frequency content crosses zero rarely, high-frequency content crosses often
	
**Decision Making (DecisionEngine — runs every audio block)**
	•	Combines those features into a "percussive score" (kick drums, transients) and a "sustained bass score" (bass guitar, sine subs)
	•	The Focus knob shifts which band drives the sustained score — low Focus = sub band, high Focus = low-mid band
	•	Applies a mud penalty — if the low-mid band is already dense, it backs off processing automatically
	•	Applies a power-law curve to harmonic amount so the control feels natural rather than too aggressive at low settings
	•	Smooths all output parameters with a one-pole filter so nothing changes abruptly between blocks
	•	Outputs five values: harmonicAmount, compressionAmount, transientPreserve, bassScore, kickScore
	
**Processing (AdaptiveProcessor — runs every sample)**
	•	Delays the dry signal by 4 samples to improve phase alignment when dry and wet are summed
	•	Extracts bass content via a 120 Hz low-pass filter
	•	Scales the waveshaper drive up when bassScore is high (+40% max) and down when kickScore is high (−30% max)
	•	Reduces drive further when the signal is already loud — prevents over-saturation on loud hits
	•	Runs that drive-adjusted bass signal through a musical waveshaper: 70% symmetric tanh (odd harmonics) blended with 30% asymmetric tanh (adds 2nd harmonic for analog warmth)
	•	Blends in a lightly shaped full-band signal (at 35% of the drive) to add subtle upper harmonic presence
	•	Runs the result through a 8 kHz low-pass filter to remove harsh high-frequency aliases from the waveshaper
	•	Applies level compensation inversely proportional to both drive and signal amplitude — louder/harder driven signals get pulled back more
	•	Runs a soft feed-forward compressor with an adaptive threshold — low compression setting = high threshold (barely touches the signal), high setting = lower threshold (more gain reduction)
	•	Blends back toward the dry signal proportionally to transientPreserve — on percussive content the effect largely bypasses itself
	•	Applies an equal-power dry/wet crossfade controlled by the Mix knob
	•	Applies a near-transparent output soft-clip as a safety ceiling
	
**Controls**
	•	Intensity — scales how much harmonic generation and compression the engine is allowed to apply
	•	Mix — equal-power dry/wet blend from fully dry to fully processed
	•	Focus — shifts which frequency band (sub vs low-mid) activates the enhancement
