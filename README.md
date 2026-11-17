# EPINOX VOX ENGINE

Creative AI Vocal Generator, a VST3 / AU plugin and standalone app by EPINOX.
*One phrase. Infinite possibilities.*

C++17, JUCE 8.0.4 (downloaded by CMake), CMake 3.22+.

## Build

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Binaries land in `build/EpinoxVoxEngine_artefacts/Release/` (VST3, Standalone, and AU on macOS).
On GitHub, every push builds Windows and macOS (`.github/workflows/build.yml`) and uploads the binaries as artifacts.

## Installers

Every push to `main` builds both platforms on GitHub (Actions tab) and produces:

- **Windows:** `EPINOX-VOX-ENGINE-Setup-<version>.exe` (Inno Setup, `Installer/windows`). It installs the VST3 into
  `C:\Program Files\Common Files\VST3`, the standalone app into Program Files, and never asks to restart Windows.
- **macOS:** `EPINOX-VOX-ENGINE-<version>.pkg` (`Installer/macos`), with VST3, Audio Unit and standalone app. It is not
  notarized yet, so macOS asks for a confirmation the first time (System Settings, Privacy and Security, Open Anyway).

To download: GitHub, Actions tab, open the latest run, scroll to Artifacts, download `EPINOX-VOX-ENGINE-Windows-Installer`
(it is a zip with the .exe inside). Close Ableton before installing an update, because it keeps the plugin file in use.
In Ableton: Preferences, Plug-Ins, turn on "Use VST3 System Folders" and press Rescan.

## Logo

Put the official logo at `Assets/Logo/epinox_logo.png`. If the file exists, CMake embeds it in the header
automatically. Without it the header shows a plain text wordmark.

## Status by phase

| Phase | Scope | State |
|-------|-------|-------|
| 1 | Foundation: project, build, parameters, presets, interface | done (this commit) |
| 2 | Audio engine: Vocal Chop, Transform, Effects, Spatial, Mix | done, validated by numerical tests |
| 3 | Vocal generation (GENERATE), offline synthesis | done, validated by numerical tests |
| 4 | Musical intelligence: Key and Scale, chord-tone melodies, tonic resolution | done, validated by numerical tests |
| 5 | 70 factory presets that generate their own vocal | done |
| 6 | Commercial parameter document, licensing | not started |

What is real today:

- All parameters exist, are automatable in the DAW and are saved with the project.
- Genre list, phrase field, Voice Type / Emotion / Style, Pitch, Formant, BPM, Mix, the four module switches and knobs, and GENERATE.
- Power switch and output gain act on the audio. Input/output meters and the rolling waveform read the real signal.
- Presets: save, open, previous / next, stored as JSON in the user's application data folder (`EPINOX/VOX ENGINE/Presets`).

Phase 2 added the real audio engine (`Source/DSP`). Signal flow:
input, Vocal Chop, Transform, Effects, Spatial, then Mix against the time-aligned dry signal.

- Vocal Chop: step-sequenced gate (Rate, Steps, Smooth, Mix).
- Transform: spectral pitch and formant shifter (formants preserved when only the pitch moves), Robot (ring modulator), Grain (granular cloud). The global Pitch/Formant knobs add to the Transform ones.
- Effects: Distort, tempo-synced ping-pong Delay (follows the BPM knob), Reverb, stereo Width.
- Spatial: damped comb + allpass room (Size, Density), ducked by the input level (Ducking), Mix.
- Latency: 2048 samples, reported to the host (Live compensates it automatically). Power off keeps the same latency.

## GENERATE (Phase 3)

GENERATE turns the phrase into a sung vocal loop, fully offline (no internet, no cost per use):

1. **Phrase to syllables** (`Voice/PhraseParser.h`): rule based, for Portuguese and English spelling. It is an approximation of
   pronunciation, tuned for vocal-chop style textures rather than intelligibility.
2. **Rhythm and melody** (`Voice/Composer.h`): the genre sets the scale, the style sets rhythm, note lengths and melodic
   behaviour, the voice type sets the register. The phrase repeats to fill the loop in the rhythmic styles.
3. **Synthesis** (`Voice/VocalSynth.h`): source-filter voice. A harmonic stack shaped by five formant resonators makes the
   vowels, filtered noise makes fricatives, plosive bursts and breath. Voice type, emotion and style control formant scale,
   vibrato, breathiness, brightness, attack, portamento and unison (choir). Whisper is fully unvoiced.
4. **Playback**: the loop (1 to 8 bars) is generated on a worker thread, never on the audio thread, then played in time with
   the host transport (free running in standalone). It enters the chain before Vocal Chop, so every module processes it,
   and it follows the Mix knob like the rest of the processed signal. It is silent while the host transport is stopped.

Musical intelligence (Phase 4): the **Key** (C to B) and **Scale** (Genre, Natural Minor, Major, Dorian, Phrygian, Mixolydian,
Lydian, Harmonic Minor, Minor/Major Pentatonic, Blues) selectors set the notes of the melody. Scale "Genre" follows the genre's own
scale. Melodies put chord tones (1st, 3rd, 5th) on the strong beats most of the time and the leaping style resolves on the tonic.
Match the key to your track to make the vocal fit what you already have in Live.

Details worth knowing:

- Pressing GENERATE again gives a new variation of the same phrase. The same phrase and settings always give the same audio.
- If the host reports a tempo, the vocal is generated at that tempo and the BPM knob follows it.
- The project saves the generator settings, not the audio. When the project opens (or the sample rate changes) the vocal is
  rebuilt from them, identically.
- The global Pitch and Formant knobs and the Transform module act on the generated vocal in real time, so they do not need a new GENERATE.
- A cloud or model backend can later replace `renderPhrase` inside `GenerationService` without touching the rest of the plugin.

## Factory presets (Phase 5)

70 presets are built in, grouped by genre in the picker (`Source/FactoryPresets.h`). Click the preset name in the header to pick one from the genre menus, or use the
arrows (factory presets first, then your own saved ones). A factory preset resets every control to its default, applies its
settings and generates its vocal straight away. Power is never changed by a preset.

## Tests

```
cmake -B build -DEPINOX_BUILD_TESTS=ON
cmake --build build --config Release --target EpinoxDspTests
ctest --test-dir build -C Release --output-on-failure
```

Numerical checks of the audio engines and the generator: pitch accuracy, formant behaviour, latency, gain, chop pattern, delay
timing, reverb and spatial decay, ducking, mix and bypass alignment, stability with every control at its extreme, syllable
parsing, sung pitch and vowel formants, voiced versus whisper, determinism, loop length, all genre/voice/emotion/style
choices, and playback position of the generated loop. A second test checks the asynchronous generation service (worker thread,
busy flag, delivery on the message thread, safe shutdown). All of them run in the GitHub workflow.

## Layout

```
Source/
  Parameters.h          every parameter ID, range and choice list
  PluginProcessor.*     audio thread, meters, state
  DSP/                  audio engines (chop, transform, effects, spatial) and the VoxEngine chain
  PluginEditor.*        main window (1100 x 740 design space, scales with the window)
  PresetManager.*       JSON presets
  GenerationService.*   asynchronous boundary between the UI and the vocal generation backend
  Voice/                phrase parser, composer, vocal synthesiser, voice/emotion/style/genre tables
  UI/                   neon look and feel and custom widgets
Tests/                  DspTests.cpp (engines and generator), GenerationServiceTest.cpp (async service)
```
