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

To download: GitHub, Actions tab, open the latest run, scroll to Artifacts, download `EPINOX-VOX-ENGINE-<version>-Windows-Installer`
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
| 5 | 585 factory presets that generate their own vocal | done |
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

Voice realism (v0.6): the synthesiser adds the small imperfections of a real singer: slow intonation drift, cycle-to-cycle
jitter, level shimmer, an irregular vibrato rate and depth, a slightly different tuning and vowel colour on every note, pulsed
breath noise, brighter tone when singing louder, a singer's formant around 3 kHz, and first-formant tuning on high notes.
Each harmonic also carries the phase of the vocal tract resonators (v0.7), which rounds the glottal pulse and removes the
sharp "buzz" of a plain harmonic stack. Robotic voices and the Robotic emotion stay perfectly regular and zero-phase on purpose.

Body controls (v0.8), next to Pitch, Formant and BPM, applied at GENERATE time:

- **Legato** (default 60 %): joins the notes. The voiced part of each note lasts longer, the release is softer, consonants are shorter
  and fewer rests are inserted. 0 % keeps the style's own gaps.
- **Body** (default 60 %): chest warmth. A low shelf around 230 Hz plus a soft saturation that thickens the harmonics.
- **Layers** (default 40 %): doubles the voice with detuned, panned copies (2 or 3 voices) for a thicker, wider vocal. Robotic voices stay single.

The Vocal Chop module is now off by default, because its gate cuts the vocal into pieces; factory presets that use it switch it on.

Producer tools (v0.9):

- **Polish** (default 50 %): finishing chain on the generated vocal: a look-ahead compressor evens the level and a de-esser calms sibilance.
- **Air** (default 30 %): high shelf around 5.5 kHz that opens the top.
- **Octave** (-2 to +2): shifts the whole melody by octaves. **Lock Melody**: GENERATE keeps the melody and only changes the voice details.
- **EXPORT WAV**: saves the generated loop as a 24 bit stereo WAV. **DRAG TO TRACK**: drag the vocal as a WAV file onto a track in the host
  (it depends on the host accepting file drops from a plugin window; EXPORT WAV is the fallback).

Automatic regeneration (v0.9.2): moving Genre, Voice Type, Emotion, Style, Key, Scale, Octave, Legato, Body, Layers, Polish, Air or
BPM regenerates the vocal about 0.2 s after you stop, keeping the same take (same variation and melody). GENERATE and presets make a new
take. Pitch and Formant act live and never need it. More human timing: each note lands up to 4 ms early or late, and the tone gets
slightly purer on high notes and fuller on low ones.

Quality gate: every push runs pluginval at strictness 5 on the VST3 (Windows and macOS) and fails the build if it reports a problem.

Details worth knowing:

- Pressing GENERATE again gives a new variation of the same phrase. The same phrase and settings always give the same audio.
- If the host reports a tempo, the vocal is generated at that tempo and the BPM knob follows it.
- The project saves the generator settings, not the audio. When the project opens (or the sample rate changes) the vocal is
  rebuilt from them, identically.
- The global Pitch and Formant knobs and the Transform module act on the generated vocal in real time, so they do not need a new GENERATE.
- A cloud or model backend can later replace `renderPhrase` inside `GenerationService` without touching the rest of the plugin.

## Factory presets (Phase 5)

585 presets are built in, grouped by genre in the picker (`Source/FactoryPresets.h`). Click the preset name in the header to pick one from the genre menus, or use the
arrows (they stay inside the genre selected in the list and wrap around at its ends; your own saved presets are stepped through in their own list). Loading any preset (factory or file) generates its vocal automatically; Enter in the phrase field also generates. A factory preset resets every control to its default, applies its
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

## Voice engines (v0.10)

The ENGINE selector (left column) chooses how the vocal is rendered. `Built-in synth` is the offline synthesiser.
Neural voice packs are folders in `%APPDATA%\EPINOX\VOX ENGINE\Voices\<pack>\` (macOS: `~/Library/Application Support/EPINOX/VOX ENGINE/Voices/`)
with a `voicepack.json`:

```json
{ "id": "my-voice", "name": "My Voice", "type": "onnx", "license": "what the license allows", "models": ["acoustic.onnx", "vocoder.onnx"] }
```

A pack is listed only when it has an id, a name, a stated license and every model file exists. This version detects and validates packs;
the ONNX runtime that executes them is not included yet, so a selected pack falls back to the built-in synth and the status line says so.

## Smarter melodies and LISTEN (v0.11)

- **Melody search.** For the stepwise, motif and leaping styles the generator no longer takes the first melody it composes. It composes 16
  candidates, scores them for musicality (singable steps, chord tones on strong beats, a clear high point, a phrase that ends on the root or
  fifth, staying in the voice's range), then refines the best one by simulated annealing, a "quantum-inspired" optimisation that runs on a
  normal CPU. Notes always stay inside the chosen scale, and the result is the same for the same phrase and variation, so LOCK MELODY still works.
- **LISTEN.** The plugin keeps the last 8 seconds of the audio coming into it. Press LISTEN next to the Key menu and it detects the key
  (Krumhansl-Kessler profiles over a pitch-class profile) and sets Key and Scale. Play the bass or chords through the plugin first.
- Polish now measures loudness with a short-term RMS detector instead of peaks.

## Voice morph (v0.12)

A new row under the waveform holds ENGINE, MORPH TO and the morph amount. MORPH TO picks a second voice type (Off by default) and the slider
moves the voice from VOICE towards it: pitch range, formants, breathiness, vibrato, brightness, layering and attack blend smoothly; Robotic
and Whisper switch on past the half-way point. The window is 868 px tall now.

## Your own voice: voice profiles (v0.13)

A voice pack of type `profile` makes the built-in synthesiser sing with a real person's vocal tract: the five measured vowel formants,
the person's centre note, breathiness and vibrato. It appears in the ENGINE menu like any pack.

1. Record the vowel test (sustained A E I O U at a low, a middle and a high note, 3 s each, with pauses).
2. `python3 Tools/analyze_voice.py recording.wav --id my-voice --name "My Voice"` writes `my-voice/voicepack.json` and prints what it measured.
   (`Tools/make_test_recording.py` makes a synthetic recording with known formants to check the script: it recovers them within about 6 %.)
3. Copy the folder to `%APPDATA%\EPINOX\VOX ENGINE\Voices\` (macOS: `~/Library/Application Support/EPINOX/VOX ENGINE/Voices/`) and pick it in ENGINE.

This keeps the synthesiser's sound character; it does not replay recordings. The voice-library and neural paths are separate steps.

## Phrase dynamics (v0.15)

Syllables no longer all carry the same weight: a gentle swell runs across the phrase, higher notes are slightly louder and each word gets its own
small level (about plus or minus 0.75 dB). It never adds silence, so a gate such as the one in EPINOX MOTION FX decides where the pauses go.
Robotic voices stay flat. The same phrase and variation always give the same dynamics.

## Smoother reverbs (0.16)

The Spatial room and the Effects reverb now use a feedback delay network (8 modulated delay lines, Hadamard mixing, input diffusion) instead of parallel comb filters. The tail is denser and no longer rings on pitched material; low frequencies are kept out of the tail so it cannot boom. Overall loudness and decay times are close to the previous versions, so existing presets still behave similarly (the longest Spatial room is now about 3 s).

## LFO (0.17)

A strip at the bottom of the window holds one LFO that moves four things at once. Each destination has its own depth knob (0 leaves it alone):

- **PITCH** - vibrato, up to +/-2 semitones (acts on the voice, before the effects).
- **FORMANT** - the vowel colour breathes, up to +/-6 semitones (acts on the voice, before the effects).
- **FILTER** - a low-pass sweeps between 150 Hz and 12 kHz, with a little resonance.
- **VOLUME** - tremolo, down to full silence.

Filter and volume run at the very end of the chain, so reverb and delay tails move with them.

Shapes: sine, triangle, square, saw down, saw up, random smooth and random step. **SYNC** is Free (the RATE knob, 0.02-30 Hz, log taper) or a tempo division: 8 bars down to 1/32, each straight, dotted (D) and triplet (T). While the host plays, synced LFOs lock to the host position, so a cycle always starts on the beat; **PHASE** shifts where the cycle starts. When SYNC is not Free the RATE knob shows the note value. Moving the LFO never regenerates the melody. 100 of the factory presets use it.
