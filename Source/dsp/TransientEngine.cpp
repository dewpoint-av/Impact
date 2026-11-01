#include "TransientEngine.h"

namespace impact
{
    namespace { constexpr float kPi = 3.14159265358979323846f;
                constexpr float kLn10over20 = 0.11512925465f; }

    // -----------------------------------------------------------------------
    // Biquad (RBJ, Butterworth Q = 0.7071 for LP/HP so two cascaded = LR4).
    // -----------------------------------------------------------------------
    void Biquad::setLowpass (double fs, float fc)
    {
        const float w0 = 2.0f * kPi * fc / (float) fs;
        const float cw = std::cos (w0), sw = std::sin (w0);
        const float alpha = sw / (2.0f * 0.70710678f);
        const float a0 = 1.0f + alpha;
        b0 = ((1.0f - cw) * 0.5f) / a0;
        b1 =  (1.0f - cw) / a0;
        b2 = b0;
        a1 = (-2.0f * cw) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void Biquad::setHighpass (double fs, float fc)
    {
        const float w0 = 2.0f * kPi * fc / (float) fs;
        const float cw = std::cos (w0), sw = std::sin (w0);
        const float alpha = sw / (2.0f * 0.70710678f);
        const float a0 = 1.0f + alpha;
        b0 = ((1.0f + cw) * 0.5f) / a0;
        b1 = -(1.0f + cw) / a0;
        b2 = b0;
        a1 = (-2.0f * cw) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    // 2nd-order all-pass (Q = 0.7071): the exact phase-compensator for an LR4
    // crossover (LR4 LP + LR4 HP sum to this all-pass).
    void Biquad::setAllpass (double fs, float fc)
    {
        const float w0 = 2.0f * kPi * fc / (float) fs;
        const float cw = std::cos (w0), sw = std::sin (w0);
        const float alpha = sw / (2.0f * 0.70710678f);
        const float a0 = 1.0f + alpha;
        b0 = (1.0f - alpha) / a0;
        b1 = (-2.0f * cw) / a0;
        b2 = (1.0f + alpha) / a0;
        a1 = (-2.0f * cw) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    // -----------------------------------------------------------------------
    // TransientEngine
    // -----------------------------------------------------------------------
    void TransientEngine::prepare (double sampleRate, int /*maxBlockSize*/)
    {
        fs = sampleRate;
        gSmoothCoef = 1.0f - std::exp (-1.0f / (0.0003f * (float) fs));  // ~0.3 ms

        vizFollower.prepare (fs, 0.005f, 0.150f);
        limiter.prepare (fs);
        agc.prepare (fs);

        vizInBuf.assign (kVizSize, 0.0f);
        vizTransBuf.assign (kVizSize, 0.0f);
        for (auto& v : vizBandBuf) v.assign (kVizSize, 0.0f);
        vizWrite.store (0);

        updateCoeffs (fs);
        reset();
    }

    void TransientEngine::reset()
    {
        for (auto& s : split) s.reset();
        for (auto& d : detector) d.reset();
        for (auto& g : smoothG) g = 1.0f;
        limiter.reset();
        agc.reset();
        vizFollower.reset();
        transientLevel.store (0.0f);
        for (auto& l : bandLevel) l.store (0.0f);
        vizWrite.store (0);
    }

    void TransientEngine::setParams (const Params& pp)
    {
        if (pp == p)
            return;
        p = pp;
        updateCoeffs (fs);
    }

    void TransientEngine::updateCoeffs (double sampleRate)
    {
        // Order + space the crossovers.
        float f1 = std::clamp (p.xover1, 30.0f, 18000.0f);
        float f2 = std::clamp (p.xover2, f1 + 20.0f, 19000.0f);
        float f3 = std::clamp (p.xover3, f2 + 20.0f, 20000.0f);

        for (auto& s : split)
            s.setCoeffs (sampleRate, f1, f2, f3);

        for (int b = 0; b < kNumBands; ++b)
            detector[(size_t) b].setTimes (sampleRate, p.band[(size_t) b].attackTime,
                                                        p.band[(size_t) b].sustainTime);
    }

    void TransientEngine::process (const float* inL, const float* inR,
                                   float* outL, float* outR, int numSamples)
    {
        const int   solo   = p.solo;
        const float effMix = (solo != SoloOff) ? 1.0f : p.mix;

        float maxBand[kNumBands] = { 0, 0, 0, 0 };
        float maxTransient = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float il = inL[i];
            const float ir = inR[i];
            const float mono = 0.5f * (il + ir);

            // Full-band transient for the main visualizer.
            const float slowV  = vizFollower.process (mono);
            const float transV = mono - slowV;
            maxTransient = std::max (maxTransient, std::fabs (transV));

            const int w = vizWrite.load();
            vizInBuf[(size_t) w]    = mono;
            vizTransBuf[(size_t) w] = transV;

            float bandsL[kNumBands], bandsR[kNumBands];
            split[0].process (il, bandsL);
            split[1].process (ir, bandsR);

            float wetL = 0.0f, wetR = 0.0f;
            for (int b = 0; b < kNumBands; ++b)
            {
                const auto& bp = p.band[(size_t) b];
                const float mag = std::max (std::fabs (bandsL[b]), std::fabs (bandsR[b]));

                float aN, sN;
                detector[(size_t) b].process (mag, aN, sN);

                float oL, oR;
                if (solo == SoloTransient)
                {
                    oL = bandsL[b] * aN;          oR = bandsR[b] * aN;
                }
                else if (solo == SoloBody)
                {
                    oL = bandsL[b] * (1.0f - aN); oR = bandsR[b] * (1.0f - aN);
                }
                else
                {
                    float gDb = 0.0f;
                    if (bp.enabled)
                        gDb = (bp.attackGain * kMaxAttackDb + p.punch * kMaxPunchDb) * aN
                            +  bp.sustainGain * kMaxSustainDb * sN;
                    gDb = std::clamp (gDb, -40.0f, 30.0f);
                    const float g = std::exp (gDb * kLn10over20);
                    smoothG[(size_t) b] += gSmoothCoef * (g - smoothG[(size_t) b]);
                    oL = bandsL[b] * smoothG[(size_t) b];
                    oR = bandsR[b] * smoothG[(size_t) b];
                }

                wetL += oL;
                wetR += oR;

                const float bm = 0.5f * (oL + oR);
                vizBandBuf[(size_t) b][(size_t) w] = bm;
                maxBand[b] = std::max (maxBand[b], std::fabs (bm));
            }

            vizWrite.store ((w + 1) % kVizSize);

            float makeup = 1.0f;
            if (p.autoGain && solo == SoloOff)
                makeup = agc.process (std::max (std::fabs (il), std::fabs (ir)),
                                      std::max (std::fabs (wetL), std::fabs (wetR)));

            float lo = ((1.0f - effMix) * il + effMix * wetL * makeup) * p.output;
            float ro = ((1.0f - effMix) * ir + effMix * wetR * makeup) * p.output;
            limiter.process (lo, ro);
            outL[i] = lo;
            outR[i] = ro;
        }

        for (int b = 0; b < kNumBands; ++b)
            bandLevel[(size_t) b].store (maxBand[b]);
        transientLevel.store (maxTransient);
    }

} // namespace impact
