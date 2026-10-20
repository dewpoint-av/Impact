#include "PluginEditor.h"

#include "TransientVisualizer.h"
#include "HoloLogo.h"

#include <cmath>
#include <functional>

namespace
{
    juce::String signedPctText (float v) { return (v >= 0.0f ? "+" : "") + juce::String (v * 100.0f, 0) + " %"; }
    juce::String pctText       (float v) { return juce::String (v * 100.0f, 0) + " %"; }
    juce::String gainText      (float v) { return juce::String (v, 2) + "x"; }

    class Knob : public juce::Component,
                 private juce::AudioProcessorValueTreeState::Listener
    {
    public:
        Knob (juce::AudioProcessorValueTreeState& state,
              const juce::String& paramId,
              const juce::String& name,
              std::function<juce::String (float)> textFn)
            : stateRef (state), param (paramId), textFn (textFn)
        {
            slider.setSliderStyle (juce::Slider::Rotary);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            addAndMakeVisible (slider);

            nameLabel.setText (name, juce::dontSendNotification);
            nameLabel.setFont (juce::Font (11.0f, juce::Font::plain));
            nameLabel.setColour (juce::Label::textColourId, SympathyLookAndFeel::muted);
            nameLabel.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (nameLabel);

            valueLabel.setFont (juce::Font (11.0f, juce::Font::bold));
            valueLabel.setColour (juce::Label::textColourId, SympathyLookAndFeel::text);
            valueLabel.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (valueLabel);

            attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                state, paramId, slider);

            stateRef.addParameterListener (param, this);
            refreshLabel();
        }

        ~Knob() override { stateRef.removeParameterListener (param, this); }

        void parameterChanged (const juce::String&, float) override { refreshLabel(); }

        void resized() override
        {
            auto b = getLocalBounds();
            nameLabel.setBounds (b.removeFromTop (18));
            valueLabel.setBounds (b.removeFromBottom (18));
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
        juce::Label nameLabel;
        juce::Label valueLabel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    class Panel : public juce::Component
    {
    public:
        explicit Panel (const juce::String& title)
        {
            titleLabel.setText (title, juce::dontSendNotification);
            titleLabel.setFont (juce::Font (13.0f, juce::Font::bold));
            titleLabel.setColour (juce::Label::textColourId, SympathyLookAndFeel::muted);
            titleLabel.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (titleLabel);
        }

        void paint (juce::Graphics& g) override
        {
            SympathyLookAndFeel::drawPanelBackground (g, getLocalBounds());
        }

        void resized() override
        {
            titleLabel.setBounds (0, 8, getWidth(), 18);
        }

        juce::Label titleLabel;
    };

    // A thin live level bar for one band.
    class LevelMeter : public juce::Component,
                       private juce::Timer
    {
    public:
        LevelMeter (ImpactAudioProcessor& p, int bandIndex)
            : proc (p), band (bandIndex)
        {
            startTimerHz (30);
        }

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
        ImpactAudioProcessor& proc;
        int band;
    };

    void layoutKnobs (std::vector<std::unique_ptr<Knob>>& knobs, int cols,
                      juce::Rectangle<int> area, int gapX, int gapY)
    {
        if (knobs.empty())
            return;

        const int rows = static_cast<int> (std::ceil (knobs.size() / static_cast<double> (cols)));
        const int cw = (area.getWidth()  - gapX * (cols - 1)) / cols;
        const int ch = (area.getHeight() - gapY * (rows - 1)) / rows;
        const int knobH = juce::jmin (ch, 96);

        int idx = 0;
        for (int r = 0; r < rows; ++r)
        {
            for (int c = 0; c < cols && idx < static_cast<int> (knobs.size()); ++c, ++idx)
            {
                const int x = area.getX() + c * (cw + gapX);
                const int y = area.getY() + r * (ch + gapY);
                knobs[static_cast<size_t> (idx)]->setBounds (x, y + (ch - knobH) / 2, cw, knobH);
            }
        }
    }
}

// ===========================================================================
class ImpactAudioProcessorEditor::Impl
{
public:
    explicit Impl (ImpactAudioProcessor& proc)
        : processor (proc)
    {
        build();
    }

    ImpactAudioProcessor& processor;

    std::unique_ptr<HoloTitle> title;
    juce::Label subtitle;
    juce::Label brand;
    juce::Label footer;

    std::unique_ptr<TransientVisualizer> visualizer;

    std::array<std::unique_ptr<Panel>, 3> bandPanels;
    std::array<std::vector<std::unique_ptr<Knob>>, 3> bandKnobs;
    std::array<std::unique_ptr<LevelMeter>, 3> bandMeters;

    std::unique_ptr<Panel> globalPanel;
    std::vector<std::unique_ptr<Knob>> globalKnobs;

    void build()
    {
        title = std::make_unique<HoloTitle> ("IMPACT", 42.0f, true);

        subtitle.setText ("MULTIBAND TRANSIENT SHAPER", juce::dontSendNotification);
        subtitle.setFont (juce::Font (12.0f, juce::Font::bold));
        subtitle.setColour (juce::Label::textColourId, SympathyLookAndFeel::muted);
        subtitle.setJustificationType (juce::Justification::centredLeft);

        brand.setText ("DEWPOINT AV", juce::dontSendNotification);
        brand.setFont (monumentFont (13.0f, false));
        brand.setColour (juce::Label::textColourId, SympathyLookAndFeel::accent);
        brand.setJustificationType (juce::Justification::centredRight);

        footer.setText (juce::String ("DEWPOINT AV  ·  v") + juce::String (JucePlugin_VersionString), juce::dontSendNotification);
        footer.setFont (monumentFont (11.0f, false));
        footer.setColour (juce::Label::textColourId, SympathyLookAndFeel::muted);
        footer.setJustificationType (juce::Justification::centred);

        visualizer = std::make_unique<TransientVisualizer> (processor);

        const char* bandNames[3] = { "LOW", "MID", "HIGH" };
        const char* attackIds[3]  = { "lowAttack",  "midAttack",  "highAttack"  };
        const char* sustainIds[3] = { "lowSustain", "midSustain", "highSustain" };
        const char* lengthIds[3]  = { "lowLength",  "midLength",  "highLength"  };

        for (int i = 0; i < 3; ++i)
        {
            bandPanels[i] = std::make_unique<Panel> (bandNames[i]);
            bandKnobs[i].push_back (std::make_unique<Knob> (processor.getParameters(),
                attackIds[i], "ATTACK", signedPctText));
            bandKnobs[i].push_back (std::make_unique<Knob> (processor.getParameters(),
                sustainIds[i], "SUSTAIN", signedPctText));
            bandKnobs[i].push_back (std::make_unique<Knob> (processor.getParameters(),
                lengthIds[i], "LENGTH", pctText));
            bandMeters[i] = std::make_unique<LevelMeter> (processor, i);
        }

        globalPanel = std::make_unique<Panel> ("GLOBAL");
        globalKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "punch", "PUNCH", pctText));
        globalKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "mix", "MIX", pctText));
        globalKnobs.push_back (std::make_unique<Knob> (processor.getParameters(), "output", "OUTPUT", gainText));
    }

    void attachTo (juce::Component& root)
    {
        root.addAndMakeVisible (*title);
        root.addAndMakeVisible (subtitle);
        root.addAndMakeVisible (brand);
        root.addAndMakeVisible (footer);
        root.addAndMakeVisible (*visualizer);

        for (int i = 0; i < 3; ++i)
        {
            root.addAndMakeVisible (*bandPanels[i]);
            for (auto& k : bandKnobs[i])
                bandPanels[i]->addAndMakeVisible (*k);
            bandPanels[i]->addAndMakeVisible (*bandMeters[i]);
        }

        root.addAndMakeVisible (*globalPanel);
        for (auto& k : globalKnobs)
            globalPanel->addAndMakeVisible (*k);
    }

    void resized (juce::Rectangle<int> area)
    {
        auto b = area;

        // Footer strip.
        footer.setBounds (b.removeFromBottom (18));
        b.removeFromBottom (2);

        auto header = b.removeFromTop (58);
        title->setBounds (header.removeFromLeft (340));
        brand.setBounds (header.removeFromRight (220));
        subtitle.setBounds (header);

        b.removeFromTop (6);
        visualizer->setBounds (b.removeFromTop (210));
        b.removeFromTop (12);

        // Four panels across the bottom (LOW / MID / HIGH / GLOBAL).
        const int gap = 12;
        const int bandW = (b.getWidth() - 3 * gap) / 4;

        for (int i = 0; i < 3; ++i)
        {
            bandPanels[i]->setBounds (b.removeFromLeft (bandW));
            b.removeFromLeft (gap);

            auto p = bandPanels[i]->getLocalBounds().reduced (12);
            p.removeFromTop (30);
            auto meter = p.removeFromBottom (14);
            bandMeters[i]->setBounds (meter);
            p.removeFromBottom (6);
            layoutKnobs (bandKnobs[i], 3, p, 6, 0);
        }

        globalPanel->setBounds (b);
        auto gb = globalPanel->getLocalBounds().reduced (12);
        gb.removeFromTop (30);
        layoutKnobs (globalKnobs, 3, gb, 6, 0);
    }
};

// ===========================================================================
ImpactAudioProcessorEditor::ImpactAudioProcessorEditor (ImpactAudioProcessor& p)
    : AudioProcessorEditor (&p)
    , processor (p)
{
    setLookAndFeel (&lookAndFeel);

    impl = std::make_unique<Impl> (processor);
    impl->attachTo (*this);

    setResizable (true, true);
    setResizeLimits (720, 480, 1500, 1000);
    setSize (940, 620);
}

ImpactAudioProcessorEditor::~ImpactAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void ImpactAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (
        SympathyLookAndFeel::background.brighter (0.05f), b.getCentreX(), b.getCentreY(),
        SympathyLookAndFeel::background, b.getWidth() * 0.65f, b.getHeight() * 0.65f, true));
    g.fillAll();
}

void ImpactAudioProcessorEditor::resized()
{
    impl->resized (getLocalBounds().reduced (16));
}
