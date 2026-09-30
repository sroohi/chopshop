#pragma once

#include "Sampler.h"
#include <cmath>

/** Synthesises a two-bar 92 BPM drum break with bass and an Am9 stab, so the instrument
    has something to chop the moment it is opened. */
inline SampleData::Ptr makeDemoBreak()
{
    const double sr = 44100.0, bpm = 92.0;
    const int step = (int) std::round (sr * 60.0 / bpm / 4.0);
    const int len = step * 32;
    const float twoPi = juce::MathConstants<float>::twoPi;

    SampleData::Ptr s = new SampleData();
    s->sampleRate = sr;
    s->name = "Demo Break 92 BPM";
    s->isDemo = true;
    s->buffer.setSize (2, len);
    s->buffer.clear();
    float* L = s->buffer.getWritePointer (0);
    float* R = s->buffer.getWritePointer (1);
    juce::Random rng (1979);

    auto at = [&] (int st) { return st * step + ((st & 1) ? (int) (step * 0.16) : 0); }; // light swing

    auto add = [&] (int start, int i, float l, float r)
    {
        if (start + i < len) { L[start + i] += l; R[start + i] += r; }
    };

    auto kick = [&] (int st, float vel)
    {
        const int s0 = at (st);
        double ph = 0.0;
        for (int i = 0; i < (int) (sr * 0.45); ++i)
        {
            const double t = i / sr;
            ph += twoPi * (47.0 + 115.0 * std::exp (-t * 30.0)) / sr;
            float v = (float) (std::sin (ph) * std::exp (-t * 7.0));
            if (i < 80) v += (rng.nextFloat() * 2.0f - 1.0f) * 0.25f * (1.0f - i / 80.0f);
            v = std::tanh (v * 1.8f) * 0.85f * vel;
            add (s0, i, v, v);
        }
    };

    auto snare = [&] (int st, float vel)
    {
        const int s0 = at (st);
        float lp = 0.0f;
        for (int i = 0; i < (int) (sr * 0.3); ++i)
        {
            const float t = (float) (i / sr);
            const float n = rng.nextFloat() * 2.0f - 1.0f;
            lp += 0.35f * (n - lp);
            const float noise = (n - lp) * std::exp (-t * 15.0f) * 0.6f;
            const float tone = std::sin (twoPi * 188.0f * t) * std::exp (-t * 28.0f) * 0.45f;
            const float v = (noise + tone) * vel;
            add (s0, i, v * 0.95f, v);
        }
    };

    auto hat = [&] (int st, float vel, bool open)
    {
        const int s0 = at (st);
        float lp1 = 0.0f, lp2 = 0.0f;
        const float dec = open ? 9.0f : 55.0f;
        for (int i = 0; i < (int) (sr * (open ? 0.35 : 0.08)); ++i)
        {
            const float t = (float) (i / sr);
            const float n = rng.nextFloat() * 2.0f - 1.0f;
            lp1 += 0.55f * (n - lp1);
            const float hp1 = n - lp1;
            lp2 += 0.55f * (hp1 - lp2);
            const float v = (hp1 - lp2) * std::exp (-t * dec) * 0.35f * vel;
            add (s0, i, v * 0.8f, v);
        }
    };

    auto bass = [&] (int st, float hz, float dur)
    {
        const int s0 = at (st);
        for (int i = 0; i < (int) (sr * dur); ++i)
        {
            const float t = (float) (i / sr);
            const float env = std::min (1.0f, t * 200.0f) * std::exp (-t * 2.2f);
            const float v = std::tanh ((std::sin (twoPi * hz * t) + 0.3f * std::sin (twoPi * hz * 2.0f * t)) * 1.5f) * 0.28f * env;
            add (s0, i, v, v);
        }
    };

    auto stab = [&] (int st)
    {
        const int s0 = at (st);
        const float notes[] { 220.0f, 261.63f, 329.63f, 392.0f, 493.88f };
        for (int i = 0; i < (int) (sr * 1.4); ++i)
        {
            const float t = (float) (i / sr);
            const float env = std::min (1.0f, t * 120.0f) * std::exp (-t * 2.6f);
            float l = 0.0f, r = 0.0f;
            for (float f : notes)
            {
                l += std::sin (twoPi * f * 0.998f * t) + 0.25f * std::sin (twoPi * f * 2.0f * t);
                r += std::sin (twoPi * f * 1.002f * t) + 0.25f * std::sin (twoPi * f * 2.0f * t);
            }
            add (s0, i, l * 0.035f * env, r * 0.035f * env);
        }
    };

    for (int st : { 0, 7, 10, 16, 23, 26 }) kick (st, st % 8 == 0 ? 1.0f : 0.8f);
    for (int st : { 4, 12, 20, 28 }) snare (st, 1.0f);
    for (int st : { 15, 25 }) snare (st, 0.3f);
    for (int st = 0; st < 32; st += 2)
        hat (st, st % 4 == 0 ? 0.9f : 0.6f, st == 14 || st == 30);
    bass (0, 55.0f, 0.9f);
    bass (10, 49.0f, 0.6f);
    bass (16, 43.65f, 0.9f);
    bass (26, 41.2f, 0.6f);
    stab (0);
    stab (16);

    const float peak = s->buffer.getMagnitude (0, len);
    if (peak > 0.0f)
        s->buffer.applyGain (0.89f / peak);
    return s;
}
