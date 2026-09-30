#pragma once

#include "PluginProcessor.h"
#include "UI/Style.h"
#include "UI/PadGrid.h"
#include "UI/WaveformView.h"
#include "UI/LibraryPanel.h"

class ChopShopEditor : public juce::AudioProcessorEditor,
                       public juce::DragAndDropContainer,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    explicit ChopShopEditor (ChopShopProcessor&);
    ~ChopShopEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyStateChanged (bool isKeyDown) override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void bindPad (int pad);
    void refreshKit();
    void attachButton (juce::Button&, const juce::String& id);
    void layoutKnobs (juce::Rectangle<int> area, std::initializer_list<juce::Component*> comps);

    ChopShopProcessor& proc;
    ui::ChopLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    struct Panel { juce::Rectangle<int> bounds; juce::String title; };
    std::vector<Panel> panels;

    // Top bar
    juce::TextButton loadBtn { "LOAD" }, demoBtn { "DEMO" };
    juce::Label info, bpmLabel;
    ui::Knob master { "MASTER" };
    std::unique_ptr<juce::FileChooser> chooser;

    LibraryPanel library;

    // Sample / slicer
    WaveformView wave;
    ui::Knob sliceMode { "MODE" }, sliceCount { "SLICES" }, sliceSens { "SENS" };
    juce::TextButton shuffleBtn { "SHUFFLE" }, resetMapBtn { "RESET MAP" }, rechopBtn { "RE-CHOP" };
    juce::Label sliceHint;

    // Pads
    PadGrid pads;
    ui::Knob padTune { "TUNE", true }, padLevel { "LEVEL" }, padPan { "PAN", true }, padMode { "MODE" };
    juce::ToggleButton padRev { "REVERSE" };
    std::unique_ptr<APVTS::ButtonAttachment> padRevAtt;

    ui::Knob att { "ATTACK" }, dec { "DECAY" }, sus { "SUSTAIN" }, rel { "RELEASE" };

    juce::ToggleButton rndOn { "ON" };
    ui::Knob rndPitch { "PITCH" }, rndPan { "PAN" }, rndLevel { "LEVEL" }, rndRev { "REVERSE" }, rndSlice { "SLICE" };

    // Performance
    juce::TextButton stutBtn { "STUTTER" }, halfBtn { "HALF-TIME" }, stopBtn { "TAPE STOP" };
    ui::Knob stutRate { "RATE" }, stutGate { "GATE" }, stutMix { "MIX" };
    ui::Knob halfLen { "LENGTH" }, halfMix { "MIX" };
    ui::Knob stopTime { "TIME" }, stopCurve { "CURVE" }, stopSpin { "SPIN-UP" };

    // FX
    juce::ToggleButton dlyOn { "ON" }, revOn { "ON" }, widOn { "ON" }, widBass { "MONO LOW" }, vinOn { "ON" };
    ui::Knob dlyTime { "TIME" }, dlyFb { "FEEDBACK" }, dlyWow { "WOW" }, dlyTone { "TONE" }, dlyAge { "AGE" }, dlyMix { "MIX" };
    ui::Knob revSize { "SIZE" }, revDecay { "DECAY" }, revPre { "PRE-DLY" }, revTone { "TONE" }, revWow { "WOW" }, revMix { "MIX" };
    ui::Knob widWidth { "WIDTH" }, widHaas { "HAAS" };
    ui::Knob vinBits { "BITS" }, vinRate { "RATE" };

    std::vector<std::unique_ptr<APVTS::ButtonAttachment>> buttonAtts;
    std::array<bool, 16> keysDown {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChopShopEditor)
};
