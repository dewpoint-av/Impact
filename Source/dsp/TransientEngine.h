#pragma once

/*
  Impact - multiband transient shaper (JUCE-free, self-contained).

  Splits the signal into three bands, and in each band separates the signal
  into a slow "sustain" envelope and a fast "transient" component, then mixes
  them back with per-band Attack / Sustain gains plus a global fast "Punch"
  transient. The classic SPL Transient Designer / NI Transient Master model.
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
    static constexpr int kNumBands  = 3;

    struct Params
    {
        float lowAttack  = 0.0f;   // -1..1
        float lowSustain = 0.0f;   // -1..1
        float midAttack  = 0.0f;
        float midSustain = 0.0f;
        float highAttack = 0.0f;
        float highSustain = 0.0f;
        float lowLength  = 62.5f;  // ms  transient-detection length
        float midLength  = 62.5f;
        float highLength = 62.5f;
        float punch      = 0.0f;   // 0..1
        int   punchMode  = 0;      // 0=Attack 1=Snap 2=Thump 3=Crisp
        float crossoverLow  = 200.0f;   // Hz  low/mid split
        float crossoverHigh = 3000.0f;  // Hz  mid/high split
        float mix        = 1.0f;   // 0..1
        float output     = 1.0f;   // 0..2

        bool operator== (const Params& o) const
        {
            return lowAttack   == o.lowAttack
                && lowSustain  == o.lowSustain
                && midAttack   == o.midAttack
                && midSustain  == o.midSustain
                && highAttack  == o.highAttack
                && highSustain == o.highSustain
                && lowLength   == o.lowLength
                && midLength   == o.midLength
                && highLength  == o.highLength
                && punch       == o.punch
                && punchMode   == o.punchMode
                && crossoverLow  == o.crossoverLow
                && crossoverHigh == o.crossoverHigh
                && mix         == o.mix
                && output      == o.output;
        }
    };

    // One-pole envelope follower.
    class EnvelopeFollower
    {
    public:
        void prepare (double fs, float attackSec, float releaseSec);
        void setTimes (double fs, float attackSec, float releaseSec);
        void reset();
        float process (float x);

    private:
        float attackCoeff = 0.0f;
        float releaseCoeff = 0.0f;
        float env = 0.0f;
    };

    // RBJ biquad (Butterworth Q) — used for crossovers and punch shaping.
    class Biquad
    {
    public:
        void setLowpass (double fs, float fc);
        void setHighpass (double fs, float fc);
        void reset();
        float process (float x);

    private:
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
    };

    // Per-band transient shaper.
    class BandShaper
    {
    public:
        void prepare (double fs);
        void reset();
        void setParams (float attack, float sustain, float punch, int punchMode, float lengthMs);
        float process (float x);

    private:
        EnvelopeFollower slow;   // sustain body
        EnvelopeFollower fast;   // punch transient
        Biquad transientFilt;    // shapes the punch transient (Thump/Crisp)
        double fs = 48000.0;
        float attack = 0.0f;
        float sustain = 0.0f;
        float punch = 0.0f;
        int   punchMode = 0;
    };

    // A compact lookahead peak limiter (2 ms) with a hard-clip safety net.
    class StereoLimiter
    {
    public:
        void prepare (double sr)
        {
            lookahead = static_cast<int> (sr * 0.002);
            if (lookahead < 1) lookahead = 1;
            delayL.assign (static_cast<size_t> (lookahead + 1), 0.0f);
            delayR.assign (static_cast<size_t> (lookahead + 1), 0.0f);
            threshold = 0.89f;
            releaseCoeff = static_cast<float> (1.0 - std::exp (-1.0 / (0.05 * sr)));
            reset();
        }

        void reset()
        {
            std::fill (delayL.begin(), delayL.end(), 0.0f);
            std::fill (delayR.begin(), delayR.end(), 0.0f);
            writePos = 0;
            gain = 1.0f;
            peak = 0.0f;
        }

        void process (float& l, float& r)
        {
            delayL[static_cast<size_t> (writePos)] = l;
            delayR[static_cast<size_t> (writePos)] = r;

            const float a = std::max (std::fabs (l), std::fabs (r));
            if (a > peak)      peak = a;
            else               peak += releaseCoeff * (a - peak);

            const float target = (peak > threshold) ? threshold / peak : 1.0f;
            if (target < gain) gain = target;
            else               gain += releaseCoeff * (target - gain);

            const int rd = (writePos + 1) % (lookahead + 1);
            l = std::clamp (delayL[static_cast<size_t> (rd)] * gain, -1.0f, 1.0f);
            r = std::clamp (delayR[static_cast<size_t> (rd)] * gain, -1.0f, 1.0f);
            writePos = rd;
        }

    private:
        int lookahead = 96;
        std::vector<float> delayL;
        std::vector<float> delayR;
        int writePos = 0;
        float gain = 1.0f;
        float peak = 0.0f;
        float threshold = 0.89f;
        float releaseCoeff = 0.0f;
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
        const std::vector<float>& vizBand (int b) const { return vizBandBuf[static_cast<size_t> (b)]; }
        int  vizWritePos() const                       { return vizWrite.load(); }

        float getTransientLevel() const { return transientLevel.load(); }
        float getBandLevel (int b) const
        {
            return (b >= 0 && b < kNumBands) ? bandLevel[static_cast<size_t> (b)].load() : 0.0f;
        }

    private:
        void updateCoeffs (double fs);

        double fs = 48000.0;
        Params p;

        Biquad lp[2];
        Biquad hp[2];
        BandShaper shaper[2][kNumBands];
        StereoLimiter limiter;

        EnvelopeFollower vizFollower;

        std::vector<float> vizInBuf;
        std::vector<float> vizTransBuf;
        std::array<std::vector<float>, kNumBands> vizBandBuf;
        std::atomic<int> vizWrite { 0 };

        std::atomic<float> transientLevel { 0.0f };
        std::array<std::atomic<float>, kNumBands> bandLevel {};
    };

} // namespace impact
