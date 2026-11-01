#pragma once

/*
  Impact — multiband transient shaper (JUCE-free, self-contained).

  Overhaul (v1.3): a 4-band Linkwitz-Riley (LR4, 24 dB/oct, phase-coherent with
  all-pass compensation) split feeds a per-band SPL/Bitwig-style differential
  envelope detector. Each band exposes the Bitwig Transient Control four-knob
  model — Attack Time, Attack Gain, Sustain Time (length), Sustain Gain (volume)
  — plus a per-band enable. Detection is level-independent (threshold-free): a
  fast peak follower minus a slow follower yields an "attack-ness" 0..1, and a
  held slow follower minus the fast one yields "sustain-ness" 0..1. Per-band gain
  (dB) = attackGain·MAXA·attackness + sustainGain·MAXS·sustainness (+ global
  Punch), converted to linear, lightly smoothed, applied to the band. A global
  Solo monitors transient-only or body-only. A safety limiter + optional auto
  makeup gain finish the chain.
*/

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace impact
{
    static constexpr int kVizSize   = 4096;
    static constexpr int kNumBands  = 4;

    // Shaping ranges (dB) at full knob + full detection.
    static constexpr float kMaxAttackDb  = 18.0f;
    static constexpr float kMaxSustainDb = 24.0f;
    static constexpr float kMaxPunchDb   = 12.0f;

    // Sustain Time parameter range + skew (must match the susTime parameter layout).
    static constexpr float kSustainTimeMinMs = 10.0f;
    static constexpr float kSustainTimeMaxMs = 500.0f;
    static constexpr float kSustainTimeSkew  = 0.5f;

    // Sustain Time means body LENGTH, whichever way the gain goes: short = less body,
    // long = more body. A longer detector release means a stronger sustain signal, which
    // suits a boost, but a cut needs the reverse or a long setting removes the most body.
    // So cuts use the release mirrored across the knob's travel (min <-> max).
    inline float sustainReleaseMs (float sustainTimeMs, float sustainGain)
    {
        if (sustainGain >= 0.0f)
            return sustainTimeMs;
        const float range = kSustainTimeMaxMs - kSustainTimeMinMs;
        const float prop  = std::clamp ((sustainTimeMs - kSustainTimeMinMs) / range, 0.0f, 1.0f);
        const float knob  = 1.0f - std::pow (prop, kSustainTimeSkew);
        return kSustainTimeMinMs + range * std::pow (knob, 1.0f / kSustainTimeSkew);
    }

    enum SoloMode { SoloOff = 0, SoloTransient = 1, SoloBody = 2 };

    struct BandParams
    {
        float attackGain  = 0.0f;    // -1..1
        float attackTime  = 5.0f;    // ms   (attack-detection window)
        float sustainGain = 0.0f;    // -1..1
        float sustainTime = 150.0f;  // ms   (sustain region length)
        bool  enabled     = true;

        bool operator== (const BandParams& o) const
        {
            return attackGain == o.attackGain && attackTime == o.attackTime
                && sustainGain == o.sustainGain && sustainTime == o.sustainTime
                && enabled == o.enabled;
        }
    };

    struct Params
    {
        std::array<BandParams, kNumBands> band {};
        float xover1 = 120.0f;    // Hz  low / low-mid
        float xover2 = 400.0f;    // Hz  low-mid / mid   (ST4b-style punch band)
        float xover3 = 3000.0f;   // Hz  mid / high
        float punch  = 0.0f;      // 0..1  global attack emphasis
        int   solo   = SoloOff;   // 0 off, 1 transient, 2 body
        float mix    = 1.0f;      // 0..1
        float output = 1.0f;      // 0..2
        bool  autoGain = true;

        bool operator== (const Params& o) const
        {
            for (int b = 0; b < kNumBands; ++b)
                if (! (band[(size_t) b] == o.band[(size_t) b])) return false;
            return xover1 == o.xover1 && xover2 == o.xover2 && xover3 == o.xover3
                && punch == o.punch && solo == o.solo && mix == o.mix
                && output == o.output && autoGain == o.autoGain;
        }
    };

    // RBJ biquad (lowpass / highpass / allpass).
    class Biquad
    {
    public:
        void setLowpass  (double fs, float fc);
        void setHighpass (double fs, float fc);
        void setAllpass  (double fs, float fc);
        void reset() { z1 = z2 = 0.0f; }
        float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    private:
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;
    };

    // 4th-order Linkwitz-Riley = two cascaded Butterworth 2nd-order sections.
    struct LR4
    {
        Biquad a, b;
        void setLowpass  (double fs, float fc) { a.setLowpass (fs, fc);  b.setLowpass (fs, fc); }
        void setHighpass (double fs, float fc) { a.setHighpass (fs, fc); b.setHighpass (fs, fc); }
        void reset() { a.reset(); b.reset(); }
        float process (float x) { return b.process (a.process (x)); }
    };

    // Phase-coherent 4-band LR4 split (one per channel). Low/low-mid bands are
    // passed through matching all-passes so all four bands sum flat.
    struct ChannelSplit
    {
        LR4 lp1, hp1, lp2, hp2, lp3, hp3;
        Biquad ap2_b0, ap3_b0, ap3_b1;

        void setCoeffs (double fs, float f1, float f2, float f3)
        {
            lp1.setLowpass (fs, f1); hp1.setHighpass (fs, f1);
            lp2.setLowpass (fs, f2); hp2.setHighpass (fs, f2);
            lp3.setLowpass (fs, f3); hp3.setHighpass (fs, f3);
            ap2_b0.setAllpass (fs, f2);
            ap3_b0.setAllpass (fs, f3);
            ap3_b1.setAllpass (fs, f3);
        }
        void reset()
        {
            lp1.reset(); hp1.reset(); lp2.reset(); hp2.reset(); lp3.reset(); hp3.reset();
            ap2_b0.reset(); ap3_b0.reset(); ap3_b1.reset();
        }
        void process (float x, float* bands)
        {
            const float l1 = lp1.process (x);
            const float h1 = hp1.process (x);
            bands[0] = ap3_b0.process (ap2_b0.process (l1));
            const float l2 = lp2.process (h1);
            const float h2 = hp2.process (h1);
            bands[1] = ap3_b1.process (l2);
            bands[2] = lp3.process (h2);
            bands[3] = hp3.process (h2);
        }
    };

    // Per-band differential detector (linked stereo). Produces attack-ness and
    // sustain-ness, both 0..1 and level-independent.
    struct BandDetector
    {
        float envFast = 0, envSlowA = 0, envSlowS = 0;
        float fastAtk = 0, fastRel = 0, slowAatk = 0, slowArel = 0, slowSatk = 0, slowSrel = 0;

        static float coef (float ms, double fs)
        {
            return 1.0f - std::exp (-1.0f / (std::max (0.01f, ms) * 0.001f * (float) fs));
        }
        void setTimes (double fs, float attackMs, float sustainMs)
        {
            fastAtk  = coef (0.2f, fs);   fastRel  = coef (20.0f, fs);
            slowAatk = coef (attackMs, fs); slowArel = coef (50.0f, fs);
            slowSatk = coef (10.0f, fs);  slowSrel = coef (sustainMs, fs);
        }
        void reset() { envFast = envSlowA = envSlowS = 0.0f; }

        void process (float mag, float& aN, float& sN)
        {
            envFast  += (mag > envFast  ? fastAtk  : fastRel)  * (mag - envFast);
            envSlowA += (mag > envSlowA ? slowAatk : slowArel) * (mag - envSlowA);
            envSlowS += (mag > envSlowS ? slowSatk : slowSrel) * (mag - envSlowS);
            aN = std::clamp ((envFast  - envSlowA) / (envFast  + 1.0e-6f), 0.0f, 1.0f);
            sN = std::clamp ((envSlowS - envFast)  / (envSlowS + 1.0e-6f), 0.0f, 1.0f);
        }
    };

    // Magnitude one-pole envelope (used only for the full-band visualizer).
    class EnvelopeFollower
    {
    public:
        void prepare (double fs, float atkSec, float relSec) { setTimes (fs, atkSec, relSec); reset(); }
        void setTimes (double fs, float atkSec, float relSec)
        {
            attackCoeff  = (float) (1.0 - std::exp (-1.0 / (atkSec * fs)));
            releaseCoeff = (float) (1.0 - std::exp (-1.0 / (relSec * fs)));
        }
        void reset() { env = 0.0f; }
        float process (float x)
        {
            const float a = std::fabs (x);
            env += (a > env ? attackCoeff : releaseCoeff) * (a - env);
            return env;
        }
    private:
        float attackCoeff = 0, releaseCoeff = 0, env = 0;
    };

    // Compact 2 ms look-ahead safety limiter with a hard-clip net.
    class StereoLimiter
    {
    public:
        void prepare (double sr)
        {
            lookahead = std::max (1, (int) (sr * 0.002));
            delayL.assign ((size_t) (lookahead + 1), 0.0f);
            delayR.assign ((size_t) (lookahead + 1), 0.0f);
            releaseCoeff = (float) (1.0 - std::exp (-1.0 / (0.05 * sr)));
            reset();
        }
        void reset()
        {
            std::fill (delayL.begin(), delayL.end(), 0.0f);
            std::fill (delayR.begin(), delayR.end(), 0.0f);
            writePos = 0; gain = 1.0f; peak = 0.0f;
        }
        void process (float& l, float& r)
        {
            delayL[(size_t) writePos] = l;
            delayR[(size_t) writePos] = r;
            const float a = std::max (std::fabs (l), std::fabs (r));
            if (a > peak) peak = a; else peak += releaseCoeff * (a - peak);
            const float target = (peak > threshold) ? threshold / peak : 1.0f;
            if (target < gain) gain = target; else gain += releaseCoeff * (target - gain);
            const int rd = (writePos + 1) % (lookahead + 1);
            l = std::clamp (delayL[(size_t) rd] * gain, -1.0f, 1.0f);
            r = std::clamp (delayR[(size_t) rd] * gain, -1.0f, 1.0f);
            writePos = rd;
        }
    private:
        int lookahead = 96;
        std::vector<float> delayL, delayR;
        int writePos = 0;
        float gain = 1.0f, peak = 0.0f, threshold = 0.89f, releaseCoeff = 0.0f;
    };

    // Slow auto makeup: track dry vs wet magnitude, apply ±12 dB makeup.
    class GainCompensator
    {
    public:
        void prepare (double fs) { coeff = (float) (1.0 - std::exp (-1.0 / (0.15 * fs))); reset(); }
        void reset() { inLvl = outLvl = 0.0f; gain = 1.0f; }
        float process (float dryMag, float wetMag)
        {
            inLvl  += coeff * (dryMag - inLvl);
            outLvl += coeff * (wetMag - outLvl);
            float target = 1.0f;
            if (outLvl > 1.0e-4f && inLvl > 1.0e-4f) target = inLvl / outLvl;
            target = std::clamp (target, 0.25f, 4.0f);
            gain += coeff * (target - gain);
            return gain;
        }
    private:
        float coeff = 0, inLvl = 0, outLvl = 0, gain = 1.0f;
    };

    class TransientEngine
    {
    public:
        void prepare (double fs, int maxBlockSize);
        void reset();
        void setParams (const Params& p);
        void process (const float* inL, const float* inR,
                      float* outL, float* outR, int numSamples);

        const std::vector<float>& vizIn() const        { return vizInBuf; }
        const std::vector<float>& vizTransient() const { return vizTransBuf; }
        const std::vector<float>& vizBand (int b) const { return vizBandBuf[(size_t) b]; }
        int  vizWritePos() const                        { return vizWrite.load(); }

        float getTransientLevel() const { return transientLevel.load(); }
        float getBandLevel (int b) const
        {
            return (b >= 0 && b < kNumBands) ? bandLevel[(size_t) b].load() : 0.0f;
        }

    private:
        void updateCoeffs (double fs);

        double fs = 48000.0;
        Params p;

        ChannelSplit split[2];
        BandDetector detector[kNumBands];     // linked (shared across channels)
        float smoothG[kNumBands] = { 1, 1, 1, 1 };
        float gSmoothCoef = 0.2f;

        StereoLimiter limiter;
        GainCompensator agc;
        EnvelopeFollower vizFollower;

        std::vector<float> vizInBuf;
        std::vector<float> vizTransBuf;
        std::array<std::vector<float>, kNumBands> vizBandBuf;
        std::atomic<int> vizWrite { 0 };

        std::atomic<float> transientLevel { 0.0f };
        std::array<std::atomic<float>, kNumBands> bandLevel {};
    };

} // namespace impact
