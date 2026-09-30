#pragma once

#include "Sampler.h"
#include <algorithm>
#include <cmath>

namespace slicer
{
/** Moves a position back to the nearest preceding zero crossing (within a short search window). */
inline int snapToZeroCrossing (const juce::AudioBuffer<float>& b, int pos, int maxSearch = 512)
{
    if (pos <= 0)
        return 0;
    const float* l = b.getReadPointer (0);
    const float* r = b.getReadPointer (1);
    auto mono = [&] (int i) { return l[i] + r[i]; };
    for (int i = pos; i > juce::jmax (1, pos - maxSearch); --i)
        if ((mono (i - 1) <= 0.0f && mono (i) >= 0.0f) || (mono (i - 1) >= 0.0f && mono (i) <= 0.0f))
            return i;
    return pos;
}

inline std::vector<int> equal (const SampleData& s, int count)
{
    std::vector<int> out;
    const int len = s.length();
    count = juce::jlimit (1, Kit::numPads, count);
    for (int i = 0; i < count; ++i)
        out.push_back (i == 0 ? 0 : snapToZeroCrossing (s.buffer, (int) ((int64_t) len * i / count), 256));
    return out;
}

/** Energy-flux onset detection. Sensitivity 0..1 lowers the threshold; at most maxSlices are kept,
    choosing the strongest onsets. */
inline std::vector<int> transients (const SampleData& s, float sensitivity, int maxSlices)
{
    const int len = s.length();
    const int hop = 256, frame = 512;
    const int numFrames = (len - frame) / hop;
    if (numFrames < 4)
        return { 0 };

    const float* l = s.buffer.getReadPointer (0);
    const float* r = s.buffer.getReadPointer (1);

    std::vector<float> db ((size_t) numFrames);
    for (int f = 0; f < numFrames; ++f)
    {
        double e = 0.0;
        for (int i = f * hop; i < f * hop + frame; ++i)
        {
            const float m = (l[i] + r[i]) * 0.5f;
            e += (double) m * m;
        }
        db[(size_t) f] = (float) (10.0 * std::log10 (e / frame + 1e-10));
    }

    const float threshold = juce::jmap (sensitivity, 12.0f, 2.5f);
    const int minGap = juce::jmax (2, (int) (0.06 * s.sampleRate / hop));

    struct Onset { int frame; float strength; };
    std::vector<Onset> onsets;
    for (int f = 2; f < numFrames - 1; ++f)
    {
        const float rise = db[(size_t) f] - juce::jmin (db[(size_t) f - 1], db[(size_t) f - 2]);
        if (rise > threshold && db[(size_t) f] > -55.0f)
        {
            const float nextRise = db[(size_t) f + 1] - db[(size_t) f - 1];
            if (rise >= nextRise) // local maximum of the flux
                onsets.push_back ({ f, rise });
        }
    }

    // Keep the strongest onsets, enforcing a minimum gap between them.
    std::sort (onsets.begin(), onsets.end(), [] (auto& a, auto& b) { return a.strength > b.strength; });
    std::vector<int> chosen;
    for (auto& o : onsets)
    {
        if ((int) chosen.size() >= maxSlices - 1)
            break;
        if (o.frame < minGap)
            continue;
        bool clash = false;
        for (int c : chosen)
            if (std::abs (c - o.frame) < minGap)
                clash = true;
        if (! clash)
            chosen.push_back (o.frame);
    }
    std::sort (chosen.begin(), chosen.end());

    std::vector<int> out { 0 };
    for (int f : chosen)
    {
        // The rise is detected in the frame that contains the hit; start a little earlier.
        const int pos = juce::jmax (0, f * hop - hop / 2);
        out.push_back (snapToZeroCrossing (s.buffer, pos, 384));
    }
    std::sort (out.begin(), out.end());
    out.erase (std::unique (out.begin(), out.end()), out.end());
    return out;
}

/** Tempo of a loop: taken from the file name when it carries one (e.g. "loop_90_Gmin"),
    otherwise the tempo that makes the sample a whole number of 4/4 bars closest to 100 BPM. */
inline double estimateBpm (const SampleData& s)
{
    if (s.isDemo)
        return 92.0;
    const auto tokens = juce::StringArray::fromTokens (s.name, "_- ().", "");
    for (int pass = 0; pass < 2; ++pass) // explicit "...bpm" tokens first, then bare numbers
        for (auto& t : tokens)
        {
            const auto lower = t.toLowerCase();
            if (pass == 0 && ! lower.endsWith ("bpm"))
                continue;
            const auto tok = lower.upToFirstOccurrenceOf ("bpm", false, false);
            if (tok.isNotEmpty() && tok.containsOnly ("0123456789.") && tok.getDoubleValue() >= 60.0 && tok.getDoubleValue() <= 200.0)
                return tok.getDoubleValue();
        }
    const double secs = s.length() / s.sampleRate;
    double best = 100.0, bestDist = 1.0e9;
    for (int bars : { 1, 2, 4, 8, 16 })
    {
        const double bpm = 240.0 * bars / secs;
        if (bpm >= 60.0 && bpm <= 180.0 && std::abs (bpm - 100.0) < bestDist)
        {
            best = bpm;
            bestDist = std::abs (bpm - 100.0);
        }
    }
    return best;
}

/** Sorts, de-duplicates and bounds a user-edited list of slice points. */
inline std::vector<int> sanitise (std::vector<int> v, int length)
{
    std::sort (v.begin(), v.end());
    std::vector<int> out { 0 };
    for (int p : v)
        if (p > out.back() + 32 && p < length - 32 && (int) out.size() < Kit::numPads)
            out.push_back (p);
    return out;
}
} // namespace slicer
