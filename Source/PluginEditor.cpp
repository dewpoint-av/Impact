#include "PluginEditor.h"

#include "TransientVisualizer.h"
#include "HoloLogo.h"
#include "AudioSpheres.h"

#include <cmath>
#include <functional>

namespace
{
    juce::String signedPctText (float v) { return (v >= 0.0f ? "+" : "") + juce::String (v * 100.0f, 0) + " %"; }
    juce::String pctText       (float v) { return juce::String (v * 100.0f, 0) + " %"; }
    juce::String gainText      (float v) { return juce::String (v, 2) + "x"; }
    juce::String msText        (float v) { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; }
    juce::String hzText        (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz"
                                                               : juce::String (v, 0) + " Hz"; }

    class Knob : public juce::Component,
                 private juce::AudioProcessorValueTreeState::Listener,
                 private juce::AsyncUpdater
    {
    public:
        Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId,
              const juce::String& name, std::function<juce::String (float)> textFnIn)
            : stateRef (state), param (paramId), textFn (textFnIn)
        {
            slider.setSliderStyle (juce::Slider::Rotary);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setRotaryParameters ({ juce::MathConstants<float>::pi * 1.25f,
                                          juce::MathConstants<float>::pi * 2.75f, true });
            addAndMakeVisible (slider);

            nameLabel.setText (name, juce::dontSendNotification);
            nameLabel.setFont (juce::FontOptions (11.0f, juce::Font::plain));
            nameLabel.setColour (juce::Label::textColourId, SympathyLookAndFeel::muted);
            nameLabel.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (nameLabel);

            valueLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            valueLabel.setColour (juce::Label::textColourId, SympathyLookAndFeel::text);
            valueLabel.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (valueLabel);

            attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
            stateRef.addParameterListener (param, this);
            refreshLabel();
        }

        ~Knob() override { stateRef.removeParameterListener (param, this); }

        void retarget (const juce::String& newParamId)
        {
            if (param == newParamId) return;
            stateRef.removeParameterListener (param, this);
            param = newParamId;
            attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (stateRef, param, slider);
            stateRef.addParameterListener (param, this);
            refreshLabel();
        }

        void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
        void handleAsyncUpdate() override { refreshLabel(); }

        void resized() override
        {
            auto b = getLocalBounds();
            nameLabel.setBounds (b.removeFromTop (16));
            valueLabel.setBounds (b.removeFromBottom (16));
            slider.setBounds (b);
        }

    private:
        void refreshLabel()
        {
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (stateRef.getParameter (param)))
                if (textFn)
                    valueLabel.setText (textFn (p->getNormalisableRange().convertFrom0to1 (p->getValue())),
                                        juce::dontSendNotification);
        }

        juce::AudioProcessorValueTreeState& stateRef;
        juce::String param;
        std::function<juce::String (float)> textFn;
        juce::Slider slider;
        juce::Label nameLabel, valueLabel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    class ToggleButton : public juce::Component,
                         private juce::AudioProcessorValueTreeState::Listener,
                         private juce::AsyncUpdater
    {
    public:
        ToggleButton (const juce::String& label, const juce::String& paramId,
                      juce::AudioProcessorValueTreeState& st)
            : name (label), param (paramId), state (st)
        {
            state.addParameterListener (param, this);
        }
        ~ToggleButton() override { state.removeParameterListener (param, this); }

        void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
        void handleAsyncUpdate() override { repaint(); }

        void paint (juce::Graphics& g) override
        {
            const bool on = isOn();
            auto r = getLocalBounds().toFloat().reduced (1.5f);
            const juce::Colour col = on ? SympathyLookAndFeel::accent : SympathyLookAndFeel::muted;
            if (on)
            {
                g.setColour (col.withAlpha (0.15f)); g.fillRoundedRectangle (r, 6.0f);
                g.setColour (col.withAlpha (0.65f)); g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.5f);
            }
            else
            {
                g.setColour (SympathyLookAndFeel::panel); g.fillRoundedRectangle (r, 6.0f);
                g.setColour (SympathyLookAndFeel::border); g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
            }
            g.setFont (juce::FontOptions (10.0f, on ? juce::Font::bold : juce::Font::plain));
            g.setColour (on ? SympathyLookAndFeel::text : SympathyLookAndFeel::muted);
            g.drawText (on ? name + " ON" : name + " OFF", r, juce::Justification::centred, false);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            if (auto* p = state.getParameter (param))
                p->setValueNotifyingHost (isOn() ? 0.0f : 1.0f);
        }
        bool isOn() const { return state.getRawParameterValue (param)->load() > 0.5f; }

    private:
        juce::String name, param;
        juce::AudioProcessorValueTreeState& state;
    };

    class Panel : public juce::Component
    {
    public:
        explicit Panel (const juce::String& title)
        {
            titleLabel.setText (title, juce::dontSendNotification);
            titleLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            titleLabel.setColour (juce::Label::textColourId, SympathyLookAndFeel::muted);
            titleLabel.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (titleLabel);
        }
        void paint (juce::Graphics& g) override { SympathyLookAndFeel::drawPanelBackground (g, getLocalBounds()); }
        void resized() override { titleLabel.setBounds (0, 8, getWidth(), 16); }
        juce::Label titleLabel;
    };

    class LevelMeter : public juce::Component, private juce::Timer
    {
    public:
        LevelMeter (ImpactAudioProcessor& p, int bandIndex) : proc (p), band (bandIndex) { startTimerHz (20); }
        ~LevelMeter() override { stopTimer(); }
        void timerCallback() override { repaint(); }
        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setColour (SympathyLookAndFeel::track);
            g.fillRoundedRectangle (b, 3.0f);
            const float lv = juce::jlimit (0.0f, 1.4f, proc.getEngine().getBandLevel (band));
            const float w = b.getWidth() * (lv / 1.4f);
            g.setColour (SympathyLookAndFeel::accent);
            g.fillRoundedRectangle (juce::Rectangle<float> (b.getX(), b.getY(), w, b.getHeight()), 3.0f);
        }
    private:
        ImpactAudioProcessor& proc; int band;
    };

    class BandScope : public juce::Component, private juce::Timer
    {
    public:
        BandScope (ImpactAudioProcessor& p, int bandIndex) : proc (p), band (bandIndex) { startTimerHz (20); }
        ~BandScope() override { stopTimer(); }
        void timerCallback() override { repaint(); }
        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (SympathyLookAndFeel::panel.brighter (0.07f));
            g.fillRoundedRectangle (b, 5.0f);
            const auto& buf = proc.getEngine().vizBand (band);
            const int wp = proc.getEngine().vizWritePos();
            const int n = static_cast<int> (buf.size());
            if (n == 0) return;
            const float midY = b.getCentreY();
            const float amp  = b.getHeight() * 0.48f;
            juce::Path p;
            for (int i = 0; i < n; ++i)
            {
                const int idx = (wp + i) % n;
                const float x = b.getX() + static_cast<float> (i) / (n - 1) * b.getWidth();
                const float y = midY - buf[static_cast<size_t> (idx)] * amp;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (SympathyLookAndFeel::accent.withAlpha (0.80f));
            g.strokePath (p, juce::PathStrokeType (1.3f));
        }
    private:
        ImpactAudioProcessor& proc; int band;
    };

    void layoutKnobs (std::vector<std::unique_ptr<Knob>>& knobs, int cols,
                      juce::Rectangle<int> area, int gapX, int gapY)
    {
        if (knobs.empty()) return;
        const int rows = static_cast<int> (std::ceil (knobs.size() / static_cast<double> (cols)));
        const int cw = (area.getWidth()  - gapX * (cols - 1)) / cols;
        const int ch = (area.getHeight() - gapY * (rows - 1)) / rows;
        const int knobH = juce::jmin (ch, 104);
        int idx = 0;
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols && idx < static_cast<int> (knobs.size()); ++c, ++idx)
            {
                const int x = area.getX() + c * (cw + gapX);
                const int y = area.getY() + r * (ch + gapY);
                knobs[static_cast<size_t> (idx)]->setBounds (x, y + (ch - knobH) / 2, cw, knobH);
            }
    }
}

// ===========================================================================
class ImpactAudioProcessorEditor::Impl
{
public:
    explicit Impl (ImpactAudioProcessor& proc) : processor (proc) { build(); }

    ImpactAudioProcessor& processor;

    std::unique_ptr<HoloTitle> title;
    juce::Label subtitle, brand, footer;

    std::unique_ptr<TransientVisualizer> visualizer;
    std::unique_ptr<SpectrogramSphere> leftSphere;
    std::unique_ptr<SpectrumSphere> rightSphere;

    static constexpr int kBands = 4;
    std::array<std::unique_ptr<Panel>, kBands> bandPanels;
    std::array<std::vector<std::unique_ptr<Knob>>, kBands> bandKnobs;
    std::array<std::unique_ptr<ToggleButton>, kBands> bandEnable;
    std::array<std::unique_ptr<BandScope>, kBands> bandScopes;
    std::array<std::unique_ptr<LevelMeter>, kBands> bandMeters;

    std::unique_ptr<Panel> globalPanel;
    std::vector<std::unique_ptr<Knob>> globalKnobs;   // xover1..3, punch, mix, output
    std::unique_ptr<juce::ComboBox> soloBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> soloAttachment;
    std::unique_ptr<ToggleButton> autoGainToggle;

    void build()
    {
        title = std::make_unique<HoloTitle> ("IMPACT", 42.0f, true);

        subtitle.setText ("MULTIBAND TRANSIENT SHAPER", juce::dontSendNotification);
        subtitle.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        subtitle.setColour (juce::Label::textColourId, SympathyLookAndFeel::muted);
        subtitle.setJustificationType (juce::Justification::centred);

        brand.setText ("DEWPOINT AV", juce::dontSendNotification);
        brand.setFont (monumentFont (13.0f, false));
        brand.setColour (juce::Label::textColourId, SympathyLookAndFeel::accent);
        brand.setJustificationType (juce::Justification::centred);

        footer.setText (juce::String ("DEWPOINT AV  -  v") + juce::String (JucePlugin_VersionString), juce::dontSendNotification);
        footer.setFont (monumentFont (11.0f, false));
        footer.setColour (juce::Label::textColourId, SympathyLookAndFeel::muted);
        footer.setJustificationType (juce::Justification::centred);

        visualizer = std::make_unique<TransientVisualizer> (processor);
        leftSphere = std::make_unique<SpectrogramSphere> (
            &processor.getEngine().vizIn(), [this] { return processor.getEngine().vizWritePos(); });
        rightSphere = std::make_unique<SpectrumSphere> (
            &processor.getEngine().vizIn(), [this] { return processor.getEngine().vizWritePos(); });

        const char* bandNames[kBands] = { "LOW", "LOW-MID", "MID", "HIGH" };
        for (int i = 0; i < kBands; ++i)
        {
            const juce::String s = juce::String (i + 1);
            bandPanels[i] = std::make_unique<Panel> (bandNames[i]);
            bandEnable[i] = std::make_unique<ToggleButton> ("BAND", "b" + s + "on", processor.getParameters());
            bandKnobs[i].push_back (std::make_unique<Knob> (processor.getParameters(), "b" + s + "atk",     "ATTACK",  signedPctText));
            bandKnobs[i].push_back (std::make_unique<Knob> (processor.getParameters(), "b" + s + "atkTime", "A.TIME",  msText));
            bandKnobs[i].push_back (std::make_unique<Knob> (processor.getParameters(), "b" + s + "sus",     "SUSTAIN", signedPctText));
            bandKnobs[i].push_back (std::make_unique<Knob> (processor.getParameters(), "b" + s + "susTime", "S.TIME",  msText));
            bandScopes[i] = std::make_unique<BandScope> (processor, i);
            bandMeters[i] = std::make_unique<LevelMeter> (processor, i);
        }

        globalPanel = std::make_unique<Panel> ("GLOBAL");
        soloBox = std::make_unique<juce::ComboBox> ("Solo");
        soloBox->addItemList (juce::StringArray { "Solo: Off", "Solo: Transient", "Solo: Body" }, 1);
        soloBox->setSelectedId (1, juce::dontSendNotification);
        soloBox->setJustificationType (juce::Justification::centred);
        soloAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            processor.getParameters(), "solo", *soloBox);

        globalKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "xover1", "XOVER 1", hzText));
        globalKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "xover2", "XOVER 2", hzText));
        globalKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "xover3", "XOVER 3", hzText));
        globalKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "punch",  "PUNCH",   pctText));
        globalKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "mix",    "MIX",     pctText));
        globalKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "output", "OUTPUT",  gainText));

        autoGainToggle = std::make_unique<ToggleButton> ("AUTO GAIN", "autoGain", processor.getParameters());
    }

    void attachTo (juce::Component& root)
    {
        root.addAndMakeVisible (*title);
        root.addAndMakeVisible (subtitle);
        root.addAndMakeVisible (brand);
        root.addAndMakeVisible (footer);
        root.addAndMakeVisible (*leftSphere);
        root.addAndMakeVisible (*rightSphere);
        root.addAndMakeVisible (*visualizer);

        for (int i = 0; i < kBands; ++i)
        {
            root.addAndMakeVisible (*bandPanels[i]);
            bandPanels[i]->addAndMakeVisible (*bandEnable[i]);
            bandPanels[i]->addAndMakeVisible (*bandScopes[i]);
            for (auto& k : bandKnobs[i]) bandPanels[i]->addAndMakeVisible (*k);
            bandPanels[i]->addAndMakeVisible (*bandMeters[i]);
        }

        root.addAndMakeVisible (*globalPanel);
        globalPanel->addAndMakeVisible (*soloBox);
        globalPanel->addAndMakeVisible (*autoGainToggle);
        for (auto& k : globalKnobs) globalPanel->addAndMakeVisible (*k);
    }

    void resized (juce::Rectangle<int> area)
    {
        auto b = area;
        footer.setBounds (b.removeFromBottom (18));
        b.removeFromBottom (2);

        auto header = b.removeFromTop (88);
        const int sphere = 72, pad = 26;
        const int sphereY = header.getY() + (header.getHeight() - sphere) / 2;
        leftSphere->setBounds  (header.getX() + pad, sphereY, sphere, sphere);
        rightSphere->setBounds (header.getRight() - pad - sphere, sphereY, sphere, sphere);
        auto head = header.withTrimmedLeft (sphere + pad).withTrimmedRight (sphere + pad);
        brand.setBounds (head.removeFromTop (18));
        title->setBounds (head.removeFromTop (50));
        subtitle.setBounds (head);

        b.removeFromTop (6);
        visualizer->setBounds (b.removeFromTop (168));
        b.removeFromTop (10);

        // Global strip across the bottom.
        auto global = b.removeFromBottom (132);
        b.removeFromBottom (10);

        // 4 band columns fill the middle.
        const int gap = 10;
        const int colW = (b.getWidth() - (kBands - 1) * gap) / kBands;
        for (int i = 0; i < kBands; ++i)
        {
            bandPanels[i]->setBounds (b.removeFromLeft (colW));
            if (i < kBands - 1) b.removeFromLeft (gap);

            auto p = bandPanels[i]->getLocalBounds().reduced (10);
            p.removeFromTop (26);
            bandEnable[i]->setBounds (p.removeFromTop (22).reduced (6, 1));
            p.removeFromTop (4);
            bandScopes[i]->setBounds (p.removeFromTop (38));
            p.removeFromTop (4);
            bandMeters[i]->setBounds (p.removeFromBottom (12));
            p.removeFromBottom (4);
            layoutKnobs (bandKnobs[i], 2, p, 6, 4);
        }

        // Global panel: solo + autogain row, then 6 knobs.
        globalPanel->setBounds (global);
        auto gb = globalPanel->getLocalBounds().reduced (12);
        gb.removeFromTop (22);
        auto topRow = gb.removeFromTop (26);
        soloBox->setBounds (topRow.removeFromLeft (200).reduced (2, 2));
        autoGainToggle->setBounds (topRow.removeFromRight (160).reduced (2, 2));
        gb.removeFromTop (6);
        layoutKnobs (globalKnobs, 6, gb, 8, 0);
    }
};

// ===========================================================================
ImpactAudioProcessorEditor::ImpactAudioProcessorEditor (ImpactAudioProcessor& p)
    : AudioProcessorEditor (&p)
{
    setLookAndFeel (&lookAndFeel);
    impl = std::make_unique<Impl> (p);
    impl->attachTo (*this);
    setResizable (true, true);
    setResizeLimits (960, 700, 1700, 1150);
    setSize (1080, 780);
}

ImpactAudioProcessorEditor::~ImpactAudioProcessorEditor() { setLookAndFeel (nullptr); }

void ImpactAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (SympathyLookAndFeel::background);
}

void ImpactAudioProcessorEditor::resized()
{
    impl->resized (getLocalBounds().reduced (16));
}
