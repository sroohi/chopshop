#include "PluginEditor.h"

namespace
{
constexpr int editorW = 1460, editorH = 780;
constexpr int libX = 16, libW = 244;
constexpr int ox = libX + libW + 16;   // main area left edge
constexpr int rx = ox + 780;           // right column left edge
constexpr int top = 64;

// Computer-keyboard pad layout, top row to bottom row (like the MPC software).
const char padKeys[16] { 'Z', 'X', 'C', 'V', 'A', 'S', 'D', 'F', 'Q', 'W', 'E', 'R', '1', '2', '3', '4' };
} // namespace

ChopShopEditor::ChopShopEditor (ChopShopProcessor& p)
    : AudioProcessorEditor (&p), proc (p), library (p), wave (p), pads (p)
{
    setLookAndFeel (&lnf);
    auto& s = proc.apvts;

    // Top bar
    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a sample to chop", LibraryPanel::libraryFolder(),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.caf;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                      proc.loadMainSample (fc.getResult());
                              });
    };
    demoBtn.onClick = [this] { proc.loadDemo(); };
    demoBtn.setTooltip ("Reload the built-in demo break");
    for (auto* l : { &info, &bpmLabel })
    {
        l->setColour (juce::Label::textColourId, ui::col::text);
        l->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*l);
    }
    info.setFont (ui::font (13.0f));
    bpmLabel.setFont (ui::font (13.0f, true));
    bpmLabel.setJustificationType (juce::Justification::centred);
    master.attach (s, "master");
    for (auto* c : std::initializer_list<juce::Component*> { &loadBtn, &demoBtn, &master, &library })
        addAndMakeVisible (c);

    // Sample / slicer
    addAndMakeVisible (wave);
    wave.onSelectPad = [this] (int pad) { bindPad (pad); };
    sliceMode.attach (s, "sliceMode");
    sliceCount.attach (s, "sliceCount");
    sliceSens.attach (s, "sliceSens");
    shuffleBtn.onClick = [this] { proc.shuffleSlices(); };
    resetMapBtn.onClick = [this] { proc.resetSliceMap(); };
    rechopBtn.onClick = [this] { proc.rechop (true); };
    shuffleBtn.setTooltip ("Randomly re-assign slices to pads");
    resetMapBtn.setTooltip ("Slice 1 on pad 1, slice 2 on pad 2...");
    rechopBtn.setTooltip ("Chop again with the current settings (discards manual marker edits)");
    sliceHint.setText ("Drag markers to move  |  Double-click to add  |  Right-click to delete  |  Click a slice to play it",
                       juce::dontSendNotification);
    sliceHint.setFont (ui::font (11.0f));
    sliceHint.setColour (juce::Label::textColourId, ui::col::dim);
    sliceHint.setJustificationType (juce::Justification::centredRight);
    for (auto* c : std::initializer_list<juce::Component*> { &sliceMode, &sliceCount, &sliceSens, &shuffleBtn, &resetMapBtn, &rechopBtn, &sliceHint })
        addAndMakeVisible (c);

    // Pads
    addAndMakeVisible (pads);
    pads.onSelectPad = [this] (int pad) { bindPad (pad); };
    for (auto* c : std::initializer_list<juce::Component*> { &padTune, &padLevel, &padPan, &padMode, &padRev })
        addAndMakeVisible (c);

    att.attach (s, "attack");
    dec.attach (s, "decay");
    sus.attach (s, "sustain");
    rel.attach (s, "release");
    for (auto* c : std::initializer_list<juce::Component*> { &att, &dec, &sus, &rel })
        addAndMakeVisible (c);

    attachButton (rndOn, "rndOn");
    rndPitch.attach (s, "rndPitch");
    rndPan.attach (s, "rndPan");
    rndLevel.attach (s, "rndLevel");
    rndRev.attach (s, "rndRev");
    rndSlice.attach (s, "rndSlice");
    for (auto* c : std::initializer_list<juce::Component*> { &rndPitch, &rndPan, &rndLevel, &rndRev, &rndSlice })
        addAndMakeVisible (c);

    // Performance
    for (auto* b : { &stutBtn, &halfBtn, &stopBtn })
    {
        b->setClickingTogglesState (true);
        b->getProperties().set ("perf", true);
    }
    attachButton (stutBtn, "stutOn");
    attachButton (halfBtn, "halfOn");
    attachButton (stopBtn, "stopOn");
    stutBtn.setTooltip ("Beat repeat, synced to the host tempo (MIDI note C0 = momentary)");
    halfBtn.setTooltip ("Half-speed playback of each window, an octave down (MIDI note C#0 = momentary)");
    stopBtn.setTooltip ("Tape/turntable stop, pitch winds down to zero (MIDI note D0 = momentary)");
    stutRate.attach (s, "stutRate");
    stutGate.attach (s, "stutGate");
    stutMix.attach (s, "stutMix");
    halfLen.attach (s, "halfLen");
    halfMix.attach (s, "halfMix");
    stopTime.attach (s, "stopTime");
    stopCurve.attach (s, "stopCurve");
    stopSpin.attach (s, "stopSpin");
    for (auto* c : std::initializer_list<juce::Component*> { &stutRate, &stutGate, &stutMix, &halfLen, &halfMix, &stopTime, &stopCurve, &stopSpin })
        addAndMakeVisible (c);

    // FX
    attachButton (dlyOn, "dlyOn");
    attachButton (revOn, "revOn");
    attachButton (widOn, "widOn");
    attachButton (widBass, "widBass");
    attachButton (vinOn, "vinOn");
    dlyTime.attach (s, "dlyTime");
    dlyFb.attach (s, "dlyFb");
    dlyWow.attach (s, "dlyWow");
    dlyTone.attach (s, "dlyTone");
    dlyAge.attach (s, "dlyAge");
    dlyMix.attach (s, "dlyMix");
    revSize.attach (s, "revSize");
    revDecay.attach (s, "revDecay");
    revPre.attach (s, "revPre");
    revTone.attach (s, "revTone");
    revWow.attach (s, "revWow");
    revMix.attach (s, "revMix");
    widWidth.attach (s, "widWidth");
    widHaas.attach (s, "widHaas");
    vinBits.attach (s, "vinBits");
    vinRate.attach (s, "vinRate");
    for (auto* c : std::initializer_list<juce::Component*> { &dlyTime, &dlyFb, &dlyWow, &dlyTone, &dlyAge, &dlyMix, &revSize, &revDecay,
                                                             &revPre, &revTone, &revWow, &revMix, &widWidth, &widHaas, &vinBits, &vinRate })
        addAndMakeVisible (c);

    // Choice/bipolar knob colours
    for (auto* k : { &sliceMode, &padMode, &stutRate, &halfLen, &dlyTime })
        k->slider.setColour (juce::Slider::rotarySliderFillColourId, ui::col::amber);

    for (auto* c : getChildren())
        if (dynamic_cast<juce::Button*> (c) != nullptr)
            c->setWantsKeyboardFocus (false);

    setWantsKeyboardFocus (true);
    setSize (editorW, editorH);

    bindPad (proc.getSelectedPad());
    proc.addChangeListener (this);
    refreshKit();
    startTimerHz (30);
}

ChopShopEditor::~ChopShopEditor()
{
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void ChopShopEditor::attachButton (juce::Button& b, const juce::String& id)
{
    buttonAtts.push_back (std::make_unique<APVTS::ButtonAttachment> (proc.apvts, id, b));
    addAndMakeVisible (b);
}

void ChopShopEditor::bindPad (int pad)
{
    auto& s = proc.apvts;
    padTune.attach (s, params::padId (pad, "tune"));
    padLevel.attach (s, params::padId (pad, "level"));
    padPan.attach (s, params::padId (pad, "pan"));
    padMode.attach (s, params::padId (pad, "mode"));
    padRevAtt.reset();
    padRevAtt = std::make_unique<APVTS::ButtonAttachment> (s, params::padId (pad, "rev"), padRev);
    wave.repaint();
    repaint();
}

void ChopShopEditor::refreshKit()
{
    auto k = proc.getKit();
    wave.setKit (k);
    pads.setKit (k);

    if (k != nullptr && k->main != nullptr)
    {
        const double secs = k->main->length() / k->main->sampleRate;
        info.setText (k->main->name + "     " + juce::String (secs, 2) + " s     " + juce::String (k->numSlices()) + " slices",
                      juce::dontSendNotification);
    }
    else
        info.setText ("No sample loaded", juce::dontSendNotification);
}

void ChopShopEditor::changeListenerCallback (juce::ChangeBroadcaster*) { refreshKit(); }

void ChopShopEditor::timerCallback()
{
    wave.refreshPlayhead();
    bpmLabel.setText (juce::String (proc.currentBpm.load(), 1) + " BPM", juce::dontSendNotification);

    juce::TextButton* perf[] { &stutBtn, &halfBtn, &stopBtn };
    for (int i = 0; i < 3; ++i)
    {
        const bool active = proc.perfActive[i].load();
        if ((bool) perf[i]->getProperties()["active"] != active)
        {
            perf[i]->getProperties().set ("active", active);
            perf[i]->repaint();
        }
    }
}

bool ChopShopEditor::keyStateChanged (bool)
{
    if (dynamic_cast<juce::TextEditor*> (getCurrentlyFocusedComponent()) != nullptr)
        return false;

    bool used = false;
    for (int pad = 0; pad < 16; ++pad)
    {
        const bool down = juce::KeyPress::isKeyCurrentlyDown (padKeys[pad]);
        if (down != keysDown[(size_t) pad])
        {
            keysDown[(size_t) pad] = down;
            if (down)
            {
                proc.setSelectedPad (pad);
                bindPad (pad);
                proc.padNoteOn (pad, 0.9f);
            }
            else
                proc.padNoteOff (pad);
            used = true;
        }
    }
    return used;
}

//==============================================================================
void ChopShopEditor::layoutKnobs (juce::Rectangle<int> area, std::initializer_list<juce::Component*> comps)
{
    const int w = area.getWidth() / (int) comps.size();
    for (auto* c : comps)
        c->setBounds (area.removeFromLeft (w).reduced (3, 0));
}

void ChopShopEditor::resized()
{
    panels.clear();
    auto addPanel = [this] (juce::Rectangle<int> r, const juce::String& title)
    {
        panels.push_back ({ r, title });
        return r.reduced (10).withTrimmedTop (20);
    };
    auto headerButton = [] (juce::Button& b, juce::Rectangle<int> panel, int w = 52)
    {
        b.setBounds (panel.getRight() - w - 10, panel.getY() + 8, w, 18);
    };

    // Top bar
    loadBtn.setBounds (ox, 14, 80, 30);
    demoBtn.setBounds (ox + 86, 14, 64, 30);
    info.setBounds (ox + 164, 14, 600, 30);
    bpmLabel.setBounds (rx + 150, 14, 110, 30);
    master.setBounds (editorW - 16 - 64, 2, 60, 58);

    // Library
    library.setBounds (addPanel ({ libX, top, libW, 700 }, "LIBRARY"));

    // Sample / slicer
    {
        auto r = juce::Rectangle<int> (ox, top, 764, 300);
        auto c = addPanel (r, "SAMPLE  /  SLICER");
        wave.setBounds (c.removeFromTop (170));
        c.removeFromTop (6);
        auto knobs = c.removeFromLeft (216);
        layoutKnobs (knobs, { &sliceMode, &sliceCount, &sliceSens });
        c.removeFromLeft (10);
        auto btns = c.withSizeKeepingCentre (c.getWidth(), 28).withY (c.getY() + 10);
        shuffleBtn.setBounds (btns.removeFromLeft (92));
        btns.removeFromLeft (6);
        resetMapBtn.setBounds (btns.removeFromLeft (100));
        btns.removeFromLeft (6);
        rechopBtn.setBounds (btns.removeFromLeft (92));
        sliceHint.setBounds (c.withTrimmedTop (44).withHeight (20));
    }

    // Pads
    {
        auto r = juce::Rectangle<int> (ox, 372, 392, 392);
        panels.push_back ({ r, {} });
        pads.setBounds (r.reduced (8));
    }
    {
        auto r = juce::Rectangle<int> (ox + 400, 372, 364, 124);
        auto c = addPanel (r, "@PAD");
        padRev.setBounds (c.getRight() - 82, c.getY() + 26, 82, 22);
        c.removeFromRight (88);
        layoutKnobs (c, { &padTune, &padLevel, &padPan, &padMode });
    }
    layoutKnobs (addPanel ({ ox + 400, 504, 364, 124 }, "ENVELOPE"), { &att, &dec, &sus, &rel });
    {
        auto r = juce::Rectangle<int> (ox + 400, 636, 364, 128);
        headerButton (rndOn, r);
        layoutKnobs (addPanel (r, "RANDOMIZER"), { &rndPitch, &rndPan, &rndLevel, &rndRev, &rndSlice });
    }

    // Performance
    {
        auto c = addPanel ({ rx, top, 388, 300 }, "PERFORMANCE");
        const int rowH = c.getHeight() / 3;
        struct Row { juce::Button* btn; std::initializer_list<juce::Component*> knobs; };
        const Row rows[] { { &stutBtn, { &stutRate, &stutGate, &stutMix } },
                           { &halfBtn, { &halfLen, &halfMix } },
                           { &stopBtn, { &stopTime, &stopCurve, &stopSpin } } };
        for (auto& row : rows)
        {
            auto rr = c.removeFromTop (rowH);
            row.btn->setBounds (rr.removeFromLeft (122).withSizeKeepingCentre (116, 58));
            rr.removeFromLeft (8);
            auto knobArea = rr.withTrimmedTop (4);
            const int w = knobArea.getWidth() / 3;
            for (auto* k : row.knobs)
                k->setBounds (knobArea.removeFromLeft (w).reduced (4, 0));
        }
    }

    // FX
    {
        auto r = juce::Rectangle<int> (rx, 372, 388, 124);
        headerButton (dlyOn, r);
        layoutKnobs (addPanel (r, "TAPE DELAY"), { &dlyTime, &dlyFb, &dlyWow, &dlyTone, &dlyAge, &dlyMix });
    }
    {
        auto r = juce::Rectangle<int> (rx, 504, 388, 124);
        headerButton (revOn, r);
        layoutKnobs (addPanel (r, "TAPE REVERB"), { &revSize, &revDecay, &revPre, &revTone, &revWow, &revMix });
    }
    {
        auto r = juce::Rectangle<int> (rx, 636, 190, 128);
        headerButton (widOn, r);
        auto c = addPanel (r, "WIDENER");
        widBass.setBounds (c.removeFromBottom (20).withSizeKeepingCentre (96, 20));
        layoutKnobs (c, { &widWidth, &widHaas });
    }
    {
        auto r = juce::Rectangle<int> (rx + 198, 636, 190, 128);
        headerButton (vinOn, r);
        layoutKnobs (addPanel (r, "VINTAGE"), { &vinBits, &vinRate });
    }
}

void ChopShopEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::col::bg);
    juce::ColourGradient chassis (ui::col::chassis.brighter (0.05f), 0.0f, 0.0f, ui::col::bg, 0.0f, (float) getHeight(), false);
    g.setGradientFill (chassis);
    g.fillRect (getLocalBounds());

    // Top bar
    auto bar = getLocalBounds().removeFromTop (56).toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillRect (bar);
    g.setColour (ui::col::accent);
    g.fillRect (bar.removeFromBottom (2.0f).withWidth ((float) getWidth()));

    g.setFont (ui::font (26.0f, true));
    g.setColour (ui::col::text);
    g.drawText ("CHOPSHOP", libX, 8, 160, 30, juce::Justification::centredLeft);
    g.setColour (ui::col::accent);
    g.setFont (ui::font (13.0f, true));
    g.drawText ("SP-16", libX + 160, 8, 60, 30, juce::Justification::centredLeft);
    g.setColour (ui::col::dim);
    g.setFont (ui::font (9.5f, true));
    g.drawText ("SAMPLING MACHINE", libX, 34, 200, 14, juce::Justification::centredLeft);

    // Screen-style box behind the sample name and tempo
    g.setColour (ui::col::screen);
    g.fillRoundedRectangle (juce::Rectangle<float> ((float) ox + 158.0f, 12.0f, 612.0f, 34.0f), 5.0f);
    g.fillRoundedRectangle (juce::Rectangle<float> ((float) rx + 146.0f, 12.0f, 118.0f, 34.0f), 5.0f);
    g.setColour (ui::col::dim);
    g.setFont (ui::font (9.5f, true));
    g.drawText ("HOST TEMPO", rx + 40, 14, 100, 30, juce::Justification::centredRight);

    for (auto& p : panels)
    {
        auto r = p.bounds.toFloat();
        g.setColour (ui::col::panel);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (ui::col::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        if (p.title.isNotEmpty())
        {
            const auto title = p.title == "@PAD" ? "PAD " + juce::String (proc.getSelectedPad() + 1) : p.title;
            g.setColour (ui::col::accent);
            g.fillRoundedRectangle (r.getX() + 10.0f, r.getY() + 12.0f, 3.0f, 11.0f, 1.5f);
            g.setColour (ui::col::text);
            g.setFont (ui::font (11.5f, true));
            g.drawText (title, (int) r.getX() + 18, (int) r.getY() + 8, 220, 18, juce::Justification::centredLeft);
        }
    }
}
