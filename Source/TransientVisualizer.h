#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "CustomLookAndFeel.h"

#include <vector>

// The main oscilloscope: the input waveform with the transient component
// highlighted on top, plus holographic particles that react to transient
// energy.
class TransientVisualizer : public juce::Component,
                            private juce::Timer
{
public:
    explicit TransientVisualizer (ImpactAudioProcessor& p);
    ~TransientVisualizer() override;

    void paint (juce::Graphics&) override;
    void timerCallback() override;

private:
    struct Particle
    {
        float x, y, vx, vy, life, maxLife, size, hue;
    };

    void update();
    void spawnParticle();
    float nextRandom();

    ImpactAudioProcessor& proc;

    std::vector<Particle> particles;
    uint32_t rngState = 0xDEADBEEFu;
    float time = 0.0f;
    float transient = 0.0f;

    static constexpr int kMaxParticles = 150;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransientVisualizer)
};
