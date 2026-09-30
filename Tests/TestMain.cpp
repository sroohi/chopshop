#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
    if (! ok) ++failures;
}

void setParam (ChopShopProcessor& p, const juce::String& id, float value)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

struct Section { juce::String name; double from, to; };
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? argv[1] : juce::File::getCurrentWorkingDirectory().getFullPathName());
    const double sr = 48000.0;
    const int block = 512;

    std::cout << "== Render test" << std::endl;
    ChopShopProcessor proc;
    proc.prepareToPlay (sr, block);

    auto kit = proc.getKit();
    check (kit != nullptr && kit->main != nullptr, "demo break loaded");
    check (kit->numSlices() >= 8, "transient slicer found " + juce::String (kit->numSlices()) + " slices in demo break");

    // Timeline (seconds): pads play a pattern throughout; effects switch on section by section.
    const std::vector<Section> sections {
        { "dry pads", 0.0, 2.6 }, { "stutter", 2.6, 4.6 }, { "half-time", 4.6, 7.2 },
        { "tape stop", 7.2, 9.2 }, { "delay+reverb", 9.2, 12.0 }, { "widener+vintage", 12.0, 14.0 },
        { "randomizer+reverse", 14.0, 16.0 } };
    const double total = 16.0;
    const int totalSamples = (int) (total * sr);

    juce::AudioBuffer<float> out (2, totalSamples);
    juce::AudioBuffer<float> buf (2, block);
    const double spb = 60.0 / 120.0 * sr; // no host: 120 BPM
    const int pattern[] { 0, 2, 1, 2, 0, 3, 1, 4 };
    int step = 0;
    double nextHit = 0.0;
    std::vector<double> blockMs;

    for (int pos = 0; pos < totalSamples; pos += block)
    {
        const double t = pos / sr;
        auto in = [&] (const Section& s) { return t >= s.from && t < s.to; };
        setParam (proc, "stutOn", in (sections[1]) && t > 3.1 ? 1.0f : 0.0f);
        setParam (proc, "halfOn", in (sections[2]) ? 1.0f : 0.0f);
        setParam (proc, "stopOn", t >= 7.4 && t < 8.6 ? 1.0f : 0.0f);
        setParam (proc, "dlyOn", t >= 9.2 && t < 14.0 ? 1.0f : 0.0f);
        setParam (proc, "revOn", t >= 9.2 ? 1.0f : 0.0f);
        setParam (proc, "widOn", t >= 12.0 ? 1.0f : 0.0f);
        setParam (proc, "widHaas", 8.0f);
        setParam (proc, "vinOn", t >= 12.0 && t < 14.0 ? 1.0f : 0.0f);
        setParam (proc, "vinBits", 8.0f);
        setParam (proc, "rndOn", t >= 14.0 ? 1.0f : 0.0f);
        setParam (proc, "p1_rev", t >= 14.0 ? 1.0f : 0.0f);

        juce::MidiBuffer midi;
        const int n = juce::jmin (block, totalSamples - pos);
        while (nextHit < pos + n)
        {
            const int at = juce::jmax (0, (int) nextHit - pos);
            const int pad = pattern[step % 8];
            midi.addEvent (juce::MidiMessage::noteOn (1, 36 + pad, (juce::uint8) 110), at);
            midi.addEvent (juce::MidiMessage::noteOff (1, 36 + pad), juce::jmin (n - 1, at + 200));
            ++step;
            nextHit += spb * 0.5;
        }
        // Chromatic note on the selected pad, and a momentary stutter via MIDI note C0.
        if (pos == (int) (1.0 * sr) / block * block) midi.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0);
        if (pos == (int) (2.0 * sr) / block * block) midi.addEvent (juce::MidiMessage::noteOn (1, 24, (juce::uint8) 100), 0);
        if (pos == (int) (2.4 * sr) / block * block) midi.addEvent (juce::MidiMessage::noteOff (1, 24), 0);

        buf.setSize (2, n, false, false, true);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        proc.processBlock (buf, midi);
        blockMs.push_back (juce::Time::getMillisecondCounterHiRes() - t0);
        for (int c = 0; c < 2; ++c)
            out.copyFrom (c, pos, buf, c, 0, n);
    }

    bool finite = true;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < totalSamples; ++i)
            if (! std::isfinite (out.getSample (c, i))) finite = false;
    check (finite, "output contains no NaN/Inf");

    const float peak = out.getMagnitude (0, totalSamples);
    check (peak > 0.05f && peak < 4.0f, "overall peak is sane: " + juce::String (peak, 3));

    for (auto& s : sections)
    {
        const int a = (int) (s.from * sr), len = (int) ((s.to - s.from) * sr);
        const float rms = 0.5f * (out.getRMSLevel (0, a, len) + out.getRMSLevel (1, a, len));
        const float pk = out.getMagnitude (a, len);
        check (rms > 0.005f && pk < 4.0f, juce::String (s.name).paddedRight (' ', 20) + " rms " + juce::String (rms, 3) + "  peak " + juce::String (pk, 3));
    }

    // Tape stop: fully stopped section should go (nearly) silent before release at 8.6 s.
    {
        const int a = (int) (8.25 * sr), len = (int) (0.3 * sr);
        check (out.getRMSLevel (0, a, len) < 0.01f, "tape stop reaches silence (rms " + juce::String (out.getRMSLevel (0, a, len), 4) + ")");
    }
    // Widener with Haas delay should decorrelate channels.
    {
        const int a = (int) (12.2 * sr), len = (int) (1.5 * sr);
        double diff = 0.0, sum = 0.0;
        for (int i = a; i < a + len; ++i)
        {
            diff += std::abs (out.getSample (0, i) - out.getSample (1, i));
            sum += std::abs (out.getSample (0, i)) + std::abs (out.getSample (1, i));
        }
        check (diff / sum > 0.1, "widener produces stereo difference (" + juce::String (diff / sum, 3) + ")");
    }

    std::sort (blockMs.begin(), blockMs.end());
    const double budget = block / sr * 1000.0;
    const double p99 = blockMs[(size_t) (blockMs.size() * 0.99)];
    check (p99 < budget * 0.25, "CPU: p99 block " + juce::String (p99, 3) + " ms of " + juce::String (budget, 1) + " ms budget");

    {
        auto wavFile = outDir.getChildFile ("chopshop_render.wav");
        wavFile.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (wavFile.createOutputStream());
        auto options = juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24);
        if (auto writer = wav.createWriterFor (os, options))
            writer->writeFromAudioSampleBuffer (out, 0, totalSamples);
        std::cout << "  wrote " << wavFile.getFullPathName() << std::endl;
    }

    std::cout << "== Master limiter" << std::endl;
    {
        ChopShopProcessor lp;
        lp.prepareToPlay (sr, block);
        check (lp.getLatencySamples() == (int) std::round (sr * 0.0015), "limiter latency reported to host: " + juce::String (lp.getLatencySamples()) + " samples");
        setParam (lp, "master", 12.0f);
        setParam (lp, "limOn", 1.0f);
        setParam (lp, "limCeil", -1.0f);
        setParam (lp, "drvOn", 1.0f);
        setParam (lp, "drvAmt", 0.8f);
        setParam (lp, "toneOn", 1.0f);
        setParam (lp, "toneTilt", 0.6f);
        setParam (lp, "toneLow", 80.0f);
        float peak2 = 0.0f, gr = 0.0f;
        bool finite2 = true;
        for (int b = 0; b < (int) (3.0 * sr / block); ++b)
        {
            juce::AudioBuffer<float> x (2, block);
            juce::MidiBuffer m;
            if (b % 20 == 0)
                for (int pad = 0; pad < 4; ++pad)
                    m.addEvent (juce::MidiMessage::noteOn (1, 36 + pad, (juce::uint8) 127), 0);
            lp.processBlock (x, m);
            peak2 = juce::jmax (peak2, x.getMagnitude (0, block));
            gr = juce::jmax (gr, lp.gainReduction.load());
            for (int i = 0; i < block; ++i)
                finite2 &= std::isfinite (x.getSample (0, i)) && std::isfinite (x.getSample (1, i));
        }
        check (finite2, "saturation + tone + limiter output is finite");
        check (peak2 <= juce::Decibels::decibelsToGain (-1.0f) + 1.0e-4f, "limiter holds -1 dB ceiling with +12 dB master (peak " + juce::String (juce::Decibels::gainToDecibels (peak2), 2) + " dBFS)");
        check (gr > 3.0f, "limiter is working (max GR " + juce::String (gr, 1) + " dB)");
    }

    std::cout << "== Time-stretch" << std::endl;
    {
        auto lengthOfHit = [&] (bool stretchOn, float ratio, float tune)
        {
            ChopShopProcessor sp;
            sp.prepareToPlay (sr, block);
            setParam (sp, "limOn", 0.0f);
            setParam (sp, "strOn", stretchOn ? 1.0f : 0.0f);
            setParam (sp, "strMode", 0.0f);
            setParam (sp, "strRatio", ratio);
            setParam (sp, "p1_tune", tune);
            setParam (sp, "sliceMode", 0.0f);
            setParam (sp, "sliceCount", 4.0f);
            sp.rechop (true);
            juce::AudioBuffer<float> all (2, (int) (6.0 * sr));
            all.clear();
            for (int pos = 0; pos + block <= all.getNumSamples(); pos += block)
            {
                juce::AudioBuffer<float> x (2, block);
                juce::MidiBuffer m;
                if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 127), 0);
                sp.processBlock (x, m);
                all.copyFrom (0, pos, x, 0, 0, block);
            }
            int last = 0;
            for (int i = 0; i < all.getNumSamples(); ++i)
                if (std::abs (all.getSample (0, i)) > 1.0e-4f) last = i;
            return last / sr;
        };
        const double plain = lengthOfHit (false, 1.0f, 0.0f);
        const double slow = lengthOfHit (true, 2.0f, 0.0f);
        const double pitchedUp = lengthOfHit (true, 1.0f, 12.0f);
        check (std::abs (slow / plain - 2.0) < 0.1, "stretch x2 doubles slice length (" + juce::String (plain, 3) + " s -> " + juce::String (slow, 3) + " s)");
        check (std::abs (pitchedUp / plain - 1.0) < 0.1, "stretch keeps length when tuned +12 st (" + juce::String (pitchedUp, 3) + " s)");
    }

    std::cout << "== Slicer & kit editing" << std::endl;
    {
        setParam (proc, "sliceMode", 0.0f);
        setParam (proc, "sliceCount", 8.0f);
        proc.rechop (false);
        check (proc.getKit()->numSlices() == 8, "equal mode gives 8 slices");
        auto sl = proc.getKit()->slices;
        sl.push_back (sl.back() + 3000);
        proc.setSlices (sl);
        check (proc.getKit()->numSlices() == 9, "manual marker added");
        proc.shuffleSlices();
        auto k = proc.getKit();
        std::vector<int> used (k->padSlice.begin(), k->padSlice.begin() + 9);
        std::sort (used.begin(), used.end());
        bool perm = true;
        for (int i = 0; i < 9; ++i) perm &= used[(size_t) i] == i;
        check (perm, "shuffle is a permutation of the slices");
    }

    std::cout << "== Library" << std::endl;
    juce::File librarySample;
    const auto lib = LibraryPanel::libraryFolder();
    const auto files = lib.findChildFiles (juce::File::findFiles, true, "*.wav");
    check (files.size() > 0, juce::String (files.size()) + " samples in " + lib.getFullPathName());
    if (! files.isEmpty()) // the rest needs at least one sample
    {
        for (auto& f : files)
            if (f.getFileName().contains ("Kick-Snare")) librarySample = f;
        if (librarySample.existsAsFile())
        {
            check (proc.loadMainSample (librarySample), "load Splice loop as main sample: " + librarySample.getFileName());
            setParam (proc, "sliceMode", 1.0f);
            setParam (proc, "sliceCount", 16.0f);
            proc.rechop (false);
            check (proc.getKit()->numSlices() >= 8, "transient chop of Splice loop: " + juce::String (proc.getKit()->numSlices()) + " slices");
        }
        for (auto& f : files)
            if (f.getFileName().contains ("kick"))
                check (proc.loadPadSample (15, f), "load one-shot onto pad 16: " + f.getFileName());

        proc.previewFile (files[0]);
        juce::AudioBuffer<float> b (2, block);
        juce::MidiBuffer m;
        proc.processBlock (b, m);
        check (b.getMagnitude (0, block) > 0.0f || proc.previewPlaying.load(), "library preview plays");
        proc.stopPreview();
    }

    std::cout << "== State round-trip" << std::endl;
    {
        setParam (proc, "p5_tune", 7.0f);
        juce::MemoryBlock state;
        proc.getStateInformation (state);
        const auto slicesBefore = proc.getKit()->slices;
        const auto mapBefore = proc.getKit()->padSlice;

        ChopShopProcessor other;
        other.setStateInformation (state.getData(), (int) state.getSize());
        auto k2 = other.getKit();
        check (std::abs (other.apvts.getRawParameterValue ("p5_tune")->load() - 7.0f) < 0.01f, "parameter restored");
        check (k2->main != nullptr && k2->main->path == proc.getKit()->main->path, "main sample path restored");
        check (k2->slices == slicesBefore, "slice points restored");
        check (k2->padSlice == mapBefore, "pad map restored");
        check (k2->padSample[15] != nullptr, "pad sample restored");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200); // let the async re-chop check run
        check (other.getKit()->slices == slicesBefore, "restoring did not trigger a re-chop");
    }

    std::cout << "== Editor snapshot" << std::endl;
    {
        proc.loadDemo();
        setParam (proc, "halfOn", 0.0f);
        setParam (proc, "stopOn", 0.0f);
        setParam (proc, "stutOn", 1.0f);
        setParam (proc, "revOn", 1.0f);
        setParam (proc, "drvOn", 1.0f);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setSize (MainView::W, MainView::H);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1500); // library scan + kit refresh
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto png = outDir.getChildFile ("chopshop_ui.png");
        png.deleteFile();
        juce::FileOutputStream fos (png);
        juce::PNGImageFormat().writeImageToStream (img, fos);
        check (img.getWidth() == MainView::W, "editor rendered " + juce::String (img.getWidth()) + "x" + juce::String (img.getHeight()));
        std::cout << "  wrote " << png.getFullPathName() << std::endl;
    }

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : juce::String (failures) + " FAILURE(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}
