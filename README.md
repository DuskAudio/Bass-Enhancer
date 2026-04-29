# Adaptive Bass Enhancer

A professional VST3 bass enhancement plugin built with JUCE. Uses real-time psychoacoustic analysis to adapt harmonic generation to the incoming signal — behaving differently on sustained bass, percussive kicks, and dense low-mid content.

Developed by **Gerardo**.

---

## How It Works

Unlike a static saturator, this plugin analyses the audio every block and makes decisions about how much processing to apply. The architecture has three distinct stages:

### 1. Feature Extractor
Analyses the incoming audio every block and computes:
- **Envelope** — smoothed amplitude with 5ms attack and 100ms decay
- **Attack strength** — how fast the envelope rose, used as a transient detector
- **Sub energy** — ratio of energy in the 20–80 Hz band
- **Low-mid energy** — ratio of energy in the 80–200 Hz band
- **Crest factor** — peak divided by RMS, high values indicate percussive content
- **Zero Crossing Rate** — low for bass content, high for high-frequency content

### 2. Decision Engine
Maps the extracted features to DSP parameters:
- Computes a **percussive score** (kick drums, transients) and a **sustained bass score** (bass guitar, sub synths)
- The **Focus** knob shifts which band drives the sustained score — low Focus targets the sub band, high Focus targets low-mid
- Applies a **mud penalty** — automatically reduces processing if the low-mid band is already dense
- Applies a **power-law curve** to harmonic amount for a more natural, musical knob response
- Smooths all output parameters with a one-pole filter to prevent abrupt changes between blocks
- Outputs five values: `harmonicAmount`, `compressionAmount`, `transientPreserve`, `bassScore`, `kickScore`

### 3. Adaptive Processor
Processes audio per-sample using the decision engine outputs:
- **Bass extraction** via 120 Hz low-pass filter
- **Context-aware drive** — waveshaper drive increases on sustained bass (+40% max) and decreases on percussive content (−30% max)
- **Dynamic drive reduction** — automatically reduces drive on loud signals to prevent over-saturation
- **Musical waveshaper** — tanh base with 30% asymmetric blend, adding warm even (2nd) harmonics alongside the standard odd harmonics
- **Harmonic extraction** — only the *new content* added by the waveshaper is blended back: `harmonics = shaped - bass`
- **Post-shaper LP** at 8000 Hz on the bass signal only, removing harsh aliases without affecting the dry signal
- **Gain compensation** — scales inversely with both drive and signal amplitude
- **Adaptive compressor** — threshold moves with compression amount (0.70 at low settings, 0.30 at high settings)
- **Transient preserve** — blends back toward dry signal proportionally on percussive material
- **Additive mix blend** — `output = dry + mix × harmonics`, so the Mix knob scales the harmonic addition without any volume change

---

## Controls

| Knob | Range | Description |
|---|---|---|
| **Intensity** | 0–100% | Master scale for harmonic generation and compression. At 0% the plugin is completely transparent. |
| **Mix** | 0–100% | Blends the harmonic addition. 0% = dry only, 100% = full enhancement. No volume change at any position. |
| **Focus** | 0–100% | Shifts which frequency band activates the enhancement. Low = sub (20–80 Hz), High = low-mid (80–200 Hz). |

The UI also shows three real-time meters at the bottom of the window displaying the current adaptive state: **Harmonic**, **Compress**, and **Transient** — so you can see how the plugin is responding to your audio.

---

## Signal Chain

```
Audio In
    │
    ├── FeatureExtractor (analysis only, no modification)
    │       Envelope · Attack · Sub Energy · Low-Mid Energy · Crest Factor · ZCR
    │
    └──► DecisionEngine
             harmonicAmount · compressionAmount · transientPreserve · bassScore · kickScore
                    │
                    ▼
            AdaptiveProcessor
                    │
                    ├── Bass extraction (LP @ 120 Hz)
                    │         │
                    │    Waveshaper (tanh + asymmetric blend)
                    │         │   drive = harmonicAmount × contextFactor × dynamicFactor
                    │    Post-shaper LP (@ 8000 Hz)
                    │         │
                    │    harmonics = shaped − bass
                    │
                    ├── Gain compensation
                    ├── Adaptive soft compressor
                    ├── Transient preserve blend
                    │
                    └── output = dry + mix × harmonics
                    │
               Audio Out
```

---

## Building

### Requirements

- [JUCE](https://github.com/juce-framework/JUCE) (7.x or later)
- CMake 3.22+
- A C++17-capable compiler:
  - macOS: Xcode 14+
  - Windows: Visual Studio 2022
  - Linux: GCC 11+ or Clang 13+

### macOS / Linux

```bash
git clone https://github.com/yourusername/BassEnhancer.git
cd BassEnhancer

# Clone JUCE into the project folder
git clone https://github.com/juce-framework/JUCE.git

# If JUCE is already installed elsewhere, edit CMakeLists.txt:
# change: add_subdirectory(/path/to/your/JUCE JUCE)

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Windows

```batch
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Output locations

| Platform | Path |
|---|---|
| macOS | `build/BassEnhancer_artefacts/Release/VST3/Bass Enhancer.vst3` |
| Windows | `build\BassEnhancer_artefacts\Release\VST3\Bass Enhancer.vst3` |
| Linux | `build/BassEnhancer_artefacts/Release/VST3/Bass Enhancer.vst3` |

### Install to your DAW

**macOS:**
```bash
cp -r "build/BassEnhancer_artefacts/Release/VST3/Bass Enhancer.vst3" \
      ~/Library/Audio/Plug-Ins/VST3/
```

**Windows:**
```
Copy to: C:\Program Files\Common Files\VST3\
```

**Linux:**
```bash
cp -r "build/BassEnhancer_artefacts/Release/VST3/Bass Enhancer.vst3" ~/.vst3/
```

---

## Project Structure

```
BassEnhancer/
├── CMakeLists.txt
├── README.md
└── Source/
    ├── FeatureExtractor.h / .cpp   — audio analysis
    ├── DecisionEngine.h / .cpp     — adaptive logic
    ├── AdaptiveProcessor.h / .cpp  — DSP processing
    ├── PluginProcessor.h / .cpp    — JUCE AudioProcessor glue
    └── PluginEditor.h / .cpp       — UI
```

---

## Technical Notes

- **No FFT** — all analysis uses lightweight IIR filters and time-domain statistics
- **No machine learning** — pure signal processing and rule-based decision making
- **Real-time safe** — no heap allocations in the audio thread, all buffers pre-allocated in `prepareToPlay`
- **Stereo** — all filters and processing maintain independent state per channel, with shared parameter smoothing to guarantee identical L/R parameter curves
- **Denormal protection** — `juce::ScopedNoDenormals` in `processBlock` plus DC-offset kill in the sample loop

---

## License

MIT — free to use in personal and commercial projects. Attribution appreciated.
