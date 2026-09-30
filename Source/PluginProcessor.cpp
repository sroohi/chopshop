#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Slicer.h"
#include "Demo.h"

ChopShopProcessor::ChopShopProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", params::createLayout())
{
    formatManager.registerBasicFormats();

    for (auto& g : padGlow) g = 0.0f;
    for (auto& a : perfActive) a = false;

    for (int i = 0; i < numPads; ++i)
    {
        auto& pp = padParams[i];
        pp.tune = apvts.getRawParameterValue (params::padId (i, "tune"));
        pp.level = apvts.getRawParameterValue (params::padId (i, "level"));
        pp.pan = apvts.getRawParameterValue (params::padId (i, "pan"));
        pp.rev = apvts.getRawParameterValue (params::padId (i, "rev"));
        pp.mode = apvts.getRawParameterValue (params::padId (i, "mode"));
    }

    for (auto* id : { "sliceMode", "sliceCount", "sliceSens" })
        apvts.addParameterListener (id, this);

    loadDemo();
    startTimer (1000);
}

ChopShopProcessor::~ChopShopProcessor()
{
    for (auto* id : { "sliceMode", "sliceCount", "sliceSens" })
        apvts.removeParameterListener (id, this);
    stopTimer();
    cancelPendingUpdate();
    for (auto& v : voices)
        v.kit = nullptr;
}

//==============================================================================
void ChopShopProcessor::prepareToPlay (double sampleRate, int)
{
    hostRate = sampleRate;
    uiMidi.reset (sampleRate);

    for (auto& v : voices)
    {
        v.active = false;
        v.kit = nullptr;
        v.env.setSampleRate (sampleRate);
    }

    vintage.prepare (sampleRate);
    stutter.prepare (sampleRate);
    halfTime.prepare (sampleRate);
    tapeDelay.prepare (sampleRate);
    tapeReverb.prepare (sampleRate);
    tapeStop.prepare (sampleRate);
    widener.prepare (sampleRate);

    masterGain.reset (sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p ("master")->load()));
    prepared = true;
}

bool ChopShopProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ChopShopProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (buffer.getNumChannels() < 2 || n == 0)
        return;

    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                bpm = juce::jlimit (20.0, 400.0, *b);
    currentBpm = bpm;

    uiBuffer.clear();
    uiMidi.removeNextBlockOfMessages (uiBuffer, n);
    midi.addEvents (uiBuffer, 0, n, 0);

    Kit::Ptr k;
    SampleData::Ptr pv;
    {
        const juce::SpinLock::ScopedLockType sl (kitLock);
        k = kit;
        pv = preview;
    }

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    int cursor = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > cursor)
        {
            renderVoices (L, R, cursor, at - cursor);
            cursor = at;
        }
        handleMidi (meta.getMessage(), *k);
    }
    if (cursor < n)
        renderVoices (L, R, cursor, n - cursor);

    // Report the playhead of the most recent voice playing the main sample.
    {
        int ph = -1;
        juce::uint64 newest = 0;
        for (auto& v : voices)
            if (v.active && v.sample == k->main.get() && v.age >= newest)
            {
                newest = v.age;
                ph = (int) v.pos;
            }
        playheadPos = ph;
    }

    //== Effects chain ========================================================
    const double spb = 60.0 / bpm * hostRate;
    auto on = [this] (const char* id) { return p (id)->load() > 0.5f; };
    auto val = [this] (const char* id) { return p (id)->load(); };
    auto idx = [this] (const char* id, int max) { return juce::jlimit (0, max, (int) p (id)->load()); };

    const bool stutOn = on ("stutOn") || momentary[0];
    const bool halfOn = on ("halfOn") || momentary[1];
    const bool stopOn = on ("stopOn") || momentary[2];
    perfActive[0] = stutOn;
    perfActive[1] = halfOn;
    perfActive[2] = stopOn;

    vintage.process (L, R, n, on ("vinOn"), val ("vinBits"), val ("vinRate"));
    stutter.process (L, R, n, stutOn, params::stutterBeats[idx ("stutRate", 5)] * spb, val ("stutGate"), val ("stutMix"));
    halfTime.process (L, R, n, halfOn, params::halfBeats[idx ("halfLen", 3)] * spb, val ("halfMix"));
    tapeDelay.process (L, R, n, on ("dlyOn"), params::delayBeats[idx ("dlyTime", 7)] * spb, val ("dlyFb"),
                       val ("dlyWow"), val ("dlyTone"), val ("dlyAge"), val ("dlyMix"));
    tapeReverb.process (L, R, n, on ("revOn"), val ("revSize"), val ("revDecay"), val ("revPre"), val ("revTone"),
                        val ("revWow"), val ("revMix"));
    tapeStop.process (L, R, n, stopOn, val ("stopTime"), val ("stopCurve"), val ("stopSpin"));
    widener.process (L, R, n, on ("widOn"), val ("widWidth"), val ("widHaas"), on ("widBass"));

    masterGain.setTargetValue (juce::Decibels::decibelsToGain (val ("master")));
    for (int i = 0; i < n; ++i)
    {
        const float g = masterGain.getNextValue();
        L[i] *= g;
        R[i] *= g;
    }

    renderPreview (L, R, n, pv.get());
}

void ChopShopProcessor::renderPreview (float* L, float* R, int n, SampleData* s)
{
    if (previewRestart.exchange (false))
        previewPos = 0.0;
    if (s == nullptr || previewPos >= s->length() - 1)
    {
        previewPlaying = false;
        return;
    }
    previewPlaying = true;

    // Library audition: dry, at the sample's original pitch, bypassing the pad FX chain.
    const float* sL = s->buffer.getReadPointer (0);
    const float* sR = s->buffer.getReadPointer (1);
    const double inc = s->sampleRate / hostRate;
    const int last = s->length() - 1;
    for (int i = 0; i < n && previewPos < last; ++i)
    {
        const int ip = (int) previewPos;
        const float f = (float) (previewPos - ip);
        const float fadeOut = juce::jmin (1.0f, (float) (last - previewPos) / 256.0f);
        L[i] += (sL[ip] + f * (sL[ip + 1] - sL[ip])) * 0.8f * fadeOut;
        R[i] += (sR[ip] + f * (sR[ip + 1] - sR[ip])) * 0.8f * fadeOut;
        previewPos += inc;
    }
}

void ChopShopProcessor::previewFile (const juce::File& file)
{
    auto s = readAudioFile (file, 60.0);
    if (s == nullptr)
        return;
    {
        const juce::SpinLock::ScopedLockType sl (kitLock);
        std::swap (preview, s);
    }
    previewRestart = true;
    previewPlaying = true;
    if (s != nullptr)
    {
        const juce::ScopedLock g (garbageLock);
        sampleGarbage.add (s.get());
    }
}

void ChopShopProcessor::stopPreview()
{
    SampleData::Ptr old;
    {
        const juce::SpinLock::ScopedLockType sl (kitLock);
        std::swap (preview, old);
    }
    previewPlaying = false;
    if (old != nullptr)
    {
        const juce::ScopedLock g (garbageLock);
        sampleGarbage.add (old.get());
    }
}

//==============================================================================
void ChopShopProcessor::handleMidi (const juce::MidiMessage& m, const Kit& k)
{
    if (m.isNoteOn())
    {
        const int note = m.getNoteNumber();
        if (note >= perfNoteBase && note < perfNoteBase + 3)
            momentary[note - perfNoteBase] = true;
        else if (note >= padNoteBase && note < padNoteBase + numPads)
            triggerPad (note - padNoteBase, m.getFloatVelocity(), note, 0.0f, k);
        else
            triggerPad (selectedPad.load(), m.getFloatVelocity(), note, (float) (note - chromaticRoot), k);
    }
    else if (m.isNoteOff())
    {
        const int note = m.getNoteNumber();
        if (note >= perfNoteBase && note < perfNoteBase + 3)
            momentary[note - perfNoteBase] = false;
        for (auto& v : voices)
            if (v.active && v.note == note && v.gate)
                v.env.noteOff();
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        for (auto& v : voices)
            v.active = false;
        for (auto& mo : momentary)
            mo = false;
    }
}

void ChopShopProcessor::triggerPad (int pad, float velocity, int note, float extraSemis, const Kit& k)
{
    const bool rnd = p ("rndOn")->load() > 0.5f;

    if (rnd && random.nextFloat() < p ("rndSlice")->load())
    {
        int candidates[numPads], count = 0;
        for (int i = 0; i < numPads; ++i)
            if (k.region (i).valid())
                candidates[count++] = i;
        if (count > 0)
            pad = candidates[random.nextInt (count)];
    }

    const auto reg = k.region (pad);
    if (! reg.valid())
        return;

    Voice* v = nullptr;
    for (auto& cand : voices)
        if (! cand.active) { v = &cand; break; }
    if (v == nullptr)
    {
        v = &voices[0];
        for (auto& cand : voices)
            if (cand.age < v->age) v = &cand;
    }

    const auto& pp = padParams[pad];
    float tune = pp.tune->load() + extraSemis;
    float pan = pp.pan->load();
    float level = juce::Decibels::decibelsToGain (pp.level->load(), -47.9f);
    bool reverse = pp.rev->load() > 0.5f;

    if (rnd)
    {
        tune += std::round ((random.nextFloat() * 2.0f - 1.0f) * p ("rndPitch")->load());
        pan = juce::jlimit (-1.0f, 1.0f, pan + (random.nextFloat() * 2.0f - 1.0f) * p ("rndPan")->load());
        level *= 1.0f - random.nextFloat() * p ("rndLevel")->load();
        if (random.nextFloat() < p ("rndRev")->load())
            reverse = ! reverse;
    }

    const float velGain = std::pow (juce::jlimit (0.0f, 1.0f, velocity), 1.2f);
    const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;

    v->active = true;
    v->pad = pad;
    v->note = note;
    v->kit = const_cast<Kit*> (&k);
    v->sample = reg.sample;
    v->start = reg.start;
    v->end = reg.end;
    v->reverse = reverse;
    v->pos = reverse ? (double) reg.end - 1.0 : (double) reg.start;
    v->inc = std::pow (2.0, tune / 12.0) * reg.sample->sampleRate / hostRate;
    v->gainL = std::cos (angle) * juce::MathConstants<float>::sqrt2 * level * velGain;
    v->gainR = std::sin (angle) * juce::MathConstants<float>::sqrt2 * level * velGain;
    v->gate = pp.mode->load() > 0.5f;
    // Slices are cut on zero crossings so forward playback needs only a tiny fade-in;
    // reverse playback starts mid-waveform and gets a longer one.
    v->fadeIn = (float) (reg.sample->sampleRate * (reverse ? 0.003 : 0.0004));
    v->fadeOut = (float) (reg.sample->sampleRate * 0.002);
    v->age = ++voiceCounter;

    juce::ADSR::Parameters ep;
    ep.attack = p ("attack")->load() * 0.001f;
    ep.decay = p ("decay")->load() * 0.001f;
    ep.sustain = p ("sustain")->load();
    ep.release = p ("release")->load() * 0.001f;
    v->env.setParameters (ep);
    v->env.reset();
    v->env.noteOn();

    padGlow[pad] = juce::jmax (0.35f, velocity);
}

void ChopShopProcessor::renderVoices (float* L, float* R, int start, int num)
{
    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        const float* sL = v.sample->buffer.getReadPointer (0);
        const float* sR = v.sample->buffer.getReadPointer (1);
        const int last = v.sample->length() - 1;
        const double dir = v.reverse ? -1.0 : 1.0;

        for (int i = start; i < start + num; ++i)
        {
            if (v.reverse ? v.pos < (double) v.start : v.pos >= (double) v.end)
            {
                v.active = false;
                break;
            }

            const int ip = (int) std::floor (v.pos);
            const float f = (float) (v.pos - ip);
            const int i0 = juce::jlimit (0, last, ip - 1), i1 = juce::jlimit (0, last, ip);
            const int i2 = juce::jlimit (0, last, ip + 1), i3 = juce::jlimit (0, last, ip + 2);
            const float dl = fx::hermite (sL[i0], sL[i1], sL[i2], sL[i3], f);
            const float dr = fx::hermite (sR[i0], sR[i1], sR[i2], sR[i3], f);

            const float played = (float) (v.reverse ? v.end - v.pos : v.pos - v.start);
            const float remaining = (float) (v.reverse ? v.pos - v.start : v.end - v.pos);
            const float declick = juce::jmin (1.0f, played / v.fadeIn) * juce::jmin (1.0f, remaining / v.fadeOut);

            const float env = v.env.getNextSample() * declick;
            L[i] += dl * v.gainL * env;
            R[i] += dr * v.gainR * env;

            if (! v.env.isActive())
            {
                v.active = false;
                break;
            }
            v.pos += v.inc * dir;
        }

        if (! v.active)
            v.kit = nullptr; // the kit stays alive in the garbage list; freed on the message thread
    }
}

//==============================================================================
void ChopShopProcessor::padNoteOn (int pad, float velocity)
{
    if (! prepared)
        return;
    auto m = juce::MidiMessage::noteOn (1, padNoteBase + pad, juce::jlimit (0.01f, 1.0f, velocity));
    m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    uiMidi.addMessageToQueue (m);
}

void ChopShopProcessor::padNoteOff (int pad)
{
    if (! prepared)
        return;
    auto m = juce::MidiMessage::noteOff (1, padNoteBase + pad);
    m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    uiMidi.addMessageToQueue (m);
}

//==============================================================================
Kit::Ptr ChopShopProcessor::getKit() const
{
    const juce::SpinLock::ScopedLockType sl (kitLock);
    return kit;
}

Kit::Ptr ChopShopProcessor::copyKit() const
{
    auto current = getKit();
    return current != nullptr ? new Kit (*current) : new Kit();
}

void ChopShopProcessor::publishKit (Kit::Ptr newKit)
{
    {
        const juce::SpinLock::ScopedLockType sl (kitLock);
        std::swap (kit, newKit);
    }
    if (newKit != nullptr)
    {
        const juce::ScopedLock g (garbageLock);
        garbage.add (newKit.get()); // the previous kit: voices may still be playing it
    }
    sendChangeMessage();
}

void ChopShopProcessor::timerCallback()
{
    const juce::ScopedLock g (garbageLock);
    for (int i = garbage.size(); --i >= 0;)
        if (garbage.getObjectPointerUnchecked (i)->getReferenceCount() == 1)
            garbage.remove (i);
    for (int i = sampleGarbage.size(); --i >= 0;)
        if (sampleGarbage.getObjectPointerUnchecked (i)->getReferenceCount() == 1)
            sampleGarbage.remove (i);
}

bool ChopShopProcessor::isSupportedAudioFile (const juce::String& path) const
{
    return juce::File (path).hasFileExtension ("wav;aif;aiff;flac;mp3;m4a;caf;ogg");
}

SampleData::Ptr ChopShopProcessor::readAudioFile (const juce::File& file, double maxSeconds)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples < 16)
        return nullptr;

    const auto len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * maxSeconds));
    SampleData::Ptr s = new SampleData();
    s->sampleRate = reader->sampleRate;
    s->name = file.getFileNameWithoutExtension();
    s->path = file.getFullPathName();
    s->buffer.setSize (2, len);
    reader->read (&s->buffer, 0, len, 0, true, true);
    if (reader->numChannels == 1)
        s->buffer.copyFrom (1, 0, s->buffer, 0, 0, len);
    return s;
}

std::vector<int> ChopShopProcessor::chop (const SampleData& s) const
{
    const int mode = (int) p ("sliceMode")->load();
    const int count = juce::roundToInt (p ("sliceCount")->load());
    return mode == 0 ? slicer::equal (s, count) : slicer::transients (s, p ("sliceSens")->load(), count);
}

void ChopShopProcessor::stampChopSettings (Kit& k) const
{
    k.chopMode = (int) p ("sliceMode")->load();
    k.chopCount = juce::roundToInt (p ("sliceCount")->load());
    k.chopSens = p ("sliceSens")->load();
}

bool ChopShopProcessor::loadMainSample (const juce::File& file)
{
    auto s = readAudioFile (file);
    if (s == nullptr)
        return false;
    auto k = copyKit();
    k->main = s;
    k->slices = chop (*s);
    k->resetMap();
    stampChopSettings (*k);
    publishKit (k);
    return true;
}

void ChopShopProcessor::loadDemo()
{
    auto s = makeDemoBreak();
    auto k = copyKit();
    k->main = s;
    k->slices = chop (*s);
    k->resetMap();
    stampChopSettings (*k);
    publishKit (k);
}

bool ChopShopProcessor::loadPadSample (int pad, const juce::File& file)
{
    auto s = readAudioFile (file);
    if (s == nullptr || pad < 0 || pad >= numPads)
        return false;
    auto k = copyKit();
    k->padSample[(size_t) pad] = s;
    publishKit (k);
    return true;
}

void ChopShopProcessor::clearPadSample (int pad)
{
    auto k = copyKit();
    k->padSample[(size_t) pad] = nullptr;
    publishKit (k);
}

void ChopShopProcessor::setSlices (std::vector<int> points)
{
    auto k = copyKit();
    if (k->main == nullptr)
        return;
    k->slices = slicer::sanitise (std::move (points), k->main->length());
    k->resetMap();
    publishKit (k);
}

void ChopShopProcessor::shuffleSlices()
{
    auto k = copyKit();
    const int n = juce::jmin (numPads, k->numSlices());
    if (n < 2)
        return;
    std::vector<int> order ((size_t) n);
    for (int i = 0; i < n; ++i) order[(size_t) i] = i;
    juce::Random r;
    for (int i = n - 1; i > 0; --i)
        std::swap (order[(size_t) i], order[(size_t) r.nextInt (i + 1)]);
    for (int i = 0; i < n; ++i)
        k->padSlice[(size_t) i] = order[(size_t) i];
    publishKit (k);
}

void ChopShopProcessor::resetSliceMap()
{
    auto k = copyKit();
    k->resetMap();
    publishKit (k);
}

void ChopShopProcessor::rechop (bool force)
{
    auto current = getKit();
    if (current == nullptr || current->main == nullptr)
        return;
    const int mode = (int) p ("sliceMode")->load();
    const int count = juce::roundToInt (p ("sliceCount")->load());
    const float sens = p ("sliceSens")->load();
    if (! force && mode == current->chopMode && count == current->chopCount && std::abs (sens - current->chopSens) < 1.0e-4f)
        return;

    auto k = copyKit();
    k->slices = chop (*k->main);
    k->resetMap();
    stampChopSettings (*k);
    publishKit (k);
}

void ChopShopProcessor::parameterChanged (const juce::String&, float) { triggerAsyncUpdate(); }
void ChopShopProcessor::handleAsyncUpdate() { rechop (false); }

//==============================================================================
void ChopShopProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("KIT"), nullptr);

    juce::ValueTree kt ("KIT");
    if (auto k = getKit())
    {
        if (k->main != nullptr)
            kt.setProperty ("main", k->main->isDemo ? juce::String ("DEMO") : k->main->path, nullptr);

        juce::StringArray sl, map;
        for (int s : k->slices) sl.add (juce::String (s));
        for (int m : k->padSlice) map.add (juce::String (m));
        kt.setProperty ("slices", sl.joinIntoString (","), nullptr);
        kt.setProperty ("map", map.joinIntoString (","), nullptr);
        kt.setProperty ("chop", juce::String (k->chopMode) + "," + juce::String (k->chopCount) + "," + juce::String (k->chopSens), nullptr);
        for (int i = 0; i < numPads; ++i)
            if (auto& ps = k->padSample[(size_t) i])
                kt.setProperty ("pad" + juce::String (i), ps->path, nullptr);
    }
    kt.setProperty ("selPad", selectedPad.load(), nullptr);
    state.appendChild (kt, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ChopShopProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;
    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.hasType (apvts.state.getType()))
        return;

    auto kt = tree.getChildWithName ("KIT").createCopy();
    tree.removeChild (tree.getChildWithName ("KIT"), nullptr);
    apvts.replaceState (tree);
    if (kt.isValid())
        restoreKit (kt);
}

void ChopShopProcessor::restoreKit (const juce::ValueTree& kt)
{
    Kit::Ptr k = new Kit();
    const auto mainPath = kt.getProperty ("main").toString();
    if (mainPath == "DEMO")
        k->main = makeDemoBreak();
    else if (mainPath.isNotEmpty())
        k->main = readAudioFile (juce::File (mainPath));

    for (int i = 0; i < numPads; ++i)
    {
        const auto path = kt.getProperty ("pad" + juce::String (i)).toString();
        if (path.isNotEmpty())
            k->padSample[(size_t) i] = readAudioFile (juce::File (path));
    }

    if (k->main != nullptr)
    {
        std::vector<int> sl;
        for (auto& t : juce::StringArray::fromTokens (kt.getProperty ("slices").toString(), ",", ""))
            sl.push_back (t.getIntValue());
        k->slices = slicer::sanitise (sl, k->main->length());

        auto map = juce::StringArray::fromTokens (kt.getProperty ("map").toString(), ",", "");
        if (map.size() == numPads)
            for (int i = 0; i < numPads; ++i)
            {
                const int m = map[i].getIntValue();
                k->padSlice[(size_t) i] = m < k->numSlices() ? m : -1;
            }
        else
            k->resetMap();
    }

    // The slice points were saved with the kit, so mark them as matching the current slicer
    // settings; otherwise restoring the parameters would re-chop and discard manual edits.
    stampChopSettings (*k);
    selectedPad = juce::jlimit (0, numPads - 1, (int) kt.getProperty ("selPad", 0));
    publishKit (k);
}

//==============================================================================
juce::AudioProcessorEditor* ChopShopProcessor::createEditor() { return new ChopShopEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChopShopProcessor(); }
