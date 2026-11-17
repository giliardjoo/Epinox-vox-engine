#include "PluginEditor.h"
#include "UI/Widgets.h"

#if EPINOX_HAS_LOGO
 #include "BinaryData.h"
#endif

using namespace epinox;
using namespace epinox::colours;
using namespace epinox::ui;

//==============================================================================
// All controls live in this 1100 x 740 design space; the editor scales it to the window.
class VoxContent : public juce::Component, private juce::Timer
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
          pitchKnob    (p.apvts, pid::pitch,   "PITCH",   " st", 1),
          formantKnob  (p.apvts, pid::formant, "FORMANT", " st", 1),
          bpmKnob      (p.apvts, pid::bpm,     "BPM",     "",    1),
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

        generateButton.setComponentID ("generate");
        generateButton.setButtonText ("GENERATE");
        generateButton.onClick = [this] { onGenerate(); };

        prevButton.setButtonText ("<");
        nextButton.setButtonText (">");
        saveButton.setButtonText ("SAVE");
        openButton.setButtonText ("OPEN");
        prevButton.onClick = [this] { if (proc.presets.loadRelative (-1)) afterPresetLoad(); };
        nextButton.onClick = [this] { if (proc.presets.loadRelative (+1)) afterPresetLoad(); };
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
                 &genreList, &phrase, &generateButton,
                 &voiceCombo, &emotionCombo, &styleCombo, &keyCombo, &scaleCombo, &pitchKnob, &formantKnob, &bpmKnob,
                 &mixKnob, &outKnob, &waveform, &meterIn, &meterOut,
                 &chopPanel, &transformPanel, &effectsPanel, &spatialPanel,
                 &prevButton, &nextButton, &saveButton, &openButton, &powerButton,
                 &presetLabel, &statusLabel })
            addAndMakeVisible (c);

        startTimerHz (30);
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
        g.drawText ("EPINOX  |  ONE PHRASE. INFINITE POSSIBILITIES.",
                    juce::Rectangle<float> (area.getRight() - 460.0f, 718.0f, 444.0f, 18.0f),
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

        genreList.setBounds (16, 80, 204, 392);

        phrase.setBounds (236, 80, 490, 52);
        generateButton.setBounds (738, 80, 110, 52);

        voiceCombo.setBounds   (236, 144, 196, 46);
        emotionCombo.setBounds (444, 144, 196, 46);
        styleCombo.setBounds   (652, 144, 196, 46);

        pitchKnob.setBounds   (236, 200, 196, 128);
        formantKnob.setBounds (444, 200, 196, 128);
        bpmKnob.setBounds     (652, 200, 196, 128);

        keyCombo.setBounds   (236, 332, 196, 46);
        scaleCombo.setBounds (444, 332, 404, 46);

        waveform.setBounds (236, 386, 612, 86);

        mixKnob.setBounds (864, 80, 220, 250);
        meterIn.setBounds  (878, 342, 50, 130);
        meterOut.setBounds (934, 342, 50, 130);
        outKnob.setBounds  (994, 354, 90, 112);

        const int w = 258, gap = 12;
        chopPanel.setBounds      (16 + 0 * (w + gap), 486, w, 228);
        transformPanel.setBounds (16 + 1 * (w + gap), 486, w, 228);
        effectsPanel.setBounds   (16 + 2 * (w + gap), 486, w, 228);
        spatialPanel.setBounds   (16 + 3 * (w + gap), 486, w, 228);

        statusLabel.setBounds (16, 718, 640, 18);
    }

private:
    void timerCallback() override
    {
        meterIn.setLevels  (proc.inPeak.load(),  proc.inRms.load());
        meterOut.setLevels (proc.outPeak.load(), proc.outRms.load());

        if (proc.clipped.exchange (false))
            meterOut.triggerClip();

        chopPanel.refresh();
        transformPanel.refresh();
        effectsPanel.refresh();
        spatialPanel.refresh();

        generateButton.setEnabled (! proc.isGenerating());

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

    void afterPresetLoad()
    {
        phrase.setText (apvts.state.getProperty ("phrase").toString(), false);
        presetLabel.setText (proc.presets.getCurrentName(), juce::dontSendNotification);
        setStatus ({}, false);
    }

    void onGenerate()
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

        // When the host reports a tempo, the vocal is generated at that tempo and the BPM knob follows it.
        const double hostBpm = proc.getHostBpm();

        if (hostBpm >= 40.0 && hostBpm <= 220.0)
        {
            request.bpm = hostBpm;

            if (auto* bpmParam = apvts.getParameter (pid::bpm))
                bpmParam->setValueNotifyingHost (bpmParam->convertTo0to1 (static_cast<float> (hostBpm)));
        }

        request.variation = proc.nextVariation();

        if (proc.requestGeneration (request))
        {
            lastStatusVersion = proc.getGenerationStatusVersion();
            setStatus ("Generating vocal...", false);
        }
        else
        {
            setStatus ("Still generating, one moment.", false);
        }
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

    GenreList genreList;
    juce::TextEditor phrase;
    juce::TextButton generateButton, prevButton, nextButton, saveButton, openButton;
    juce::ToggleButton powerButton;
    juce::Label presetLabel, statusLabel;

    NeonCombo voiceCombo, emotionCombo, styleCombo, keyCombo, scaleCombo;
    NeonKnob pitchKnob, formantKnob, bpmKnob, mixKnob, outKnob;
    WaveformView waveform;
    MeterBar meterIn, meterOut;
    ModulePanel chopPanel, transformPanel, effectsPanel, spatialPanel;

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
