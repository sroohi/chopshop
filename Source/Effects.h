#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <vector>

namespace fx
{
constexpr float twoPi = juce::MathConstants<float>::twoPi;

inline float hermite (float xm1, float x0, float x1, float x2, float t) noexcept
{
    const float c = (x1 - xm1) * 0.5f;
    const float v = x0 - x1;
    const float w = c + v;
    const float a = w + v + (x2 - x0) * 0.5f;
    const float b = w + a;
    return (((a * t) - b) * t + c) * t + x0;
}

/** Stereo circular buffer. A delay of 0 reads the most recently pushed sample. */
struct Ring
{
    std::vector<float> data[2];
    int size = 0, mask = 0, w = 0;

    void prepare (int minSamples)
    {
        size = juce::nextPowerOfTwo (juce::jmax (16, minSamples));
        mask = size - 1;
        for (auto& d : data)
            d.assign ((size_t) size, 0.0f);
        w = 0;
    }

    void clear()
    {
        for (auto& d : data)
            std::fill (d.begin(), d.end(), 0.0f);
    }

    void push (float l, float r) noexcept
    {
        data[0][(size_t) w] = l;
        data[1][(size_t) w] = r;
        w = (w + 1) & mask;
    }

    float at (int ch, int delay) const noexcept { return data[ch][(size_t) ((w - 1 - delay) & mask)]; }

    float read (int ch, double delay) const noexcept
    {
        delay = juce::jlimit (0.0, (double) (size - 4), delay);
        const double rp = (double) (w - 1) - delay;
        const int i = (int) std::floor (rp);
        const float f = (float) (rp - i);
        const auto& d = data[ch];
        const float x0 = d[(size_t) (i & mask)];
        const float x1 = d[(size_t) ((i + 1) & mask)];
        if (delay < 2.0) // cubic would touch samples that haven't been written yet
            return x0 + f * (x1 - x0);
        return hermite (d[(size_t) ((i - 1) & mask)], x0, x1, d[(size_t) ((i + 2) & mask)], f);
    }
};

struct OnePoleLP
{
    float a = 1.0f, z = 0.0f;
    void setCutoff (float hz, double sr) { a = 1.0f - std::exp (-twoPi * juce::jmin (hz, (float) sr * 0.49f) / (float) sr); }
    float process (float x) noexcept { z += a * (x - z); return z; }
    void reset() { z = 0.0f; }
};

struct OnePoleHP
{
    OnePoleLP lp;
    void setCutoff (float hz, double sr) { lp.setCutoff (hz, sr); }
    float process (float x) noexcept { return x - lp.process (x); }
    void reset() { lp.reset(); }
};

inline float softClip (float x, float drive) noexcept { return std::tanh (x * drive) / drive; }

//==============================================================================
/** Beat-repeat: captures one tempo-synced slice of audio when engaged and loops it. */
class Stutter
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        capacity = (int) (sr * 4.5);
        for (auto& c : cap)
            c.assign ((size_t) capacity, 0.0f);
        wet.reset (sr, 0.006);
        wet.setCurrentAndTargetValue (0.0f);
        fade = (float) juce::jmax (16.0, sr * 0.002);
        gateCoef = 1.0f - std::exp (-1.0f / (float) (0.002 * sr));
        engaged = capturing = false;
    }

    void process (float* L, float* R, int n, bool on, double lenSamples, float gate, float mix)
    {
        const int wantLen = juce::jlimit (64, capacity, (int) std::round (lenSamples));

        if (on && ! engaged)
        {
            engaged = capturing = true;
            capLen = pos = 0;
            loopLen = wantLen;
            gateEnv = 1.0f;
            wet.setTargetValue (1.0f);
        }
        else if (! on && engaged)
        {
            engaged = false;
            wet.setTargetValue (0.0f);
        }

        for (int i = 0; i < n; ++i)
        {
            const float e = wet.getNextValue();
            if (e <= 0.0f && ! engaged)
                continue;

            float wl, wr, window, p;

            if (capturing)
            {
                cap[0][(size_t) capLen] = L[i];
                cap[1][(size_t) capLen] = R[i];
                wl = L[i];
                wr = R[i];
                p = (float) capLen;
                ++capLen;
                window = juce::jmin (1.0f, (float) (loopLen - capLen) / fade);
                if (capLen >= loopLen)
                {
                    capturing = false;
                    pos = 0;
                }
            }
            else
            {
                if (pos == 0)
                    loopLen = juce::jmin (wantLen, capLen); // rate changes land on the next repeat

                wl = cap[0][(size_t) pos];
                wr = cap[1][(size_t) pos];
                p = (float) pos;
                window = juce::jmin (1.0f, p / fade, (float) (loopLen - pos) / fade);
                if (++pos >= loopLen)
                    pos = 0;
            }

            const float gateTarget = p < gate * (float) loopLen ? 1.0f : 0.0f;
            gateEnv += gateCoef * (gateTarget - gateEnv);

            const float amt = e * mix;
            const float g = juce::jmax (0.0f, window) * gateEnv;
            L[i] = L[i] * (1.0f - amt) + wl * g * amt;
            R[i] = R[i] * (1.0f - amt) + wr * g * amt;
        }
    }

private:
    double sr = 44100.0;
    std::vector<float> cap[2];
    int capacity = 0, capLen = 0, loopLen = 1, pos = 0;
    bool engaged = false, capturing = false;
    float fade = 64.0f, gateEnv = 1.0f, gateCoef = 0.01f;
    juce::SmoothedValue<float> wet;
};

//==============================================================================
/** Half-time: inside each window, plays the first half of it back at half speed (an octave down). */
class HalfTime
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        ring.prepare ((int) (sr * 10.0));
        wet.reset (sr, 0.008);
        wet.setCurrentAndTargetValue (0.0f);
        xfLen = juce::jmax (32, (int) (sr * 0.012));
        engaged = false;
    }

    void process (float* L, float* R, int n, bool on, double windowSamples, float mix)
    {
        const double W = juce::jlimit (256.0, (double) ring.size * 1.6, windowSamples);

        if (on && ! engaged)
        {
            engaged = true;
            p = 0.0;
            xf = 0;
            wet.setTargetValue (1.0f);
        }
        else if (! on && engaged)
        {
            engaged = false;
            wet.setTargetValue (0.0f);
        }

        for (int i = 0; i < n; ++i)
        {
            ring.push (L[i], R[i]);
            const float e = wet.getNextValue();
            if (e <= 0.0f && ! engaged)
                continue;

            float wl = ring.read (0, p * 0.5);
            float wr = ring.read (1, p * 0.5);

            if (xf > 0)
            {
                const float a = (float) xf / (float) xfLen;
                wl = wl * (1.0f - a) + ring.read (0, pOld * 0.5) * a;
                wr = wr * (1.0f - a) + ring.read (1, pOld * 0.5) * a;
                pOld += 1.0;
                --xf;
            }

            p += 1.0;
            if (p >= W)
            {
                pOld = p;
                p = 0.0;
                xf = xfLen;
            }

            const float amt = e * mix;
            L[i] = L[i] * (1.0f - amt) + wl * amt;
            R[i] = R[i] * (1.0f - amt) + wr * amt;
        }
    }

private:
    double sr = 44100.0, p = 0.0, pOld = 0.0;
    Ring ring;
    int xf = 0, xfLen = 256;
    bool engaged = false;
    juce::SmoothedValue<float> wet;
};

//==============================================================================
/** Turntable / tape stop: playback speed winds down to zero, with an optional spin-up on release. */
class TapeStop
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        ring.prepare ((int) (sr * 10.0));
        state = Idle;
        d = 0.0;
        x = 1.0f;
        retLen = juce::jmax (32, (int) (sr * 0.03));
    }

    bool isEngaged() const { return state != Idle; }

    void process (float* L, float* R, int n, bool on, float stopSec, float curve, float spinSec)
    {
        const float k = juce::jmap (curve, 0.5f, 3.0f);

        if (on && (state == Idle || state == SpinUp || state == Return))
        {
            if (state == Idle)
            {
                d = 0.0;
                x = 1.0f;
            }
            else if (state == Return)
                x = 1.0f;
            state = Stopping;
        }
        else if (! on && state == Stopping)
        {
            if (spinSec > 0.01f)
            {
                if (std::pow (x, k) < 0.01f)
                    d = 0.0; // fully stopped: restart the "tape" at the live signal
                state = SpinUp;
            }
            else
            {
                state = Return;
                ret = 0;
            }
        }

        const float stopStep = 1.0f / (float) (juce::jmax (0.01f, stopSec) * sr);
        const float spinStep = 1.0f / (float) (juce::jmax (0.01f, spinSec) * sr);
        const double maxD = (double) ring.size - 8.0;

        for (int i = 0; i < n; ++i)
        {
            ring.push (L[i], R[i]);
            if (state == Idle)
                continue;

            const float s = std::pow (juce::jmax (0.0f, x), k);
            const float amp = juce::jmin (1.0f, s * 8.0f);
            const float tl = ring.read (0, d) * amp;
            const float tr = ring.read (1, d) * amp;

            if (state == Stopping)
            {
                x = juce::jmax (0.0f, x - stopStep);
                d = juce::jmin (maxD, d + (1.0 - s));
                L[i] = tl;
                R[i] = tr;
            }
            else if (state == SpinUp)
            {
                x += spinStep;
                d = juce::jmin (maxD, d + (1.0 - s));
                if (x >= 1.0f)
                {
                    x = 1.0f;
                    state = Return;
                    ret = 0;
                }
                L[i] = tl;
                R[i] = tr;
            }
            else // Return: crossfade from the tape read-head back to the live signal
            {
                const float r = (float) ret / (float) retLen;
                L[i] = tl * (1.0f - r) + L[i] * r;
                R[i] = tr * (1.0f - r) + R[i] * r;
                if (++ret >= retLen)
                    state = Idle;
            }
        }
    }

private:
    enum State { Idle, Stopping, SpinUp, Return };
    State state = Idle;
    double sr = 44100.0, d = 0.0;
    float x = 1.0f;
    int ret = 0, retLen = 1024;
    Ring ring;
};

//==============================================================================
/** Tempo-synced tape echo: wow/flutter, a darkening feedback loop and tape saturation. */
class TapeDelay
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        ring.prepare ((int) (sr * 4.5));
        inGain.reset (sr, 0.02);
        inGain.setCurrentAndTargetValue (0.0f);
        smoothedDelay = -1.0;
        slew = 1.0 - std::exp (-1.0 / (0.12 * sr));
        for (int c = 0; c < 2; ++c)
        {
            lp[c].reset();
            hp[c].reset();
            hp[c].setCutoff (70.0f, sr);
        }
        wowPh = 0.0f;
        flutPh = 0.3f;
    }

    void process (float* L, float* R, int n, bool on, double delaySamples, float fb, float wow, float tone,
                  float age, float mix)
    {
        inGain.setTargetValue (on ? 1.0f : 0.0f);
        const double target = juce::jlimit (16.0, (double) ring.size - 2000.0, delaySamples);
        if (smoothedDelay < 0.0)
            smoothedDelay = target;

        const float cutoff = 1200.0f * std::pow (12.0f, tone); // 1.2 kHz .. 14 kHz
        for (auto& f : lp)
            f.setCutoff (cutoff, sr);

        const float drive = 1.0f + age * 5.0f;
        const float wowDepth = wow * 0.0035f * (float) sr;
        const float flutDepth = wow * 0.00035f * (float) sr;
        const float wowInc = twoPi * 0.55f / (float) sr;
        const float flutInc = twoPi * 6.3f / (float) sr;

        for (int i = 0; i < n; ++i)
        {
            smoothedDelay += (target - smoothedDelay) * slew;
            wowPh += wowInc;
            if (wowPh > twoPi) wowPh -= twoPi;
            flutPh += flutInc;
            if (flutPh > twoPi) flutPh -= twoPi;

            const float gIn = inGain.getNextValue();
            float y[2];
            for (int c = 0; c < 2; ++c)
            {
                const float off = c == 0 ? 0.0f : 1.7f;
                const double mod = wowDepth * (1.0f + std::sin (wowPh + off)) + flutDepth * (1.0f + std::sin (flutPh + off * 2.1f));
                float v = ring.read (c, smoothedDelay + mod);
                v = hp[c].process (lp[c].process (v));
                y[c] = softClip (v, drive);
            }

            ring.push (L[i] * gIn + fb * y[0], R[i] * gIn + fb * y[1]);

            L[i] += y[0] * mix;
            R[i] += y[1] * mix;
        }
    }

private:
    double sr = 44100.0, smoothedDelay = -1.0, slew = 0.001;
    Ring ring;
    OnePoleLP lp[2];
    OnePoleHP hp[2];
    float wowPh = 0.0f, flutPh = 0.0f;
    juce::SmoothedValue<float> inGain;
};

//==============================================================================
/** 8-line feedback-delay-network reverb with modulated lines, damping and tape saturation. */
class TapeReverb
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        pre.prepare ((int) (sr * 0.25) + 8);
        const int lineLen = juce::nextPowerOfTwo ((int) (sr * 0.3));
        for (auto& l : lines)
            l.assign ((size_t) lineLen, 0.0f);
        lineMask = lineLen - 1;
        wpos = 0;

        static constexpr float apMs[4] { 4.77f, 3.59f, 12.73f, 9.31f };
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 4; ++k)
                ap[c][k].prepare ((int) (apMs[k] * (c == 0 ? 1.0f : 1.083f) * 0.001f * (float) sr));

        for (int j = 0; j < N; ++j)
        {
            damp[j].reset();
            lfoPh[j] = (float) j * 0.785f;
        }
        for (auto& h : outHp)
        {
            h.reset();
            h.setCutoff (110.0f, sr);
        }
        inGain.reset (sr, 0.02);
        inGain.setCurrentAndTargetValue (0.0f);
        sizeSm.reset (sr, 0.25);
        sizeSm.setCurrentAndTargetValue (0.5f);
        idle = 0;
    }

    void process (float* L, float* R, int n, bool on, float size, float decay, float preMs, float tone,
                  float wow, float mix)
    {
        inGain.setTargetValue (on ? 1.0f : 0.0f);
        sizeSm.setTargetValue (size);

        // Once switched off and the tail has died away, skip the network entirely.
        if (! on && ! inGain.isSmoothing())
        {
            idle += n;
            if ((double) idle > sr * ((double) decay + 0.6))
                return;
        }
        else
            idle = 0;

        const float scale = 0.45f + sizeSm.getCurrentValue() * 1.3f;
        float g[N];
        for (int j = 0; j < N; ++j)
            g[j] = std::pow (10.0f, -3.0f * baseMs[j] * scale * 0.001f / juce::jmax (0.1f, decay));

        const float cutoff = 1500.0f * std::pow (12.0f, tone); // 1.5 kHz .. 18 kHz
        for (auto& dmp : damp)
            dmp.setCutoff (cutoff, sr);

        const int preSamples = juce::jlimit (0, pre.size - 2, (int) (preMs * 0.001f * (float) sr));
        const float modDepth = (0.00025f + wow * 0.0025f) * (float) sr;

        for (int i = 0; i < n; ++i)
        {
            const float gIn = inGain.getNextValue();
            const float sc = 0.45f + sizeSm.getNextValue() * 1.3f;
            pre.push (L[i] * gIn, R[i] * gIn);
            float xl = pre.at (0, preSamples);
            float xr = pre.at (1, preSamples);
            for (int k = 0; k < 4; ++k)
            {
                xl = ap[0][k].process (xl);
                xr = ap[1][k].process (xr);
            }

            float o[N];
            float sum = 0.0f;
            for (int j = 0; j < N; ++j)
            {
                lfoPh[j] += twoPi * lfoHz[j] / (float) sr;
                if (lfoPh[j] > twoPi) lfoPh[j] -= twoPi;
                const float len = baseMs[j] * sc * 0.001f * (float) sr + modDepth * (1.0f + std::sin (lfoPh[j]));
                o[j] = damp[j].process (readLine (j, len)) * g[j];
                sum += o[j];
            }
            sum *= 2.0f / (float) N; // Householder feedback matrix

            for (int j = 0; j < N; ++j)
            {
                const float in = ((j & 1) == 0 ? xl : xr) * ((j & 2) == 0 ? 0.5f : -0.5f);
                lines[(size_t) j][(size_t) wpos] = o[j] - sum + in;
            }
            wpos = (wpos + 1) & lineMask;

            float wl = 0.42f * (o[0] - o[2] + o[4] + o[6]);
            float wr = 0.42f * (o[1] + o[3] - o[5] + o[7]);
            wl = softClip (outHp[0].process (wl), 1.4f);
            wr = softClip (outHp[1].process (wr), 1.4f);

            L[i] += wl * mix;
            R[i] += wr * mix;
        }
    }

private:
    static constexpr int N = 8;
    static constexpr float baseMs[N] { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.7f, 73.1f };
    static constexpr float lfoHz[N] { 0.31f, 0.43f, 0.57f, 0.67f, 0.79f, 0.93f, 1.07f, 1.19f };

    struct Allpass
    {
        std::vector<float> b;
        int idx = 0;
        float g = 0.62f;
        void prepare (int len) { b.assign ((size_t) juce::jmax (1, len), 0.0f); idx = 0; }
        float process (float x) noexcept
        {
            const float d = b[(size_t) idx];
            const float y = -g * x + d;
            b[(size_t) idx] = x + g * y;
            if (++idx >= (int) b.size()) idx = 0;
            return y;
        }
    };

    float readLine (int j, float delay) const noexcept
    {
        const float rp = (float) wpos - delay;
        const int i0 = (int) std::floor (rp);
        const float f = rp - (float) i0;
        const auto& l = lines[(size_t) j];
        const float a = l[(size_t) (i0 & lineMask)];
        const float b = l[(size_t) ((i0 + 1) & lineMask)];
        return a + f * (b - a);
    }

    double sr = 44100.0;
    Ring pre;
    std::array<std::vector<float>, N> lines;
    int lineMask = 0, wpos = 0, idle = 0;
    Allpass ap[2][4];
    OnePoleLP damp[N];
    OnePoleHP outHp[2];
    float lfoPh[N] {};
    juce::SmoothedValue<float> inGain, sizeSm;
};

//==============================================================================
/** Mid/side stereo widener with optional Haas delay and mono low end. */
class Widener
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        ring.prepare ((int) (sr * 0.05));
        sideHp.reset();
        sideHp.setCutoff (140.0f, sr);
        width.reset (sr, 0.03);
        width.setCurrentAndTargetValue (1.0f);
        haas.reset (sr, 0.05);
        haas.setCurrentAndTargetValue (0.0f);
        bass.reset (sr, 0.03);
        bass.setCurrentAndTargetValue (0.0f);
    }

    void process (float* L, float* R, int n, bool on, float w, float haasMs, bool monoLow)
    {
        width.setTargetValue (on ? w : 1.0f);
        haas.setTargetValue (on ? haasMs : 0.0f);
        bass.setTargetValue (on && monoLow ? 1.0f : 0.0f);

        if (! on && ! width.isSmoothing() && ! haas.isSmoothing() && ! bass.isSmoothing())
        {
            for (int i = 0; i < n; ++i)
                ring.push (L[i], R[i]);
            return;
        }

        for (int i = 0; i < n; ++i)
        {
            const float m = (L[i] + R[i]) * 0.5f;
            float s = (L[i] - R[i]) * 0.5f;
            const float b = bass.getNextValue();
            s = s * (1.0f - b) + sideHp.process (s) * b;
            s *= width.getNextValue();
            float l = m + s, r = m - s;
            ring.push (l, r);
            const float h = haas.getNextValue();
            if (h > 0.01f)
                r = ring.read (1, h * 0.001f * (float) sr);
            L[i] = l;
            R[i] = r;
        }
    }

private:
    double sr = 44100.0;
    Ring ring;
    OnePoleHP sideHp;
    juce::SmoothedValue<float> width, haas, bass;
};

//==============================================================================
/** Early-sampler character: sample-and-hold rate reduction plus bit-depth reduction. */
class Vintage
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        amount.reset (sr, 0.02);
        amount.setCurrentAndTargetValue (0.0f);
        phase = 0.0;
        hold[0] = hold[1] = 0.0f;
        for (auto& f : recon) f.reset();
    }

    void process (float* L, float* R, int n, bool on, float bits, float rate)
    {
        amount.setTargetValue (on ? 1.0f : 0.0f);
        if (! on && ! amount.isSmoothing())
            return;

        const double ratio = juce::jmin (1.0, (double) rate / sr);
        const float q = std::pow (2.0f, bits - 1.0f);
        for (auto& f : recon)
            f.setCutoff (juce::jmin (rate * 0.45f, 20000.0f), sr);

        for (int i = 0; i < n; ++i)
        {
            phase += ratio;
            if (phase >= 1.0)
            {
                phase -= 1.0;
                hold[0] = std::round (L[i] * q) / q;
                hold[1] = std::round (R[i] * q) / q;
            }
            const float m = amount.getNextValue();
            L[i] = L[i] * (1.0f - m) + recon[0].process (hold[0]) * m;
            R[i] = R[i] * (1.0f - m) + recon[1].process (hold[1]) * m;
        }
    }

private:
    double sr = 44100.0, phase = 0.0;
    float hold[2] {};
    OnePoleLP recon[2];
    juce::SmoothedValue<float> amount;
};

//==============================================================================
/** Saturation with three curves. Uses first-order antiderivative anti-aliasing (ADAA)
    instead of oversampling, so it adds no latency. */
class Drive
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        mixS.reset (sr, 0.02);
        mixS.setCurrentAndTargetValue (0.0f);
        gainS.reset (sr, 0.03);
        gainS.setCurrentAndTargetValue (1.0f);
        for (int c = 0; c < 2; ++c)
        {
            xPrev[c] = 0.0f;
            dc[c].reset();
            dc[c].setCutoff (8.0f, sr);
        }
    }

    void process (float* L, float* R, int n, bool on, float amount, int type, float mix)
    {
        mixS.setTargetValue (on ? mix : 0.0f);
        if (! on && ! mixS.isSmoothing())
        {
            xPrev[0] = L[n - 1];
            xPrev[1] = R[n - 1];
            return;
        }

        curve = juce::jlimit (0, 2, type);
        gainS.setTargetValue (juce::Decibels::decibelsToGain (amount * 30.0f));
        float* ch[2] { L, R };

        for (int i = 0; i < n; ++i)
        {
            const float g = gainS.getNextValue();
            const float comp = 0.25f / shape (0.25f * g); // a -12 dBFS input comes out at the same level
            const float m = mixS.getNextValue();
            for (int c = 0; c < 2; ++c)
            {
                const float x = ch[c][i] * g;
                const float y = dc[c].process (adaa (x, xPrev[c])) * comp;
                xPrev[c] = x;
                ch[c][i] = ch[c][i] * (1.0f - m) + y * m;
            }
        }
    }

private:
    static constexpr float tubeBias = 0.35f;

    static float logCosh (float x) noexcept
    {
        const float a = std::abs (x);
        return a + std::log1p (std::exp (-2.0f * a)) - 0.6931472f;
    }

    float shape (float x) const noexcept
    {
        switch (curve)
        {
            case 1:  return std::tanh (x + tubeBias) - std::tanh (tubeBias);
            case 2:  return juce::jlimit (-1.0f, 1.0f, x);
            default: return std::tanh (x);
        }
    }

    float antiderivative (float x) const noexcept
    {
        switch (curve)
        {
            case 1:  return logCosh (x + tubeBias) - x * std::tanh (tubeBias);
            case 2:  return std::abs (x) <= 1.0f ? 0.5f * x * x : std::abs (x) - 0.5f;
            default: return logCosh (x);
        }
    }

    float adaa (float x, float x1) const noexcept
    {
        const float d = x - x1;
        if (std::abs (d) < 1.0e-4f)
            return shape (0.5f * (x + x1));
        return (antiderivative (x) - antiderivative (x1)) / d;
    }

    double sr = 44100.0;
    int curve = 0;
    float xPrev[2] {};
    OnePoleHP dc[2];
    juce::SmoothedValue<float> mixS, gainS;
};

//==============================================================================
/** Tilt EQ around 1 kHz plus low-cut and high-cut filters. */
class ToneEQ
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec { sr, 4096, 1 };
        for (auto& chain : filters)
            for (auto& f : chain)
            {
                // Start every stage as a second-order filter so later coefficient updates never resize state.
                f.coefficients = new juce::dsp::IIR::Coefficients<float> (1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
                f.prepare (spec);
                f.reset();
            }
        lastTilt = lastLow = lastHigh = -1000.0f;
        wasOn = false;
    }

    void process (float* L, float* R, int n, bool on, float tilt, float lowHz, float highHz)
    {
        if (! on)
        {
            wasOn = false;
            return;
        }
        if (! wasOn)
            for (auto& chain : filters)
                for (auto& f : chain)
                    f.reset();
        wasOn = true;

        if (tilt != lastTilt || lowHz != lastLow || highHz != lastHigh)
        {
            lastTilt = tilt;
            lastLow = lowHz;
            lastHigh = highHz;
            using C = juce::dsp::IIR::ArrayCoefficients<float>; // no allocation on the audio thread
            const float gdb = tilt * 6.0f;
            auto lowShelf = C::makeLowShelf (sr, 700.0f, 0.6f, juce::Decibels::decibelsToGain (-gdb));
            auto highShelf = C::makeHighShelf (sr, 1400.0f, 0.6f, juce::Decibels::decibelsToGain (gdb));
            auto hp = C::makeHighPass (sr, juce::jmax (10.0f, lowHz), 0.707f);
            auto lp = C::makeLowPass (sr, juce::jmin (highHz, (float) sr * 0.45f), 0.707f);
            for (auto& chain : filters)
            {
                *chain[0].coefficients = lowShelf;
                *chain[1].coefficients = highShelf;
                *chain[2].coefficients = hp;
                *chain[3].coefficients = lp;
            }
        }

        const bool useLow = lowHz > 21.0f, useHigh = highHz < 19900.0f, useTilt = std::abs (tilt) > 0.005f;
        float* ch[2] { L, R };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                float x = ch[c][i];
                if (useTilt) x = filters[c][1].processSample (filters[c][0].processSample (x));
                if (useLow)  x = filters[c][2].processSample (x);
                if (useHigh) x = filters[c][3].processSample (x);
                ch[c][i] = x;
            }
    }

private:
    double sr = 44100.0;
    std::array<std::array<juce::dsp::IIR::Filter<float>, 4>, 2> filters;
    float lastTilt = -1000.0f, lastLow = -1000.0f, lastHigh = -1000.0f;
    bool wasOn = false;
};

//==============================================================================
/** Lookahead brickwall limiter: sliding-minimum gain with instant attack, smooth release,
    and a box filter over the lookahead window so gain changes never click. The signal is
    always delayed by the lookahead (reported to the host as latency), even when bypassed. */
class Limiter
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        look = juce::jmax (1, (int) std::round (sr * 0.0015));
        delay.prepare (look + 8);
        box.assign ((size_t) look, 1.0f);
        boxSum = (double) look;
        boxIdx = 0;
        cap = look + 4;
        dqVal.assign ((size_t) cap, 1.0f);
        dqT.assign ((size_t) cap, 0);
        dqHead = dqCount = 0;
        t = 0;
        env = 1.0f;
        relCoef = 1.0f - std::exp (-1.0f / (float) (0.08 * sr));
    }

    int getLatency() const { return look; }

    /** Returns the lowest gain applied during the block. */
    float process (float* L, float* R, int n, bool on, float ceilingDb)
    {
        const float ceiling = juce::Decibels::decibelsToGain (ceilingDb);
        float minGain = 1.0f;

        for (int i = 0; i < n; ++i)
        {
            const float peak = juce::jmax (std::abs (L[i]), std::abs (R[i]));
            const float req = on && peak > ceiling ? ceiling / peak : 1.0f;
            delay.push (L[i], R[i]);

            const float held = slidingMin (req);
            env = held < env ? held : env + (held - env) * relCoef;

            boxSum += (double) env - (double) box[(size_t) boxIdx];
            box[(size_t) boxIdx] = env;
            if (++boxIdx >= look) boxIdx = 0;
            const float g = juce::jmin (1.0f, (float) (boxSum / look));

            float l = delay.at (0, look) * g;
            float r = delay.at (1, look) * g;
            if (on)
            {
                l = juce::jlimit (-ceiling, ceiling, l); // safety for float rounding
                r = juce::jlimit (-ceiling, ceiling, r);
            }
            L[i] = l;
            R[i] = r;
            minGain = juce::jmin (minGain, g);
        }
        return minGain;
    }

private:
    // Minimum of the last (look + 1) required-gain values, via a monotonic queue.
    float slidingMin (float v)
    {
        while (dqCount > 0 && dqVal[(size_t) back()] >= v)
            --dqCount;
        const int slot = (dqHead + dqCount) % cap;
        dqVal[(size_t) slot] = v;
        dqT[(size_t) slot] = t;
        ++dqCount;
        while (dqT[(size_t) dqHead] <= t - (look + 1))
        {
            dqHead = (dqHead + 1) % cap;
            --dqCount;
        }
        ++t;
        return dqVal[(size_t) dqHead];
    }

    int back() const { return (dqHead + dqCount - 1) % cap; }

    double sr = 44100.0;
    int look = 64, boxIdx = 0, cap = 1, dqHead = 0, dqCount = 0;
    juce::int64 t = 0;
    Ring delay;
    std::vector<float> box, dqVal;
    std::vector<juce::int64> dqT;
    double boxSum = 0.0;
    float env = 1.0f, relCoef = 0.001f;
};

} // namespace fx
