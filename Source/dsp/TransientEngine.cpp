#include "TransientEngine.h"

#include <algorithm>

namespace impact
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;
    }

    // -----------------------------------------------------------------------
    // EnvelopeFollower
    // -----------------------------------------------------------------------
    void EnvelopeFollower::prepare (double fs, float attackSec, float releaseSec)
    {
        attackCoeff  = static_cast<float> (1.0 - std::exp (-1.0 / (attackSec * fs)));
        releaseCoeff = static_cast<float> (1.0 - std::exp (-1.0 / (releaseSec * fs)));
        env = 0.0f;
    }

    void EnvelopeFollower::reset() { env = 0.0f; }

    float EnvelopeFollower::process (float x)
    {
        const float a = std::fabs (x);
        env += (a > env ? attackCoeff : releaseCoeff) * (a - env);
        return env;
    }

    // -----------------------------------------------------------------------
    // BandShaper
    // -----------------------------------------------------------------------
    void BandShaper::prepare (double fs)
    {
        slow.prepare (fs, 0.005f, 0.150f);   // sustain body
        fast.prepare (fs, 0.0005f, 0.025f);  // punch transient
        reset();
    }

    void BandShaper::reset()
    {
        slow.reset();
        fast.reset();
    }

    void BandShaper::setParams (float a, float s, float punchAmount)
    {
        attack  = a;
        sustain = s;
        punch   = punchAmount;
    }

    float BandShaper::process (float x)
    {
        const float s  = slow.process (x);   // sustain envelope
        const float t  = x - s;              // transient
        const float f  = fast.process (x);   // fast envelope
        const float ft = x - f;              // punch transient

        return x + s * sustain + t * attack + ft * punch;
    }

    // -----------------------------------------------------------------------
    // Biquad
    // -----------------------------------------------------------------------
    void Biquad::reset() { z1 = z2 = 0.0f; }

    void Biquad::setLowpass (double fs, float fc)
    {
        const float w0 = 2.0f * kPi * fc / static_cast<float> (fs);
        const float cw = std::cos (w0);
        const float sw = std::sin (w0);
        const float alpha = sw / (2.0f * 0.70710678f);
        const float a0 = 1.0f + alpha;

        b0 = ((1.0f - cw) / 2.0f) / a0;
        b1 = (1.0f - cw) / a0;
        b2 = ((1.0f - cw) / 2.0f) / a0;
        a1 = (-2.0f * cw) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void Biquad::setHighpass (double fs, float fc)
    {
        const float w0 = 2.0f * kPi * fc / static_cast<float> (fs);
        const float cw = std::cos (w0);
        const float sw = std::sin (w0);
        const float alpha = sw / (2.0f * 0.70710678f);
        const float a0 = 1.0f + alpha;

        b0 = ((1.0f + cw) / 2.0f) / a0;
        b1 = -(1.0f + cw) / a0;
        b2 = ((1.0f + cw) / 2.0f) / a0;
        a1 = (-2.0f * cw) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    float Biquad::process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    // -----------------------------------------------------------------------
    // TransientEngine
    // -----------------------------------------------------------------------
    void TransientEngine::prepare (double sampleRate, int /*maxBlockSize*/)
    {
        fs = sampleRate;

        for (auto& f : lp) f.reset();
        for (auto& f : hp) f.reset();

        for (auto& s : shaper)
            for (auto& b : s)
                b.prepare (fs);

        vizFollower.prepare (fs, 0.005f, 0.150f);

        vizInBuf.assign (kVizSize, 0.0f);
        vizTransBuf.assign (kVizSize, 0.0f);
        vizWrite.store (0);

        updateCoeffs (fs);
        reset();
    }

    void TransientEngine::reset()
    {
        for (auto& f : lp) f.reset();
        for (auto& f : hp) f.reset();
        for (auto& s : shaper)
            for (auto& b : s)
                b.reset();
        vizFollower.reset();

        transientLevel.store (0.0f);
        for (auto& l : bandLevel) l.store (0.0f);
        vizWrite.store (0);
    }

    void TransientEngine::setParams (const Params& pp)
    {
        if (pp == p)
            return;   // nothing changed -> skip re-computing crossovers/band gains
        p = pp;
        updateCoeffs (fs);
    }

    void TransientEngine::updateCoeffs (double sampleRate)
    {
        // Fixed crossovers: low/mid 200 Hz, mid/high 3 kHz.
        for (auto& f : lp) f.setLowpass (sampleRate, 200.0f);
        for (auto& f : hp) f.setHighpass (sampleRate, 3000.0f);

        for (int ch = 0; ch < 2; ++ch)
        {
            shaper[ch][0].setParams (p.lowAttack,  p.lowSustain,  p.punch);
            shaper[ch][1].setParams (p.midAttack,  p.midSustain,  p.punch);
            shaper[ch][2].setParams (p.highAttack, p.highSustain, p.punch);
        }
    }

    void TransientEngine::process (const float* inL, const float* inR,
                                   float* outL, float* outR, int numSamples)
    {
        float maxBand[kNumBands] = { 0.0f, 0.0f, 0.0f };
        float maxTransient = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float il = inL[i];
            const float ir = inR[i];
            const float mono = 0.5f * (il + ir);

            // Full-band transient for the visualizer.
            const float slowV = vizFollower.process (mono);
            const float transV = mono - slowV;
            maxTransient = std::max (maxTransient, std::fabs (transV));

            int w = vizWrite.load();
            vizInBuf[static_cast<size_t> (w)] = mono;
            vizTransBuf[static_cast<size_t> (w)] = transV;
            vizWrite.store ((w + 1) % kVizSize);

            // Per channel: 3-band split + per-band shaping.
            float out[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                const float x = (ch == 0) ? il : ir;

                const float low  = lp[ch].process (x);
                const float high = hp[ch].process (x);
                const float mid  = x - low - high;

                const float l = shaper[ch][0].process (low);
                const float m = shaper[ch][1].process (mid);
                const float h = shaper[ch][2].process (high);

                maxBand[0] = std::max (maxBand[0], std::fabs (l));
                maxBand[1] = std::max (maxBand[1], std::fabs (m));
                maxBand[2] = std::max (maxBand[2], std::fabs (h));

                const float wet = l + m + h;
                out[ch] = ((1.0f - p.mix) * x + p.mix * wet) * p.output;
            }

            outL[i] = std::tanh (out[0]);
            outR[i] = std::tanh (out[1]);
        }

        for (int b = 0; b < kNumBands; ++b)
            bandLevel[static_cast<size_t> (b)].store (maxBand[b]);
        transientLevel.store (maxTransient);
    }

} // namespace impact
