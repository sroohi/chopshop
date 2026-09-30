#include "PluginEditor.h"

namespace
{
constexpr int libX = 16, libW = 236;
constexpr int colX = libX + libW + 16;   // monitor + pads column
constexpr int colW = 560;
constexpr int boxX = colX + colW + 16;   // module boxes
constexpr int boxW = 312, boxH = 386, gap = 12;
constexpr int top = 80;

const juce::Rectangle<int> monitorBounds { colX, top, colW, 320 };

// Computer-keyboard pad layout, bottom row to top row (like the MPC software).
const char padKeys[16] { 'Z', 'X', 'C', 'V', 'A', 'S', 'D', 'F', 'Q', 'W', 'E', 'R', '1', '2', '3', '4' };
} // namespace

MainView::MainView (ChopShopProcessor& p)
    : proc (p), library (p), wave (p), pads (p)
{
    auto& s = proc.apvts;
    auto show = [this] (std::initializer_list<juce::Component*> comps)
    {
        for (auto* c : comps)
            addAndMakeVisible (c);
    };

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
    attachButton (limOn, "limOn");
    limOn.getProperties().set ("wide", true);
    limOn.setTooltip ("Lookahead brickwall limiter on the master output (1.5 ms latency)");
    limCeil.attach (s, "limCeil");
    master.attach (s, "master");
    master.slider.setColour (juce::Slider::rotarySliderFillColourId, ui::col::amber);
    show ({ &loadBtn, &demoBtn, &limCeil, &master, &library });

    // Monitor
    wave.onSelectPad = [this] (int pad) { bindPad (pad); };
    att.attach (s, "attack");
    dec.attach (s, "decay");
    sus.attach (s, "sustain");
    rel.attach (s, "release");
    show ({ &wave, &padTune, &padLevel, &padPan, &padMode, &padRev, &att, &dec, &sus, &rel });

    addAndMakeVisible (pads);
    pads.onSelectPad = [this] (int pad) { bindPad (pad); };

    // MAIN
    attachButton (halfOn, "halfOn");
    attachButton (strOn, "strOn");
    halfLen.attach (s, "halfLen");
    halfMix.attach (s, "halfMix");
    strMode.attach (s, "strMode");
    strRatio.attach (s, "strRatio");
    strBpm.attach (s, "strBpm");
    strGrain.attach (s, "strGrain");
    sliceMode.attach (s, "sliceMode");
    sliceCount.attach (s, "sliceCount");
    sliceSens.attach (s, "sliceSens");
    shuffleBtn.onClick = [this] { proc.shuffleSlices(); };
    resetMapBtn.onClick = [this] { proc.resetSliceMap(); };
    rechopBtn.onClick = [this] { proc.rechop (true); };
    halfOn.setTooltip ("Half-time: each window plays at half speed, an octave down (MIDI C#0 = momentary)");
    strOn.setTooltip ("Time-stretch the chopped sample without changing pitch. SYNC follows the host tempo from SRC BPM.");
    shuffleBtn.setTooltip ("Randomly re-assign slices to pads");
    resetMapBtn.setTooltip ("Slice 1 on pad 1, slice 2 on pad 2...");
    rechopBtn.setTooltip ("Chop again with the current settings (discards manual marker edits)");
    mainBox.addSection ("HALF-TIME", &halfOn, { &halfLen, &halfMix }, 2);
    mainBox.addSection ("STRETCH", &strOn, { &strMode, &strRatio, &strBpm, &strGrain }, 4);
    mainBox.addSection ("SLICER", nullptr, { &sliceMode, &sliceCount, &sliceSens, &shuffleBtn, &resetMapBtn, &rechopBtn }, 3);

    // FX
    attachButton (widOn, "widOn");
    attachButton (widBass, "widBass");
    attachButton (rndOn, "rndOn");
    attachButton (stutOn, "stutOn");
    attachButton (stopOn, "stopOn");
    widBass.getProperties().set ("wide", true);
    widWidth.attach (s, "widWidth");
    widHaas.attach (s, "widHaas");
    rndPitch.attach (s, "rndPitch");
    rndPan.attach (s, "rndPan");
    rndLevel.attach (s, "rndLevel");
    rndRev.attach (s, "rndRev");
    rndSlice.attach (s, "rndSlice");
    stutRate.attach (s, "stutRate");
    stutGate.attach (s, "stutGate");
    stutMix.attach (s, "stutMix");
    stopTime.attach (s, "stopTime");
    stopCurve.attach (s, "stopCurve");
    stopSpin.attach (s, "stopSpin");
    stutOn.setTooltip ("Beat repeat, synced to the host tempo (MIDI C0 = momentary)");
    stopOn.setTooltip ("Tape stop: pitch winds down to zero (MIDI D0 = momentary)");
    fxBox.addSection ("WIDENER", &widOn, { &widWidth, &widHaas, &widBass }, 3);
    fxBox.addSection ("RANDOMIZER", &rndOn, { &rndPitch, &rndPan, &rndLevel, &rndRev, &rndSlice }, 5);
    fxBox.addSection ("STUTTER", &stutOn, { &stutRate, &stutGate, &stutMix }, 3);
    fxBox.addSection ("TAPE STOP", &stopOn, { &stopTime, &stopCurve, &stopSpin }, 3);

    // EFFECT
    attachButton (dlyOn, "dlyOn");
    attachButton (revOn, "revOn");
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
    effectBox.addSection ("TAPE DELAY", &dlyOn, { &dlyTime, &dlyFb, &dlyWow, &dlyTone, &dlyAge, &dlyMix }, 3);
    effectBox.addSection ("TAPE REVERB", &revOn, { &revSize, &revDecay, &revPre, &revTone, &revWow, &revMix }, 3);

    // DRIVE
    attachButton (drvOn, "drvOn");
    attachButton (toneOn, "toneOn");
    attachButton (vinOn, "vinOn");
    drvAmt.attach (s, "drvAmt");
    drvType.attach (s, "drvType");
    drvMix.attach (s, "drvMix");
    toneTilt.attach (s, "toneTilt");
    toneLow.attach (s, "toneLow");
    toneHigh.attach (s, "toneHigh");
    vinBits.attach (s, "vinBits");
    vinRate.attach (s, "vinRate");
    driveBox.addSection ("SATURATION", &drvOn, { &drvAmt, &drvType, &drvMix }, 3);
    driveBox.addSection ("TONE", &toneOn, { &toneTilt, &toneLow, &toneHigh }, 3);
    driveBox.addSection ("LO-FI", &vinOn, { &vinBits, &vinRate }, 2);

    show ({ &mainBox, &fxBox, &effectBox, &driveBox });

    for (auto* b : std::initializer_list<juce::Button*> { &loadBtn, &demoBtn, &shuffleBtn, &resetMapBtn, &rechopBtn, &padRev })
        b->setWantsKeyboardFocus (false);
    for (auto& a : buttonAtts)
        juce::ignoreUnused (a);

    setSize (W, H);
    bindPad (proc.getSelectedPad());
    proc.addChangeListener (this);
    refreshKit();
    startTimerHz (30);
}

MainView::~MainView()
{
    proc.removeChangeListener (this);
}

void MainView::attachButton (juce::Button& b, const juce::String& id)
{
    buttonAtts.push_back (std::make_unique<APVTS::ButtonAttachment> (proc.apvts, id, b));
    b.setWantsKeyboardFocus (false);
    addAndMakeVisible (b);
}

void MainView::bindPad (int pad)
{
    auto& s = proc.apvts;
    padTune.attach (s, params::padId (pad, "tune"));
    padLevel.attach (s, params::padId (pad, "level"));
    padPan.attach (s, params::padId (pad, "pan"));
    padMode.attach (s, params::padId (pad, "mode"));
    padRevAtt.reset();
    padRevAtt = std::make_unique<APVTS::ButtonAttachment> (s, params::padId (pad, "rev"), padRev);
    wave.repaint();
    repaint (monitorBounds);
}

void MainView::refreshKit()
{
    auto k = proc.getKit();
    wave.setKit (k);
    pads.setKit (k);
    repaint();
}

void MainView::changeListenerCallback (juce::ChangeBroadcaster*) { refreshKit(); }

void MainView::timerCallback()
{
    wave.refreshPlayhead();

    // Meters: fast attack, ~300 ms fall.
    auto fall = [] (float current, float target) { return target > current ? target : current * 0.86f; };
    meterL = fall (meterL, proc.outPeak[0].exchange (0.0f));
    meterR = fall (meterR, proc.outPeak[1].exchange (0.0f));
    meterGr = fall (meterGr, proc.gainReduction.load());
    repaint (0, 0, W, 68);
    repaint (monitorBounds.withHeight (32));
}

bool MainView::handleKeys()
{
    if (dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr)
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
void MainView::resized()
{
    // Top bar
    loadBtn.setBounds (colX, 18, 80, 32);
    demoBtn.setBounds (colX + 86, 18, 64, 32);
    master.setBounds (W - 16 - 66, 2, 66, 64);
    limCeil.setBounds (W - 16 - 66 - 104, 14, 96, 42);
    limOn.setBounds (W - 16 - 66 - 104 - 96, 14, 88, 20);

    library.setBounds (libX + 10, top + 30, libW - 20, H - top - 16 - 40);

    // Monitor: screen with header, waveform and the selected pad's settings.
    {
        auto screen = monitorBounds.reduced (10);
        screen.removeFromTop (24);
        wave.setBounds (screen.removeFromTop (160));
        screen.removeFromTop (8);
        auto row1 = screen.removeFromTop (44);
        auto row2 = screen.withTrimmedTop (6).withHeight (44);
        const int cw = row1.getWidth() / 5;
        for (auto* c : std::initializer_list<juce::Component*> { &padTune, &padLevel, &padPan, &padMode })
            c->setBounds (row1.removeFromLeft (cw).reduced (2, 0));
        padRev.setBounds (row1.reduced (4, 11));
        const int cw2 = row2.getWidth() / 4;
        for (auto* c : std::initializer_list<juce::Component*> { &att, &dec, &sus, &rel })
            c->setBounds (row2.removeFromLeft (cw2).reduced (2, 0));
    }

    pads.setBounds (juce::Rectangle<int> (colX, top + 320 + 12, colW, H - (top + 320 + 12) - 16).reduced (10));

    mainBox.setBounds (boxX, top, boxW, boxH);
    fxBox.setBounds (boxX + boxW + gap, top, boxW, boxH);
    effectBox.setBounds (boxX, top + boxH + gap, boxW, boxH);
    driveBox.setBounds (boxX + boxW + gap, top + boxH + gap, boxW, boxH);
}

void MainView::paintMeters (juce::Graphics& g)
{
    const int x = W - 16 - 66 - 104 - 96 - 150;
    g.setFont (ui::font (9.0f, true));
    auto meter = [&] (int y, const juce::String& label, float lin)
    {
        g.setColour (ui::col::dim);
        g.drawText (label, x, y, 30, 12, juce::Justification::centredLeft);
        auto r = juce::Rectangle<float> ((float) x + 30.0f, (float) y + 2.0f, 110.0f, 8.0f);
        g.setColour (ui::col::screen);
        g.fillRoundedRectangle (r, 2.0f);
        const float db = juce::Decibels::gainToDecibels (lin, -60.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        const auto c = db > -0.5f ? ui::col::accent : (db > -6.0f ? ui::col::amber : juce::Colour (0xff4cd07d));
        g.setColour (c);
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * frac), 2.0f);
    };
    meter (16, "OUT L", meterL);
    meter (30, "OUT R", meterR);

    // Gain reduction grows from the right, 0..12 dB.
    g.setColour (ui::col::dim);
    g.drawText ("GR", x, 44, 30, 12, juce::Justification::centredLeft);
    auto r = juce::Rectangle<float> ((float) x + 30.0f, 46.0f, 110.0f, 8.0f);
    g.setColour (ui::col::screen);
    g.fillRoundedRectangle (r, 2.0f);
    const float frac = juce::jlimit (0.0f, 1.0f, meterGr / 12.0f);
    g.setColour (ui::col::accent);
    g.fillRoundedRectangle (r.withLeft (r.getRight() - r.getWidth() * frac), 2.0f);
    g.setColour (ui::col::dim);
    g.drawText (juce::String (meterGr, 1) + " dB", (int) r.getRight() - 110, 44, 104, 12, juce::Justification::centredRight);
}

void MainView::paint (juce::Graphics& g)
{
    juce::ColourGradient chassis (ui::col::chassis.brighter (0.05f), 0.0f, 0.0f, ui::col::bg, 0.0f, (float) H, false);
    g.setGradientFill (chassis);
    g.fillAll();

    // Top bar
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillRect (0, 0, W, 68);
    g.setColour (ui::col::accent);
    g.fillRect (0, 66, W, 2);

    g.setFont (ui::font (26.0f, true));
    g.setColour (ui::col::text);
    g.drawText ("CHOPSHOP", libX, 12, 160, 30, juce::Justification::centredLeft);
    g.setColour (ui::col::accent);
    g.setFont (ui::font (13.0f, true));
    g.drawText ("SP-16", libX + 160, 12, 60, 30, juce::Justification::centredLeft);
    g.setColour (ui::col::dim);
    g.setFont (ui::font (9.5f, true));
    g.drawText ("SAMPLING MACHINE", libX, 38, 200, 14, juce::Justification::centredLeft);

    g.setColour (ui::col::screen);
    g.fillRoundedRectangle (juce::Rectangle<float> ((float) colX + 162.0f, 16.0f, 398.0f, 36.0f), 5.0f);
    g.fillRoundedRectangle (juce::Rectangle<float> ((float) boxX, 16.0f, 150.0f, 36.0f), 5.0f);
    g.setColour (ui::col::dim);
    g.setFont (ui::font (9.0f, true));
    g.drawText ("HOST", boxX + 10, 16, 40, 36, juce::Justification::centredLeft);
    g.setColour (ui::col::text);
    g.setFont (ui::font (15.0f, true));
    g.drawText (juce::String (proc.currentBpm.load(), 1) + " BPM", boxX + 40, 16, 104, 36, juce::Justification::centredRight);

    auto k = proc.getKit();
    g.setFont (ui::font (13.0f, true));
    g.setColour (ui::col::text);
    g.drawFittedText (k != nullptr && k->main != nullptr ? k->main->name : juce::String ("No sample loaded"),
                      colX + 174, 16, 380, 36, juce::Justification::centredLeft, 1, 0.8f);

    paintMeters (g);

    // Library panel frame
    auto lib = juce::Rectangle<float> ((float) libX, (float) top, (float) libW, (float) (H - top - 16));
    g.setColour (ui::col::panel);
    g.fillRoundedRectangle (lib, 9.0f);
    g.setColour (ui::col::panelEdge);
    g.drawRoundedRectangle (lib.reduced (0.5f), 9.0f, 1.0f);
    g.setColour (ui::col::accent);
    g.fillRoundedRectangle (lib.getX() + 12.0f, lib.getY() + 12.0f, 4.0f, 14.0f, 2.0f);
    g.setColour (ui::col::text);
    g.setFont (ui::font (14.0f, true));
    g.drawText ("LIBRARY", (int) lib.getX() + 24, (int) lib.getY() + 8, 150, 22, juce::Justification::centredLeft);

    // Monitor bezel + screen
    auto mon = monitorBounds.toFloat();
    g.setColour (juce::Colour (0xff0e0f11));
    g.fillRoundedRectangle (mon, 10.0f);
    g.setColour (ui::col::panelEdge);
    g.drawRoundedRectangle (mon.reduced (0.5f), 10.0f, 1.2f);
    g.setColour (ui::col::screen);
    g.fillRoundedRectangle (mon.reduced (6.0f), 7.0f);

    auto header = monitorBounds.reduced (14, 10).removeFromTop (22);
    const int sel = proc.getSelectedPad();
    g.setColour (ui::col::amber);
    g.setFont (ui::font (12.0f, true));
    g.drawText ("PAD " + juce::String (sel + 1), header.removeFromLeft (64), juce::Justification::centredLeft);
    g.setColour (ui::col::dim);
    g.setFont (ui::font (11.0f, true));
    if (k != nullptr && k->main != nullptr)
    {
        juce::String what;
        if (auto& ps = k->padSample[(size_t) sel])
            what = "PAD SAMPLE: " + ps->name;
        else if (k->padSlice[(size_t) sel] >= 0)
            what = "SLICE " + juce::String (k->padSlice[(size_t) sel] + 1) + " of " + juce::String (k->numSlices());
        const auto secs = k->main->length() / k->main->sampleRate;
        g.drawText (what, header.removeFromLeft (240), juce::Justification::centredLeft);
        g.drawText (juce::String (secs, 2) + " s   SRC " + juce::String (proc.apvts.getRawParameterValue ("strBpm")->load(), 1) + " BPM",
                    header, juce::Justification::centredRight);
    }

    // Pads frame
    auto padFrame = juce::Rectangle<float> ((float) colX, (float) (top + 332), (float) colW, (float) (H - (top + 332) - 16));
    g.setColour (ui::col::panel);
    g.fillRoundedRectangle (padFrame, 9.0f);
    g.setColour (ui::col::panelEdge);
    g.drawRoundedRectangle (padFrame.reduced (0.5f), 9.0f, 1.0f);
}

//==============================================================================
ChopShopEditor::ChopShopEditor (ChopShopProcessor& p) : AudioProcessorEditor (&p), view (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (view);
    setWantsKeyboardFocus (true);

    // Start at a size that fits the screen; the window can then be resized freely.
    float scale = 1.0f;
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = display->userArea;
        scale = juce::jmin (1.0f, (float) (area.getWidth() - 40) / (float) MainView::W,
                            (float) (area.getHeight() - 120) / (float) MainView::H);
    }
    setResizable (true, true);
    setResizeLimits (MainView::W / 2, MainView::H / 2, MainView::W * 2, MainView::H * 2);
    getConstrainer()->setFixedAspectRatio ((double) MainView::W / (double) MainView::H);
    setSize (juce::roundToInt (MainView::W * scale), juce::roundToInt (MainView::H * scale));
}

ChopShopEditor::~ChopShopEditor()
{
    setLookAndFeel (nullptr);
}

void ChopShopEditor::resized()
{
    const float scale = (float) getWidth() / (float) MainView::W;
    view.setTransform (juce::AffineTransform::scale (scale));
}
