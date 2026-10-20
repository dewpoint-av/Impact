#pragma once

/*
  Impact - multiband transient shaper (JUCE-free, self-contained).

  Splits the signal into three bands, and in each band separates the signal
  into a slow "sustain" envelope and a fast "transient" component, then mixes
  them back with per-band Attack / Sustain gains plus a global fast "Punch"
  transient. The classic SPL Transient Designer / NI Transient Master model.
*/

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
        float punch      = 0.0f;   // 0..1
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
                && punch       == o.punch
                && mix         == o.mix
                && output      == o.output;
        }
    };

    // One-pole envelope follower.
    class EnvelopeFollower
    {
    public:
        void prepare (double fs, float attackSec, float releaseSec);
        void reset();
        float process (float x);

    private:
        float attackCoeff = 0.0f;
        float releaseCoeff = 0.0f;
        float env = 0.0f;
    };

    // Per-band transient shaper.
    class BandShaper
    {
    public:
        void prepare (double fs);
        void reset();
        void setParams (float attack, float sustain, float punch);
        float process (float x);

    private:
        EnvelopeFollower slow;   // sustain body
        EnvelopeFollower fast;   // punch transient
        float attack = 0.0f;
        float sustain = 0.0f;
        float punch = 0.0f;
    };

    // RBJ biquad crossover (Butterworth Q).
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

        EnvelopeFollower vizFollower;

        std::vector<float> vizInBuf;
        std::vector<float> vizTransBuf;
        std::atomic<int> vizWrite { 0 };

        std::atomic<float> transientLevel { 0.0f };
        std::array<std::atomic<float>, kNumBands> bandLevel {};
    };

} // namespace impact
