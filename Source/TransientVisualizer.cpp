#include "TransientVisualizer.h"

#include <algorithm>
#include <cmath>

namespace
{
    void buildWaveformPath (juce::Path& p, const std::vector<float>& buf, int writePos,
                            juce::Rectangle<float> b)
    {
        const int n = static_cast<int> (buf.size());
        if (n == 0)
            return;

        const float midY = b.getCentreY();
        const float amp  = b.getHeight() * 0.45f;

        for (int i = 0; i < n; ++i)
        {
            const int idx = (writePos + i) % n;
            const float x = b.getX() + static_cast<float> (i) / (n - 1) * b.getWidth();
            const float y = midY - buf[static_cast<size_t> (idx)] * amp;

            if (i == 0)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }
    }
}

TransientVisualizer::TransientVisualizer (ImpactAudioProcessor& p)
    : proc (p)
{
    startTimerHz (60);
}

TransientVisualizer::~TransientVisualizer()
{
    stopTimer();
}

float TransientVisualizer::nextRandom()
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return static_cast<float> (rngState >> 8) * (1.0f / 16777216.0f);
}

void TransientVisualizer::timerCallback()
{
    time += 1.0f / 60.0f;
    transient = proc.getEngine().getTransientLevel();
    update();
    repaint();
}

void TransientVisualizer::update()
{
    const int target = juce::roundToInt (kMaxParticles * juce::jmin (1.0f, transient * 2.0f));
    while (static_cast<int> (particles.size()) < target)
        spawnParticle();

    for (auto& pt : particles)
    {
        pt.life -= 1.0f / 60.0f;
        pt.x += pt.vx;
        pt.y += pt.vy;
        pt.vx *= 0.98f;
        pt.vy *= 0.98f;
    }

    particles.erase (std::remove_if (particles.begin(), particles.end(),
                                     [] (const Particle& p) { return p.life <= 0.0f; }),
                     particles.end());
}

void TransientVisualizer::spawnParticle()
{
    const auto b = getLocalBounds().toFloat();
    Particle pt;
    pt.x = b.getX() + nextRandom() * b.getWidth();
    pt.y = b.getCentreY() + (nextRandom() - 0.5f) * b.getHeight();
    pt.vx = (nextRandom() - 0.5f) * 1.2f;
    pt.vy = (nextRandom() - 0.5f) * 1.2f;
    pt.life = 0.4f + nextRandom() * 1.0f;
    pt.maxLife = pt.life;
    pt.size = 1.5f + 3.0f * nextRandom();
    pt.hue = std::fmod (0.50f + time * 0.03f + nextRandom() * 0.10f, 1.0f);
    particles.push_back (pt);
}

void TransientVisualizer::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    g.setColour (SympathyLookAndFeel::panel);
    g.fillRoundedRectangle (b, 8.0f);
    g.setColour (SympathyLookAndFeel::border);
    g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.0f);

    auto& engine = proc.getEngine();
    auto plot = b.reduced (12.0f, 10.0f);

    auto header = plot.removeFromTop (16.0f);
    g.setFont (juce::Font (12.0f, juce::Font::bold));
    g.setColour (SympathyLookAndFeel::accent);
    g.drawText ("INPUT  +  TRANSIENT", header.removeFromLeft (200), juce::Justification::centredLeft, false);
    g.setFont (juce::Font (10.0f, juce::Font::plain));
    g.setColour (SympathyLookAndFeel::muted);
    g.drawText ("cyan = transient  |  white = signal", header, juce::Justification::centredRight, false);

    // Input waveform.
    juce::Path wave;
    buildWaveformPath (wave, engine.vizIn(), engine.vizWritePos(), plot);
    g.setColour (SympathyLookAndFeel::text.withAlpha (0.75f));
    g.strokePath (wave, juce::PathStrokeType (1.2f));

    // Transient component (accent).
    juce::Path trans;
    buildWaveformPath (trans, engine.vizTransient(), engine.vizWritePos(), plot);
    g.setColour (SympathyLookAndFeel::accent.withAlpha (0.30f));
    g.strokePath (trans, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (SympathyLookAndFeel::accent);
    g.strokePath (trans, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Particles.
    for (const auto& pt : particles)
    {
        const float a = juce::jlimit (0.0f, 1.0f, pt.life / pt.maxLife);
        const juce::Colour c = juce::Colour::fromHSV (pt.hue, 0.8f, 0.95f, a);
        g.setColour (c);
        g.fillEllipse (pt.x - pt.size * 0.5f, pt.y - pt.size * 0.5f, pt.size, pt.size);
    }
}
