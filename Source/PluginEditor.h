#pragma once

#include "PluginProcessor.h"
#include "UI/Style.h"
#include "UI/PadGrid.h"
#include "UI/WaveformView.h"
#include "UI/LibraryPanel.h"

/** The whole interface at its native size; the editor scales it to fit the window. */
class MainView : public juce::Component,
                 private juce::ChangeListener,
                 private juce::Timer
{
public:
    static constexpr int W = 1496, H = 880;

    explicit MainView (ChopShopProcessor&);
    ~MainView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool handleKeys();

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void bindPad (int pad);
    void refreshKit();
    void attachButton (juce::Button&, const juce::String& id);
    void paintMeters (juce::Graphics&);

    ChopShopProcessor& proc;

    // Top bar
    juce::TextButton loadBtn { "LOAD" }, demoBtn { "DEMO" };
    juce::ToggleButton limOn { "LIMITER" };
    ui::ValueBox limCeil { "CEILING" };
    ui::Knob master { "MASTER" };
    std::unique_ptr<juce::FileChooser> chooser;
    float meterL = 0.0f, meterR = 0.0f, meterGr = 0.0f;

    LibraryPanel library;

    // Monitor
    WaveformView wave;
    ui::ValueBox padTune { "TUNE", true }, padLevel { "LEVEL" }, padPan { "PAN", true }, padMode { "PLAY" };
    ui::ValueBox att { "ATTACK" }, dec { "DECAY" }, sus { "SUSTAIN" }, rel { "RELEASE" };
    juce::ToggleButton padRev { "REVERSE" };
    std::unique_ptr<APVTS::ButtonAttachment> padRevAtt;

    PadGrid pads;

    // MAIN box
    ui::ModuleBox mainBox { "MAIN", "HALF-TIME  /  STRETCH  /  SLICER" };
    juce::ToggleButton halfOn { "ON" }, strOn { "ON" };
    ui::ValueBox halfLen { "LENGTH" }, halfMix { "MIX" };
    ui::ValueBox strMode { "MODE" }, strRatio { "RATIO" }, strBpm { "SRC BPM" }, strGrain { "GRAIN" };
    ui::ValueBox sliceMode { "MODE" }, sliceCount { "SLICES" }, sliceSens { "SENSITIVITY" };
    juce::TextButton shuffleBtn { "SHUFFLE" }, resetMapBtn { "RESET MAP" }, rechopBtn { "RE-CHOP" };

    // FX box
    ui::ModuleBox fxBox { "FX", "WIDENER  /  RANDOMIZER  /  STUTTER" };
    juce::ToggleButton widOn { "ON" }, rndOn { "ON" }, stutOn { "ON" }, stopOn { "ON" }, widBass { "MONO LOW" };
    ui::ValueBox widWidth { "WIDTH" }, widHaas { "HAAS" };
    ui::ValueBox rndPitch { "PITCH" }, rndPan { "PAN" }, rndLevel { "LEVEL" }, rndRev { "REVERSE" }, rndSlice { "SLICE" };
    ui::ValueBox stutRate { "RATE" }, stutGate { "GATE" }, stutMix { "MIX" };
    ui::ValueBox stopTime { "TIME" }, stopCurve { "CURVE" }, stopSpin { "SPIN-UP" };

    // EFFECT box
    ui::ModuleBox effectBox { "EFFECT", "TAPE DELAY  /  TAPE REVERB" };
    juce::ToggleButton dlyOn { "ON" }, revOn { "ON" };
    ui::ValueBox dlyTime { "TIME" }, dlyFb { "FEEDBACK" }, dlyWow { "WOW" }, dlyTone { "TONE" }, dlyAge { "AGE" }, dlyMix { "MIX" };
    ui::ValueBox revSize { "SIZE" }, revDecay { "DECAY" }, revPre { "PRE-DELAY" }, revTone { "TONE" }, revWow { "WOW" }, revMix { "MIX" };

    // DRIVE box
    ui::ModuleBox driveBox { "DRIVE", "SATURATION  /  TONE" };
    juce::ToggleButton drvOn { "ON" }, toneOn { "ON" }, vinOn { "ON" };
    ui::ValueBox drvAmt { "DRIVE" }, drvType { "TYPE" }, drvMix { "MIX" };
    ui::ValueBox toneTilt { "TILT", true }, toneLow { "LOW CUT" }, toneHigh { "HIGH CUT" };
    ui::ValueBox vinBits { "BITS" }, vinRate { "RATE" };

    std::vector<std::unique_ptr<APVTS::ButtonAttachment>> buttonAtts;
    std::array<bool, 16> keysDown {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};

//==============================================================================
class ChopShopEditor : public juce::AudioProcessorEditor,
                       public juce::DragAndDropContainer
{
public:
    explicit ChopShopEditor (ChopShopProcessor&);
    ~ChopShopEditor() override;

    void resized() override;
    bool keyStateChanged (bool) override { return view.handleKeys(); }

private:
    ui::ChopLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    MainView view;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChopShopEditor)
};
