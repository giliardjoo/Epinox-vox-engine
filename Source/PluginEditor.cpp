#include "PluginEditor.h"
#include "FactoryPresets.h"
#include "VoiceEngines.h"
#include "UI/Widgets.h"
#include <juce_audio_formats/juce_audio_formats.h>

#if EPINOX_HAS_LOGO
 #include "BinaryData.h"
#endif

using namespace epinox;
using namespace epinox::colours;
using namespace epinox::ui;

//==============================================================================
namespace
{
    // Writes the generated loop as a 24 bit WAV file. Returns false when the file could not be written.
    bool writeClipWav (const epinox::GeneratedClip& clip, const juce::File& file)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> out (file.createOutputStream());

        if (out == nullptr)
            return false;

        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (out.get(), clip.sampleRate, 2, 24, {}, 0));

        if (writer == nullptr)
            return false;

        out.release();   // the writer owns the stream now

        juce::AudioBuffer<float> buffer (2, clip.length());
        buffer.copyFrom (0, 0, clip.left.data(), clip.length());
        buffer.copyFrom (1, 0, clip.right.data(), clip.length());
        return writer->writeFromAudioSampleBuffer (buffer, 0, clip.length());
    }

    juce::String clipFileName (const epinox::GeneratedClip& clip, const juce::String& phrase)
    {
        auto name = juce::File::createLegalFileName (phrase.trim().substring (0, 28));

        if (name.isEmpty())
            name = "Vocal";

        return "EPINOX VOX - " + name + " - " + juce::String (juce::roundToInt (clip.bpm)) + " BPM.wav";
    }

    // A handle you drag onto a track in the DAW: it carries the generated vocal as a WAV file.
    class DragHandle : public juce::Component
    {
    public:
        std::function<juce::File()> prepareFile;

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (juce::Colour (0xff0a1210));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (neonDim.withAlpha (over ? 1.0f : 0.8f));
            g.drawRoundedRectangle (r, 8.0f, 1.3f);
            g.setColour (neon);
            g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
            g.drawText ("DRAG TO TRACK", r, juce::Justification::centred);
        }

        DragHandle() { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }

        void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { over = false; repaint(); }

        // The file is written when the button goes down, so the drag starts at once and the host
        // is handed a finished WAV.
        void mouseDown (const juce::MouseEvent&) override
        {
            dragging = false;
            file = prepareFile ? prepareFile() : juce::File();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging || e.getDistanceFromDragStart() < 4 || ! file.existsAsFile())
                return;

            dragging = true;
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                         [this] { dragging = false; });
        }

        // A plain click (no drag) shows the file in Explorer / Finder, so it can still be dropped by hand.
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! dragging && e.getDistanceFromDragStart() < 4 && file.existsAsFile())
                file.revealToUser();

            dragging = false;
        }

    private:
        bool over = false, dragging = false;
        juce::File file;
    };
}

//==============================================================================
// All controls live in this 1100 x 998 design space; the editor scales it to the window.
class VoxContent : public juce::Component, private juce::Timer, private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit VoxContent (VoxProcessor& p)
        : proc (p),
          apvts (p.apvts),
          genreList (p.apvts),
          voiceCombo   (p.apvts, pid::voiceType, "VOICE TYPE"),
          emotionCombo (p.apvts, pid::emotion,   "EMOTION"),
          styleCombo   (p.apvts, pid::style,     "STYLE"),
          keyCombo     (p.apvts, pid::key,       "KEY"),
          scaleCombo   (p.apvts, pid::scale,     "SCALE"),
          morphCombo   (p.apvts, pid::morphTo,  "MORPH TO"),
          pitchKnob    (p.apvts, pid::pitch,   "PITCH",   " st", 1),
          formantKnob  (p.apvts, pid::formant, "FORMANT", " st", 1),
          bpmKnob      (p.apvts, pid::bpm,     "BPM",     "",    1),
          legatoKnob   (p.apvts, pid::legato,  "LEGATO",  " %",  0),
          bodyKnob     (p.apvts, pid::body,    "BODY",    " %",  0),
          layersKnob   (p.apvts, pid::layers,  "LAYERS",  " %",  0),
          octaveKnob   (p.apvts, pid::octave,  "OCTAVE",  "",    0),
          polishKnob   (p.apvts, pid::polish,  "POLISH",  " %",  0),
          airKnob      (p.apvts, pid::air,     "AIR",     " %",  0),
          deEssKnob    (p.apvts, pid::deEss,      "DE-ESS",     " %", 0),
          warmthKnob   (p.apvts, pid::warmth,     "WARMTH",     " %", 0),
          consonantsKnob (p.apvts, pid::consonants, "CONSONANT", " %", 0),
          humanizeKnob (p.apvts, pid::humanize,   "HUMANIZE",   " %", 0),
          mixKnob      (p.apvts, pid::mix,     "MIX",     " %",  0, true),
          outKnob      (p.apvts, pid::outGain, "OUTPUT",  " dB", 1),
          waveform (p),
          meterIn ("IN"),
          meterOut ("OUT"),
          chopPanel (p.apvts, "VOCAL CHOP ENGINE", pid::chopOn,
                     { { pid::chopRate,   "RATE",   " Hz", 1 },
                       { pid::chopSteps,  "STEPS",  "",    0 },
                       { pid::chopSmooth, "SMOOTH", " %",  0 },
                       { pid::chopMix,    "MIX",    " %",  0 } }),
          transformPanel (p.apvts, "TRANSFORM", pid::trOn,
                          { { pid::trPitch,   "PITCH",   " st", 1 },
                            { pid::trFormant, "FORMANT", " st", 1 },
                            { pid::trRobot,   "ROBOT",   " %",  0 },
                            { pid::trGrain,   "GRAIN",   " %",  0 } }),
          effectsPanel (p.apvts, "EFFECTS", pid::fxOn,
                        { { pid::fxReverb,  "REVERB",  " %", 0 },
                          { pid::fxDelay,   "DELAY",   " %", 0 },
                          { pid::fxDistort, "DISTORT", " %", 0 },
                          { pid::fxWidth,   "WIDTH",   " %", 0 } }),
          spatialPanel (p.apvts, "SPATIAL", pid::spOn,
                        { { pid::spSize,    "SIZE",    " %", 0 },
                          { pid::spDensity, "DENSITY", " %", 0 },
                          { pid::spDucking, "DUCKING", " %", 0 },
                          { pid::spMix,     "MIX",     " %", 0 } }),
          lfoPanel (p.apvts),
          powerAttachment (p.apvts, pid::power, powerButton)
    {
        setSize (1100, 740);

#if EPINOX_HAS_LOGO
        logo = juce::ImageCache::getFromMemory (BinaryData::epinox_logo_png, BinaryData::epinox_logo_pngSize);
#endif

        phrase.setMultiLine (false);
        phrase.setFont (juce::Font (juce::FontOptions (18.0f)));
        phrase.setJustification (juce::Justification::centredLeft);
        phrase.setIndents (16, 0);
        phrase.setInputRestrictions (200);
        phrase.setTextToShowWhenEmpty ("Type a phrase...   One phrase. Infinite possibilities.", textDim);
        phrase.setText (apvts.state.getProperty ("phrase").toString(), false);
        phrase.onTextChange = [this] { apvts.state.setProperty ("phrase", phrase.getText(), nullptr); };

        morphSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        morphSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 24);
        morphSlider.setTextValueSuffix (" %");
        morphSlider.setColour (juce::Slider::trackColourId, neon);
        morphSlider.setColour (juce::Slider::thumbColourId, neon);
        morphSlider.setColour (juce::Slider::textBoxTextColourId, textDim);
        morphSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        morphSlider.setTooltip ("How far the voice moves from VOICE towards MORPH TO.");
        morphAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, pid::morphAmt, morphSlider);

        listenButton.setButtonText ("LISTEN");
        listenButton.setTooltip ("Detects the key of the audio coming into the plugin (put the bass or chords on this track's input) and sets Key and Scale to match.");
        listenButton.onClick = [this]
        {
            const auto r = proc.detectKeyFromInput();

            if (! r.valid)
            {
                setStatus ("No key found. Play the track through the plugin for a few seconds first (the input must carry bass, chords or a melody).", true);
                return;
            }

            auto setChoice = [this] (const char* id, int index)
            {
                if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id)))
                    p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (index)));
            };

            setChoice (pid::key, r.tonic);
            setChoice (pid::scale, r.minor ? 1 : 2);   // Natural Minor or Major
            setStatus ("Key set to " + keyNames()[r.tonic] + (r.minor ? " minor" : " major")
                           + (r.confidence < 0.35f ? ". The match is weak; check it by ear." : "."), r.confidence < 0.35f);
        };

        lockButton.setComponentID ("lock");
        lockButton.setButtonText ("LOCK MELODY");
        lockAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, pid::lockMelody, lockButton);
        lockButton.setTooltip ("Keeps the same melody when you press GENERATE again; only the voice details change.");

        exportButton.setButtonText ("EXPORT WAV");
        exportButton.onClick = [this] { onExport(); };

        dragHandle.prepareFile = [this] { return prepareDragFile(); };

        generateButton.setComponentID ("generate");
        generateButton.setButtonText ("GENERATE");
        generateButton.onClick = [this] { pendingGenerate = false; autoDirty.store (false); onGenerate(); };
        phrase.onReturnKey = [this] { pendingGenerate = false; onGenerate(); };   // Enter in the phrase field also generates

        prevButton.setButtonText ("<");
        nextButton.setButtonText (">");
        saveButton.setButtonText ("SAVE");
        openButton.setButtonText ("OPEN");
        prevButton.onClick = [this] { if (proc.presets.loadRelative (-1)) afterPresetLoad(); };
        nextButton.onClick = [this] { if (proc.presets.loadRelative (+1)) afterPresetLoad(); };
        presetLabel.addMouseListener (this, false);
        saveButton.onClick = [this] { onSave(); };
        openButton.onClick = [this] { onOpen(); };

        powerButton.setComponentID ("power");

        presetLabel.setJustificationType (juce::Justification::centred);
        presetLabel.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        presetLabel.setText (proc.presets.getCurrentName(), juce::dontSendNotification);

        statusLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
        statusLabel.setColour (juce::Label::textColourId, textDim);
        statusLabel.setJustificationType (juce::Justification::centredLeft);
        statusLabel.setText (proc.getGenerationStatus(), juce::dontSendNotification);
        lastStatusVersion = proc.getGenerationStatusVersion();

        for (auto* c : std::initializer_list<juce::Component*> {
                 &genreList, &engineCombo, &phrase, &generateButton,
                 &voiceCombo, &emotionCombo, &styleCombo, &keyCombo, &listenButton, &scaleCombo, &morphCombo, &morphSlider, &pitchKnob, &formantKnob, &bpmKnob, &legatoKnob, &bodyKnob, &layersKnob, &octaveKnob, &polishKnob, &airKnob, &deEssKnob, &warmthKnob, &consonantsKnob, &humanizeKnob, &lockButton, &exportButton, &dragHandle,
                 &mixKnob, &outKnob, &waveform, &meterIn, &meterOut,
                 &chopPanel, &transformPanel, &effectsPanel, &spatialPanel, &lfoPanel,
                 &prevButton, &nextButton, &saveButton, &openButton, &powerButton,
                 &presetLabel, &statusLabel })
            addAndMakeVisible (c);

        setupEngineCombo();

        // Any control that shapes the generated vocal regenerates it by itself, a moment after you stop moving it.
        for (auto* id : autoIds)
            apvts.addParameterListener (id, this);

        startTimerHz (30);
    }

    ~VoxContent() override
    {
        for (auto* id : autoIds)
            apvts.removeParameterListener (id, this);
    }

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff010304), 0.0f, 0.0f,
                                                 juce::Colour (0xff03120d), 0.0f, area.getBottom(), false));
        g.fillAll();

        g.setGradientFill (juce::ColourGradient (neon.withAlpha (0.11f), area.getRight() * 0.75f, 0.0f,
                                                 neon.withAlpha (0.0f), area.getRight() * 0.75f + 520.0f, 0.0f, true));
        g.fillAll();
        g.setGradientFill (juce::ColourGradient (cyan.withAlpha (0.06f), 0.0f, area.getBottom(),
                                                 cyan.withAlpha (0.0f), 480.0f, area.getBottom(), true));
        g.fillAll();

        g.setColour (neon.withAlpha (0.035f));
        for (int x = 0; x < getWidth(); x += 44)
            g.drawVerticalLine (x, 0.0f, area.getBottom());
        for (int y = 0; y < getHeight(); y += 44)
            g.drawHorizontalLine (y, 0.0f, area.getRight());

        // Header
        float titleX = 20.0f;

        if (logo.isValid())
        {
            g.drawImage (logo, juce::Rectangle<float> (20.0f, 8.0f, 48.0f, 48.0f), juce::RectanglePlacement::centred);
            titleX = 82.0f;
        }
        else
        {
            g.setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)).withExtraKerningFactor (0.22f));
            g.setColour (text);
            g.drawText ("EPINOX", juce::Rectangle<float> (20.0f, 14.0f, 160.0f, 36.0f), juce::Justification::centredLeft);
            g.setColour (neon.withAlpha (0.5f));
            g.fillRect (174.0f, 18.0f, 1.5f, 28.0f);
            titleX = 190.0f;
        }

        g.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)).withExtraKerningFactor (0.2f));
        g.setColour (neon.withAlpha (0.25f));
        g.drawText ("VOX ENGINE", juce::Rectangle<float> (titleX + 1.0f, 15.0f, 260.0f, 34.0f), juce::Justification::centredLeft);
        g.setColour (neon);
        g.drawText ("VOX ENGINE", juce::Rectangle<float> (titleX, 14.0f, 260.0f, 34.0f), juce::Justification::centredLeft);

        auto presetBox = presetLabel.getBounds().toFloat().expanded (4.0f, 0.0f);
        g.setColour (juce::Colour (0xff060c0a));
        g.fillRoundedRectangle (presetBox, 8.0f);
        glowRoundedRect (g, presetBox, 8.0f, neon, 0.35f);
        g.setColour (neon.withAlpha (0.55f));
        g.drawRoundedRectangle (presetBox, 8.0f, 1.2f);

        g.setColour (neon.withAlpha (0.3f));
        g.drawHorizontalLine (66, 16.0f, area.getRight() - 16.0f);

        // Footer
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)).withExtraKerningFactor (0.18f));
        g.setColour (textDim);
        g.drawText (juce::String ("EPINOX  |  ONE PHRASE. INFINITE POSSIBILITIES.  |  V") + JucePlugin_VersionString,
                    juce::Rectangle<float> (area.getRight() - 560.0f, 976.0f, 544.0f, 18.0f),
                    juce::Justification::centredRight);
    }

    void resized() override
    {
        prevButton.setBounds (470, 18, 30, 30);
        presetLabel.setBounds (506, 18, 230, 30);
        nextButton.setBounds (742, 18, 30, 30);
        saveButton.setBounds (790, 18, 62, 30);
        openButton.setBounds (858, 18, 62, 30);
        powerButton.setBounds (1040, 12, 42, 42);

        genreList.setBounds (16, 80, 204, 466);
        engineCombo.setBounds (16, 556, 204, 46);
        morphCombo.setBounds (236, 556, 196, 46);
        morphSlider.setBounds (444, 566, 404, 30);

        phrase.setBounds (236, 80, 490, 52);
        generateButton.setBounds (738, 80, 110, 52);

        voiceCombo.setBounds   (236, 144, 196, 46);
        emotionCombo.setBounds (444, 144, 196, 46);
        styleCombo.setBounds   (652, 144, 196, 46);

        // Six knobs in one row: the three voice knobs, then the three that give the vocal its body.
        {
            NeonKnob* row[] = { &pitchKnob, &formantKnob, &bpmKnob, &legatoKnob, &bodyKnob, &layersKnob };

            for (int i = 0; i < 6; ++i)
                row[i]->setBounds (236 + i * 102, 200, 102, 128);
        }

        keyCombo.setBounds   (236, 334, 132, 46);
        listenButton.setBounds (372, 334, 60, 46);
        scaleCombo.setBounds (236, 386, 196, 46);

        octaveKnob.setBounds (444, 332, 102, 128);
        polishKnob.setBounds (546, 332, 102, 128);
        airKnob.setBounds    (648, 332, 102, 128);

        lockButton.setBounds   (758, 336, 90, 36);
        exportButton.setBounds (758, 380, 90, 36);
        dragHandle.setBounds   (758, 424, 90, 36);

        {
            NeonKnob* q[] = { &deEssKnob, &warmthKnob, &consonantsKnob, &humanizeKnob };

            for (int i = 0; i < 4; ++i)
                q[i]->setBounds (236 + i * 102, 462, 102, 90);
        }

        waveform.setBounds (650, 470, 198, 76);

        mixKnob.setBounds (864, 80, 220, 250);
        meterIn.setBounds  (878, 342, 50, 204);
        meterOut.setBounds (934, 342, 50, 204);
        outKnob.setBounds  (994, 390, 90, 112);

        const int w = 258, gap = 12;
        chopPanel.setBounds      (16 + 0 * (w + gap), 616, w, 228);
        transformPanel.setBounds (16 + 1 * (w + gap), 616, w, 228);
        effectsPanel.setBounds   (16 + 2 * (w + gap), 616, w, 228);
        spatialPanel.setBounds   (16 + 3 * (w + gap), 616, w, 228);
        lfoPanel.setBounds       (16, 852, 1068, 120);

        statusLabel.setBounds (16, 976, 640, 18);
    }

private:
    // Called for every change of a generator control (from the UI, a preset or host automation, so it only sets flags).
    void parameterChanged (const juce::String&, float) override
    {
        if (ignoreChanges.load())
            return;

        lastChangeMs.store (juce::Time::getMillisecondCounter());
        autoDirty.store (true);
    }

    void timerCallback() override
    {
        if (autoDirty.load() && juce::Time::getMillisecondCounter() - lastChangeMs.load() > 220 && ! proc.isGenerating())
        {
            autoDirty.store (false);

            if (phrase.getText().trim().isNotEmpty())
                onGenerate (true);
        }

        meterIn.setLevels  (proc.inPeak.load(),  proc.inRms.load());
        meterOut.setLevels (proc.outPeak.load(), proc.outRms.load());

        if (proc.clipped.exchange (false))
            meterOut.triggerClip();

        chopPanel.refresh();
        transformPanel.refresh();
        effectsPanel.refresh();
        spatialPanel.refresh();
        lfoPanel.refresh();

        // The morph amount only means something once a second voice is chosen in MORPH TO.
        {
            const bool morphOn = proc.apvts.getRawParameterValue (pid::morphTo)->load() > 0.5f;
            morphSlider.setEnabled (morphOn);
            morphSlider.setAlpha (morphOn ? 1.0f : 0.35f);
            morphSlider.setTooltip (morphOn ? "How far the voice moves from VOICE towards MORPH TO."
                                            : "Choose a voice in MORPH TO first: this slider blends VOICE into it.");
        }

        generateButton.setEnabled (! proc.isGenerating());

        if (pendingGenerate && ! proc.isGenerating())
        {
            pendingGenerate = false;
            onGenerate();
        }

        // Show the generator's messages (finished, restored with the project, failed).
        if (proc.getGenerationStatusVersion() != lastStatusVersion)
        {
            lastStatusVersion = proc.getGenerationStatusVersion();
            setStatus (proc.getGenerationStatus(), proc.generationStatusIsWarning());
        }
    }

    void setStatus (const juce::String& message, bool warning)
    {
        statusLabel.setColour (juce::Label::textColourId, warning ? warn : textDim);
        statusLabel.setText (message, juce::dontSendNotification);
    }

    // Click the preset name to pick a factory preset from a list.
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.eventComponent != &presetLabel)
            return;

        juce::PopupMenu menu;
        const auto& list = factoryPresets();

        // One submenu per genre.
        const auto genres = genreNames();

        for (int g = 0; g < genres.size(); ++g)
        {
            juce::PopupMenu sub;

            for (size_t i = 0; i < list.size(); ++i)
            {
                int presetGenre = 0;

                for (const auto& kv : list[i].values)
                    if (juce::String (kv.first) == pid::genre)
                        presetGenre = static_cast<int> (kv.second);

                if (presetGenre == g)
                    sub.addItem (static_cast<int> (i) + 1, list[i].name);
            }

            menu.addSubMenu (genres[g], sub);
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetLabel),
                            [this] (int result)
                            {
                                if (result > 0 && proc.presets.loadFactory (result - 1))
                                    afterPresetLoad();
                            });
    }

    void afterPresetLoad()
    {
        autoDirty.store (false);   // the preset generates its own vocal below
        phrase.setText (apvts.state.getProperty ("phrase").toString(), false);
        presetLabel.setText (proc.presets.getCurrentName(), juce::dontSendNotification);
        setStatus ({}, false);

        // Every preset sounds right away: loading it generates its vocal. While one is still being made
        // (quick stepping through presets), the newest preset waits and is generated as soon as the worker is free.
        if (proc.isGenerating())
            pendingGenerate = true;
        else
            onGenerate();
    }

    void onGenerate (bool automatic = false)
    {
        const auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

        if (phrase.getText().trim().isEmpty())
        {
            setStatus ("Type a phrase first.", true);
            return;
        }

        GenerationRequest request;
        request.phrase    = phrase.getText();
        request.genre     = static_cast<int> (get (pid::genre));
        request.voiceType = static_cast<int> (get (pid::voiceType));
        request.emotion   = static_cast<int> (get (pid::emotion));
        request.style     = static_cast<int> (get (pid::style));
        request.key       = static_cast<int> (get (pid::key));
        request.scale     = static_cast<int> (get (pid::scale));
        request.bpm       = get (pid::bpm);
        request.legato    = get (pid::legato) / 100.0f;
        request.body      = get (pid::body) / 100.0f;
        request.layers    = get (pid::layers) / 100.0f;
        request.octave    = static_cast<int> (get (pid::octave));
        request.polish    = get (pid::polish) / 100.0f;
        request.air       = get (pid::air) / 100.0f;
        request.deEss     = get (pid::deEss) / 100.0f;
        request.warmth    = get (pid::warmth) / 100.0f;
        request.consonants = get (pid::consonants) / 100.0f;
        request.humanize  = get (pid::humanize) / 100.0f;
        request.engineId  = selectedEngineId();
        request.morphTo   = static_cast<int> (get (pid::morphTo)) - 1;   // choice 0 is "Off"
        request.morph     = request.morphTo >= 0 ? get (pid::morphAmt) / 100.0f : 0.0f;

        // When the host reports a tempo, the vocal is generated at that tempo and the BPM knob follows it.
        const double hostBpm = proc.getHostBpm();

        if (! automatic && hostBpm >= 40.0 && hostBpm <= 220.0)
        {
            request.bpm = hostBpm;

            if (auto* bpmParam = apvts.getParameter (pid::bpm))
            {
                ignoreChanges.store (true);
                bpmParam->setValueNotifyingHost (bpmParam->convertTo0to1 (static_cast<float> (hostBpm)));
                ignoreChanges.store (false);
            }
        }

        // A tweak regenerates the same take (same variation); GENERATE and presets make a new one.
        request.variation = automatic ? proc.getLastVariation() : proc.nextVariation();

        // Lock Melody: keep the melody of the last vocal, vary only the voice details.
        const bool lock = (automatic || get (pid::lockMelody) > 0.5f) && proc.getLastMelodyVariation() >= 0;
        request.melodyVariation = lock ? proc.getLastMelodyVariation() : static_cast<int> (request.variation);

        if (proc.requestGeneration (request))
        {
            lastStatusVersion = proc.getGenerationStatusVersion();
            setStatus (automatic ? "Updating vocal..." : "Generating vocal...", false);
        }
        else
        {
            setStatus ("Still generating, one moment.", false);
        }
    }

    void onExport()
    {
        const auto clip = proc.getCurrentClip();

        if (clip == nullptr)
        {
            setStatus ("Generate a vocal first, then export it.", true);
            return;
        }

        chooser = std::make_unique<juce::FileChooser> ("Export the vocal as WAV",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                                           .getChildFile (clipFileName (*clip, phrase.getText())),
                                                       "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, clip] (const juce::FileChooser& fc)
                              {
                                  const auto f = fc.getResult();

                                  if (f == juce::File())
                                      return;

                                  setStatus (writeClipWav (*clip, f.withFileExtension ("wav")) ? "Exported: " + f.getFileName()
                                                                                              : "Could not write the WAV file.",
                                             false);
                              });
    }

    // Writes the current vocal to a temporary WAV so it can be dragged onto a track.
    juce::File prepareDragFile()
    {
        const auto clip = proc.getCurrentClip();

        if (clip == nullptr)
        {
            setStatus ("Generate a vocal first, then drag it to a track.", true);
            return {};
        }

        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("EPINOX VOX ENGINE");
        dir.createDirectory();
        const auto file = dir.getChildFile (clipFileName (*clip, phrase.getText()));

        if (! writeClipWav (*clip, file))
        {
            setStatus ("Could not prepare the file to drag.", true);
            return {};
        }

        return file;
    }

    void onSave()
    {
        chooser = std::make_unique<juce::FileChooser> ("Save preset",
                                                       proc.presets.getPresetDirectory().getChildFile ("New Preset.json"),
                                                       "*.json");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto f = fc.getResult();

                                  if (f == juce::File())
                                      return;

                                  if (proc.presets.savePreset (f.withFileExtension ("json")))
                                  {
                                      presetLabel.setText (proc.presets.getCurrentName(), juce::dontSendNotification);
                                      setStatus ("Preset saved.", false);
                                  }
                                  else
                                  {
                                      setStatus ("Could not save the preset.", true);
                                  }
                              });
    }

    void onOpen()
    {
        chooser = std::make_unique<juce::FileChooser> ("Open preset", proc.presets.getPresetDirectory(), "*.json");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto f = fc.getResult();

                                  if (f == juce::File())
                                      return;

                                  if (proc.presets.loadPreset (f))
                                      afterPresetLoad();
                                  else
                                      setStatus ("That file is not a valid VOX ENGINE preset.", true);
                              });
    }

    VoxProcessor& proc;
    juce::AudioProcessorValueTreeState& apvts;
    int lastStatusVersion = 0;

    juce::Image logo;

    bool pendingGenerate = false;
    std::atomic<bool> autoDirty { false }, ignoreChanges { false };
    std::atomic<juce::uint32> lastChangeMs { 0 };
    static constexpr const char* autoIds[] = { pid::genre, pid::voiceType, pid::emotion, pid::style, pid::key, pid::scale, pid::octave,
                                                pid::legato, pid::body, pid::layers, pid::polish, pid::air, pid::deEss, pid::warmth, pid::consonants, pid::humanize, pid::bpm, pid::morphTo, pid::morphAmt };
    juce::String selectedEngineId() const
    {
        const auto i = engineCombo.getSelectedItemIndex();
        return i >= 0 && i < engineIds.size() ? engineIds[i] : juce::String ("builtin");
    }

    void setupEngineCombo()
    {
        engineIds.clear();
        engineCombo.clear (juce::dontSendNotification);

        for (auto& e : engines::listEngines())
        {
            engineIds.add (e.id);
            engineCombo.addItem ("ENGINE: " + e.name, engineIds.size());
        }

        const auto saved = apvts.state.getProperty ("engine_choice", "builtin").toString();
        const auto idx = juce::jmax (0, engineIds.indexOf (saved));
        engineCombo.setSelectedItemIndex (idx, juce::dontSendNotification);
        engineCombo.onChange = [this]
        {
            apvts.state.setProperty ("engine_choice", selectedEngineId(), nullptr);
            autoDirty.store (true);
            lastChangeMs.store (juce::Time::getMillisecondCounter());
        };
    }

    GenreList genreList;
    juce::ComboBox engineCombo;
    juce::StringArray engineIds;
    juce::TextEditor phrase;
    juce::TextButton generateButton, prevButton, nextButton, saveButton, openButton;
    juce::ToggleButton powerButton;
    juce::Label presetLabel, statusLabel;

    NeonCombo voiceCombo, emotionCombo, styleCombo, keyCombo, scaleCombo, morphCombo;
    juce::Slider morphSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> morphAttachment;
    NeonKnob pitchKnob, formantKnob, bpmKnob, legatoKnob, bodyKnob, layersKnob, octaveKnob, polishKnob, airKnob, deEssKnob, warmthKnob, consonantsKnob, humanizeKnob, mixKnob, outKnob;
    juce::ToggleButton lockButton;
    juce::TextButton exportButton, listenButton;
    DragHandle dragHandle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lockAttachment;
    WaveformView waveform;
    MeterBar meterIn, meterOut;
    ModulePanel chopPanel, transformPanel, effectsPanel, spatialPanel;
    LfoPanel lfoPanel;

    juce::AudioProcessorValueTreeState::ButtonAttachment powerAttachment;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
VoxEditor::VoxEditor (VoxProcessor& p) : juce::AudioProcessorEditor (p)
{
    setLookAndFeel (&laf);

    content = std::make_unique<VoxContent> (p);
    addAndMakeVisible (*content);

    setResizable (true, true);
    setResizeLimits (designWidth * 4 / 5, designHeight * 4 / 5, designWidth * 3 / 2, designHeight * 3 / 2);
    getConstrainer()->setFixedAspectRatio (static_cast<double> (designWidth) / designHeight);
    setSize (designWidth, designHeight);
}

VoxEditor::~VoxEditor()
{
    content.reset();
    setLookAndFeel (nullptr);
}

void VoxEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void VoxEditor::resized()
{
    const float scale = static_cast<float> (getWidth()) / static_cast<float> (designWidth);
    content->setBounds (0, 0, designWidth, designHeight);
    content->setTransform (juce::AffineTransform::scale (scale));
}
