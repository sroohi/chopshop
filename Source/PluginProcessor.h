#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "Params.h"
#include "Sampler.h"
#include "Effects.h"

class ChopShopProcessor : public juce::AudioProcessor,
                          public juce::ChangeBroadcaster,
                          private juce::AudioProcessorValueTreeState::Listener,
                          private juce::AsyncUpdater,
                          private juce::Timer
{
public:
    static constexpr int numPads = Kit::numPads;
    static constexpr int padNoteBase = 36;   // C1..D#2 -> pads 1..16 (MPC layout)
    static constexpr int perfNoteBase = 24;  // C0 stutter, C#0 half-time, D0 tape stop (momentary)
    static constexpr int chromaticRoot = 60; // other notes play the selected pad chromatically

    ChopShopProcessor();
    ~ChopShopProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //== Kit editing (message thread) ==========================================
    Kit::Ptr getKit() const;
    bool loadMainSample (const juce::File&);
    bool loadPadSample (int pad, const juce::File&);
    void clearPadSample (int pad);
    void setSlices (std::vector<int> points);
    void shuffleSlices();
    void resetSliceMap();
    void loadDemo();
    void rechop (bool force);
    bool isSupportedAudioFile (const juce::String& path) const;

    //== Library preview (message thread) =======================================
    void previewFile (const juce::File&);
    void stopPreview();
    std::atomic<bool> previewPlaying { false };

    //== UI -> audio ============================================================
    void padNoteOn (int pad, float velocity);
    void padNoteOff (int pad);
    void setSelectedPad (int pad) { selectedPad = juce::jlimit (0, numPads - 1, pad); }
    int getSelectedPad() const { return selectedPad.load(); }

    //== Audio -> UI ============================================================
    std::atomic<float> padGlow[numPads];
    std::atomic<int> playheadPos { -1 };
    std::atomic<double> currentBpm { 120.0 };
    std::atomic<bool> perfActive[3];
    std::atomic<float> outPeak[2];      // linear, post-limiter
    std::atomic<float> gainReduction { 0.0f }; // dB, positive

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioFormatManager formatManager;

private:
    struct Voice
    {
        bool active = false, gate = false;
        int pad = -1, note = -1;
        Kit::Ptr kit;
        const SampleData* sample = nullptr;
        int start = 0, end = 0;
        double pos = 0.0, inc = 1.0;
        bool reverse = false;
        float gainL = 1.0f, gainR = 1.0f;
        float fadeIn = 16.0f, fadeOut = 64.0f;
        juce::ADSR env;
        juce::uint64 age = 0;

        // Time-stretch (granular): srcPos advances with time, grains read at pitch speed.
        bool stretched = false;
        double baseRate = 1.0, srcPos = 0.0;
        double grainRp[2] {};
        float grainPh[2] {};
    };

    struct PadParams
    {
        std::atomic<float>* tune = nullptr;
        std::atomic<float>* level = nullptr;
        std::atomic<float>* pan = nullptr;
        std::atomic<float>* rev = nullptr;
        std::atomic<float>* mode = nullptr;
    };

    void parameterChanged (const juce::String& id, float) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    void publishKit (Kit::Ptr newKit);
    Kit::Ptr copyKit() const;
    SampleData::Ptr readAudioFile (const juce::File&, double maxSeconds = 600.0);
    void renderPreview (float* L, float* R, int n, SampleData* s);
    std::vector<int> chop (const SampleData&) const;
    void stampChopSettings (Kit&) const;
    void publishChopped (Kit::Ptr);          // chop k->main, reset the pad map and publish
    void setMainSample (SampleData::Ptr);
    void restoreKit (const juce::ValueTree&);

    void handleMidi (const juce::MidiMessage&, const Kit&);
    void triggerPad (int pad, float velocity, int note, float extraSemis, const Kit&);
    void renderVoices (float* L, float* R, int start, int num);
    void renderStretched (Voice&, float* L, float* R, int start, int num);
    void setSourceBpmFor (const SampleData&);

    std::atomic<float>* p (const char* id) const { return apvts.getRawParameterValue (id); }

    Kit::Ptr kit;
    mutable juce::SpinLock kitLock;
    juce::ReferenceCountedArray<Kit> garbage;
    juce::ReferenceCountedArray<SampleData> sampleGarbage;
    juce::CriticalSection garbageLock;

    SampleData::Ptr preview;
    double previewPos = 0.0;
    std::atomic<bool> previewRestart { false };

    std::array<Voice, 32> voices;
    juce::uint64 voiceCounter = 0;
    PadParams padParams[numPads];
    std::atomic<int> selectedPad { 0 };
    bool momentary[3] {};

    juce::MidiMessageCollector uiMidi;
    juce::MidiBuffer uiBuffer;
    std::atomic<bool> prepared { false };
    double hostRate = 44100.0;
    juce::Random random;
    juce::SmoothedValue<float> masterGain;
    double stretchT = 1.0;   // time scale for stretched voices (>1 = slower)
    float grainLen = 3000.0f;

    fx::Vintage vintage;
    fx::Stutter stutter;
    fx::HalfTime halfTime;
    fx::TapeDelay tapeDelay;
    fx::TapeReverb tapeReverb;
    fx::TapeStop tapeStop;
    fx::Widener widener;
    fx::Drive drive;
    fx::ToneEQ tone;
    fx::Limiter limiter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChopShopProcessor)
};
