#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>

/** Decoded audio, always stored as two channels. Immutable once shared with the audio thread. */
struct SampleData : juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;

    juce::AudioBuffer<float> buffer;
    double sampleRate = 44100.0;
    juce::String name;
    juce::String path;   // empty for generated audio
    bool isDemo = false;

    int length() const { return buffer.getNumSamples(); }
};

/** A complete "program": the main sample, its slice points, and what each pad plays.
    Kits are immutable once published; edits build a modified copy and swap it in. */
struct Kit : juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<Kit>;
    static constexpr int numPads = 16;

    SampleData::Ptr main;
    std::vector<int> slices;                           // ascending start points, first is always 0
    std::array<int, numPads> padSlice;                 // slice index per pad, -1 = none
    std::array<SampleData::Ptr, numPads> padSample;    // per-pad sample overriding the slice

    // Slicer settings this kit was chopped with, so we only re-chop when they change.
    int chopMode = -1, chopCount = -1;
    float chopSens = -1.0f;

    Kit() { padSlice.fill (-1); }
    Kit (const Kit&) = default;

    struct Region
    {
        const SampleData* sample = nullptr;
        int start = 0, end = 0;
        bool valid() const { return sample != nullptr && end > start + 8; }
    };

    int numSlices() const { return (int) slices.size(); }

    std::pair<int, int> sliceRange (int s) const
    {
        const int len = main != nullptr ? main->length() : 0;
        const int a = slices[(size_t) s];
        const int b = s + 1 < numSlices() ? slices[(size_t) s + 1] : len;
        return { a, b };
    }

    Region region (int pad) const
    {
        if (pad < 0 || pad >= numPads)
            return {};
        if (auto* s = padSample[(size_t) pad].get())
            return { s, 0, s->length() };
        const int sl = padSlice[(size_t) pad];
        if (main != nullptr && sl >= 0 && sl < numSlices())
        {
            auto [a, b] = sliceRange (sl);
            return { main.get(), a, b };
        }
        return {};
    }

    int padForSlice (int s) const
    {
        for (int p = 0; p < numPads; ++p)
            if (padSlice[(size_t) p] == s && padSample[(size_t) p] == nullptr)
                return p;
        return -1;
    }

    void resetMap()
    {
        padSlice.fill (-1);
        for (int i = 0; i < juce::jmin (numPads, numSlices()); ++i)
            padSlice[(size_t) i] = i;
    }
};
