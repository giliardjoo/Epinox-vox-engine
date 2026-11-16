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

## Logo

Put the official logo at `Assets/Logo/epinox_logo.png`. If the file exists, CMake embeds it in the header
automatically. Without it the header shows a plain text wordmark.

## Status by phase

| Phase | Scope | State |
|-------|-------|-------|
| 1 | Foundation: project, build, parameters, presets, interface | done (this commit) |
| 2 | Audio engine: Vocal Chop, Transform, Effects, Spatial, Mix | not started |
| 3 | Vocal generation (GENERATE) | not started |
| 4 to 6 | Musical intelligence, advanced features, installers and licensing | not started |

What is real today:

- All parameters exist, are automatable in the DAW and are saved with the project.
- Genre list, phrase field, Voice Type / Emotion / Style, Pitch, Formant, BPM, Mix, the four module switches and knobs.
- Power switch and output gain act on the audio. Input/output meters and the rolling waveform read the real signal.
- Presets: save, open, previous / next, stored as JSON in the user's application data folder (`EPINOX/VOX ENGINE/Presets`).

What does not do anything yet: the Mix knob and the module knobs have no audio effect until Phase 2, and GENERATE
reports that no vocal engine is installed until Phase 3. Nothing simulates audio.

## Layout

```
Source/
  Parameters.h          every parameter ID, range and choice list
  PluginProcessor.*     audio thread, meters, state
  PluginEditor.*        main window (1100 x 740 design space, scales with the window)
  PresetManager.*       JSON presets
  GenerationService.*   boundary between UI and the (future) vocal generation backend
  UI/                   neon look and feel and custom widgets
```
