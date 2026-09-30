#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace params
{
inline constexpr int numPads = 16;

inline juce::String padId (int pad, const char* what) { return "p" + juce::String (pad + 1) + "_" + what; }

// Sample formats ChopShop loads: "wav;aif;..." for File::hasFileExtension, "*.wav;*.aif;..." for choosers/iterators.
inline const juce::String audioExtensions { "wav;aif;aiff;flac;mp3;m4a;caf;ogg" };
inline const juce::String audioWildcard   { "*." + audioExtensions.replace (";", ";*.") };

inline const juce::StringArray sliceModes   { "Equal", "Transient" };
inline const juce::StringArray padModes     { "One Shot", "Note On" };
inline const juce::StringArray stutterRates { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
inline constexpr double stutterBeats[]      { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
inline const juce::StringArray halfLengths  { "1 Beat", "2 Beats", "1 Bar", "2 Bars" };
inline constexpr double halfBeats[]         { 1.0, 2.0, 4.0, 8.0 };
inline const juce::StringArray driveTypes   { "Tape", "Tube", "Fuzz" };
inline const juce::StringArray stretchModes { "Manual", "Sync" };
inline const juce::StringArray delayTimes   { "1/16", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2" };
inline constexpr double delayBeats[]        { 0.25, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0 };

inline juce::String fmtMs (float v)
{
    return v < 1000.0f ? juce::String (v, v < 10.0f ? 1 : 0) + " ms"
                       : juce::String (v / 1000.0f, 2) + " s";
}
inline juce::String fmtSec (float v)  { return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, 2) + " s"; }
inline juce::String fmtPct (float v)  { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
inline juce::String fmtDb (float v)   { return v <= -47.9f ? juce::String ("-inf") : juce::String (v, 1) + " dB"; }
inline juce::String fmtSemi (float v) { return (v > 0.004f ? "+" : "") + juce::String (v, 2) + " st"; }
inline juce::String fmtInt (float v)  { return juce::String (juce::roundToInt (v)); }
inline juce::String fmtPan (float v)
{
    const int p = juce::roundToInt (v * 100.0f);
    return p == 0 ? juce::String ("C") : (p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p));
}
inline juce::String fmtHz (float v)   { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto addFloat = [&] (const juce::String& id, const juce::String& name, juce::NormalisableRange<float> range,
                         float def, std::function<juce::String (float)> fmt)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, range, def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([fmt] (float v, int) { return fmt (v); })));
    };
    auto addChoice = [&] (const juce::String& id, const juce::String& name, const juce::StringArray& c, int def)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, c, def));
    };
    auto addBool = [&] (const juce::String& id, const juce::String& name, bool def)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
    };
    auto skewed = [] (float lo, float hi, float centre, float step = 0.0f)
    {
        juce::NormalisableRange<float> r (lo, hi, step);
        r.setSkewForCentre (centre);
        return r;
    };

    // Master / envelope
    addFloat ("master", "Master", { -36.0f, 12.0f, 0.1f }, 0.0f, fmtDb);
    addBool ("limOn", "Limiter", true);
    addFloat ("limCeil", "Limiter Ceiling", { -12.0f, 0.0f, 0.1f }, -0.3f, fmtDb);
    addFloat ("attack", "Attack", skewed (0.0f, 2000.0f, 60.0f), 0.0f, fmtMs);
    addFloat ("decay", "Decay", skewed (1.0f, 4000.0f, 300.0f), 600.0f, fmtMs);
    addFloat ("sustain", "Sustain", { 0.0f, 1.0f }, 1.0f, fmtPct);
    addFloat ("release", "Release", skewed (1.0f, 4000.0f, 200.0f), 40.0f, fmtMs);

    // Slicer
    addChoice ("sliceMode", "Slice Mode", sliceModes, 1);
    addFloat ("sliceCount", "Slices", { 2.0f, 16.0f, 1.0f }, 16.0f, fmtInt);
    addFloat ("sliceSens", "Sensitivity", { 0.0f, 1.0f }, 0.5f, fmtPct);

    // Pads
    for (int i = 0; i < numPads; ++i)
    {
        const auto n = "Pad " + juce::String (i + 1) + " ";
        addFloat (padId (i, "tune"), n + "Tune", { -24.0f, 24.0f, 0.01f }, 0.0f, fmtSemi);
        auto lvl = skewed (-48.0f, 6.0f, -12.0f, 0.1f);
        addFloat (padId (i, "level"), n + "Level", lvl, 0.0f, fmtDb);
        addFloat (padId (i, "pan"), n + "Pan", { -1.0f, 1.0f, 0.01f }, 0.0f, fmtPan);
        addBool (padId (i, "rev"), n + "Reverse", false);
        addChoice (padId (i, "mode"), n + "Mode", padModes, 0);
    }

    // Randomizer
    addBool ("rndOn", "Randomizer", false);
    addFloat ("rndPitch", "Rnd Pitch", { 0.0f, 12.0f, 1.0f }, 3.0f, [] (float v) { return juce::String (juce::roundToInt (v)) + " st"; });
    addFloat ("rndPan", "Rnd Pan", { 0.0f, 1.0f }, 0.5f, fmtPct);
    addFloat ("rndLevel", "Rnd Level", { 0.0f, 1.0f }, 0.3f, fmtPct);
    addFloat ("rndRev", "Rnd Reverse", { 0.0f, 1.0f }, 0.2f, fmtPct);
    addFloat ("rndSlice", "Rnd Slice", { 0.0f, 1.0f }, 0.25f, fmtPct);

    // Performance FX
    addBool ("stutOn", "Stutter", false);
    addChoice ("stutRate", "Stutter Rate", stutterRates, 3);
    addFloat ("stutGate", "Stutter Gate", { 0.1f, 1.0f }, 1.0f, fmtPct);
    addFloat ("stutMix", "Stutter Mix", { 0.0f, 1.0f }, 1.0f, fmtPct);

    addBool ("halfOn", "Half-Time", false);
    addChoice ("halfLen", "Half-Time Length", halfLengths, 2);
    addFloat ("halfMix", "Half-Time Mix", { 0.0f, 1.0f }, 1.0f, fmtPct);

    addBool ("stopOn", "Tape Stop", false);
    addFloat ("stopTime", "Stop Time", skewed (0.05f, 4.0f, 0.8f), 0.8f, fmtSec);
    addFloat ("stopCurve", "Stop Curve", { 0.0f, 1.0f }, 0.4f, fmtPct);
    addFloat ("stopSpin", "Spin Up", { 0.0f, 2.0f }, 0.25f, [] (float v) { return v < 0.01f ? juce::String ("Off") : fmtSec (v); });

    // Time-stretch (applies to the chopped main sample)
    addBool ("strOn", "Stretch", false);
    addChoice ("strMode", "Stretch Mode", stretchModes, 1);
    addFloat ("strRatio", "Stretch Ratio", skewed (0.25f, 4.0f, 1.0f, 0.01f), 1.0f,
              [] (float v) { return juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) + juce::String (v, 2); });
    addFloat ("strBpm", "Source BPM", { 40.0f, 240.0f, 0.1f }, 92.0f, [] (float v) { return juce::String (v, 1); });
    addFloat ("strGrain", "Grain Size", { 20.0f, 200.0f, 1.0f }, 70.0f, fmtMs);

    // Drive: saturation + tone
    addBool ("drvOn", "Saturation", false);
    addFloat ("drvAmt", "Drive", { 0.0f, 1.0f }, 0.35f, fmtPct);
    addChoice ("drvType", "Saturation Type", driveTypes, 0);
    addFloat ("drvMix", "Saturation Mix", { 0.0f, 1.0f }, 1.0f, fmtPct);
    addBool ("toneOn", "Tone", false);
    addFloat ("toneTilt", "Tilt", { -1.0f, 1.0f, 0.01f }, 0.0f,
              [] (float v) { return std::abs (v) < 0.005f ? juce::String ("Flat") : (v > 0 ? "Bright " : "Dark ") + fmtPct (std::abs (v)); });
    addFloat ("toneLow", "Low Cut", skewed (20.0f, 800.0f, 120.0f, 1.0f), 20.0f, [] (float v) { return v < 21.0f ? juce::String ("Off") : fmtHz (v); });
    addFloat ("toneHigh", "High Cut", skewed (1000.0f, 20000.0f, 6000.0f, 10.0f), 20000.0f, [] (float v) { return v > 19900.0f ? juce::String ("Off") : fmtHz (v); });

    // Tape delay
    addBool ("dlyOn", "Delay", false);
    addChoice ("dlyTime", "Delay Time", delayTimes, 3);
    addFloat ("dlyFb", "Delay Feedback", { 0.0f, 0.95f }, 0.45f, fmtPct);
    addFloat ("dlyWow", "Delay Wow", { 0.0f, 1.0f }, 0.3f, fmtPct);
    addFloat ("dlyTone", "Delay Tone", { 0.0f, 1.0f }, 0.5f, fmtPct);
    addFloat ("dlyAge", "Delay Age", { 0.0f, 1.0f }, 0.4f, fmtPct);
    addFloat ("dlyMix", "Delay Mix", { 0.0f, 1.0f }, 0.35f, fmtPct);

    // Tape reverb
    addBool ("revOn", "Reverb", false);
    addFloat ("revSize", "Reverb Size", { 0.0f, 1.0f }, 0.55f, fmtPct);
    addFloat ("revDecay", "Reverb Decay", skewed (0.2f, 12.0f, 2.5f), 2.2f, fmtSec);
    addFloat ("revPre", "Reverb Pre-Delay", { 0.0f, 200.0f, 0.1f }, 20.0f, fmtMs);
    addFloat ("revTone", "Reverb Tone", { 0.0f, 1.0f }, 0.45f, fmtPct);
    addFloat ("revWow", "Reverb Wow", { 0.0f, 1.0f }, 0.3f, fmtPct);
    addFloat ("revMix", "Reverb Mix", { 0.0f, 1.0f }, 0.3f, fmtPct);

    // Widener
    addBool ("widOn", "Widener", false);
    addFloat ("widWidth", "Width", { 0.0f, 2.0f }, 1.4f, fmtPct);
    addFloat ("widHaas", "Haas", { 0.0f, 25.0f, 0.1f }, 0.0f, fmtMs);
    addBool ("widBass", "Mono Low", true);

    // Vintage (early-sampler grit)
    addBool ("vinOn", "Vintage", false);
    addFloat ("vinBits", "Bits", { 4.0f, 16.0f, 0.1f }, 12.0f, [] (float v) { return juce::String (v, 1) + " bit"; });
    addFloat ("vinRate", "Sample Rate", skewed (2000.0f, 48000.0f, 12000.0f, 10.0f), 26040.0f, fmtHz);

    return layout;
}
} // namespace params
