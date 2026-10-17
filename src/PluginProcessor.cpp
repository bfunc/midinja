#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "core/Scales.h"

MidinjaProcessor::MidinjaProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    events.reserve (512);
    publish();
}

MidinjaProcessor::~MidinjaProcessor()
{
    sendAllNotesOffNow();
    openMidiOut ({});
}

// ---------------------------------------------------------------------------------------------
// UI side

void MidinjaProcessor::publish()
{
    const auto& d = sess.doc;
    const auto seq = d.playSeq();
    const juce::SpinLock::ScopedLockType lock (stateLock);
    shared.parts = d.parts;
    shared.seqLen = juce::jlimit (1, midinja::kMaxBars, (int) seq.size());
    for (int i = 0; i < shared.seqLen; ++i)
        shared.seq[(size_t) i] = juce::jlimit (0, midinja::kNumParts - 1, seq[(size_t) i]);
    shared.rate = d.rate;
    shared.swing = d.swing;
    ++version;
}

bool MidinjaProcessor::openMidiOut (const juce::String& identifier)
{
    std::unique_ptr<juce::MidiOutput> next;
    if (identifier.isNotEmpty())
    {
        next = juce::MidiOutput::openDevice (identifier);
        if (next == nullptr)
            return false;
        next->startBackgroundThread();
    }

    sendAllNotesOffNow();
    {
        const juce::SpinLock::ScopedLockType lock (midiOutLock);
        std::swap (midiOut, next);
        midiOutIdentifier = identifier;
    }
    next.reset(); // the old port closes outside the lock
    return true;
}

void MidinjaProcessor::setChannel (int newChannel)
{
    sendAllNotesOffNow(); // on the old channel
    channel = juce::jlimit (1, 16, newChannel);
}

void MidinjaProcessor::sendAllNotesOffNow()
{
    const juce::SpinLock::ScopedLockType lock (midiOutLock);
    if (midiOut != nullptr)
        midiOut->sendMessageNow (juce::MidiMessage::allNotesOff (channel.load()));
}

juce::MemoryBlock MidinjaProcessor::exportMidi() const
{
    const auto bytes = sess.midiFile ((double) bpm.load(), channel.load());
    return juce::MemoryBlock (bytes.data(), bytes.size());
}

midinja::Rate MidinjaProcessor::rateOf (int stepsPerBeat)
{
    return stepsPerBeat == 2 ? midinja::Rate::Eighth
         : stepsPerBeat == 6 ? midinja::Rate::SixteenthTriplet
                             : midinja::Rate::Sixteenth;
}

// ---------------------------------------------------------------------------------------------
// Audio

void MidinjaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    sequencer.prepare (sampleRate);
    synth.prepare (sampleRate);
    outBuffer.ensureSize (4096);
    events.reserve (512);
    monoScratch.assign ((size_t) juce::jmax (1, samplesPerBlock) * 2, 0.0f);
    playStep = -1;
    playBar = -1;
}

void MidinjaProcessor::releaseResources()
{
    synth.reset();
}

bool MidinjaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void MidinjaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    midi.clear();

    // Pick up the bars the UI published.
    {
        const juce::SpinLock::ScopedTryLockType lock (stateLock);
        if (lock.isLocked() && seenVersion != version.load())
        {
            audio = shared;
            seenVersion = version.load();
            barIdx = juce::jlimit (0, audio.seqLen - 1, barIdx);
            playPat = audio.parts[(size_t) audio.seq[(size_t) barIdx]];
        }
    }

    // The host's tempo and transport (VST3). The plugin follows the host while it plays and
    // starts again from the first bar when the transport starts.
    bool hostStart = false;
    if (isHosted())
    {
        if (auto* ph = getPlayHead())
        {
            if (const auto pos = ph->getPosition())
            {
                if (const auto b = pos->getBpm())
                    hostBpm = (float) *b;
                const bool hp = pos->getIsPlaying();
                if (hp && ! lastHostPlaying)
                    hostStart = true;
                lastHostPlaying = hp;
                hostPlaying = hp;
            }
        }
    }

    events.clear();
    const bool wantPlay = playing.load() || (isHosted() && hostPlaying.load());
    if (restart.exchange (false) || hostStart)
    {
        if (sequencer.isPlaying())
            sequencer.stop (events);
        barIdx = 0;
        playPat = audio.parts[(size_t) audio.seq[0]];
    }
    if (wantPlay && ! sequencer.isPlaying())
    {
        barIdx = 0;
        playPat = audio.parts[(size_t) audio.seq[0]];
        sequencer.start();
    }
    else if (! wantPlay && sequencer.isPlaying())
    {
        sequencer.stop (events);
    }

    const double useBpm = isHosted() && hostBpm.load() > 0.0f ? (double) hostBpm.load() : (double) bpm.load();
    sequencer.process (numSamples, useBpm, rateOf (audio.rate), audio.swing, playPat, events,
                       [this] (midinja::Pattern& p) {
                           barIdx = (barIdx + 1) % audio.seqLen;
                           p = audio.parts[(size_t) audio.seq[(size_t) barIdx]];
                       });
    playStep = sequencer.currentStep();
    playBar = sequencer.isPlaying() ? barIdx : -1;

    // Synth settings for this block.
    const bool useSynth = synthOn.load();
    synth.wave = synthWave.load() == 1 ? midinja::MonoSynth::Wave::Square : midinja::MonoSynth::Wave::Saw;
    synth.cutoffHz = cutoff.load();
    synth.resonance = resonance.load();
    synth.decayMs = decay.load();
    synth.volume = volume.load();
    if (! useSynth)
        synth.reset();

    float* left = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    if (right == nullptr)
    {
        if ((int) monoScratch.size() < numSamples)
            monoScratch.resize ((size_t) numSamples); // only if the host lied about the block size
        std::fill (monoScratch.begin(), monoScratch.begin() + numSamples, 0.0f);
        right = monoScratch.data();
    }

    const int ch = channel.load();
    outBuffer.clear();
    int pos = 0;
    for (const auto& e : events)
    {
        const int at = juce::jlimit (0, numSamples - 1, e.sampleOffset);
        if (useSynth && at > pos)
            synth.render (left, right, pos, at - pos);
        pos = juce::jmax (pos, at);

        switch (e.status)
        {
            case 0x90:
                if (useSynth) synth.noteOn (e.data1, e.data2);
                outBuffer.addEvent (juce::MidiMessage::noteOn (ch, e.data1, (juce::uint8) e.data2), at);
                break;
            case 0x80:
                if (useSynth) synth.noteOff (e.data1);
                outBuffer.addEvent (juce::MidiMessage::noteOff (ch, e.data1), at);
                break;
            default:
                if (useSynth) synth.allOff();
                outBuffer.addEvent (juce::MidiMessage::controllerEvent (ch, e.data1, e.data2), at);
                break;
        }
    }
    if (useSynth && pos < numSamples)
        synth.render (left, right, pos, numSamples - pos);

    if (! outBuffer.isEmpty())
    {
        midi.addEvents (outBuffer, 0, numSamples, 0); // to the host (VST3)
        const juce::SpinLock::ScopedTryLockType lock (midiOutLock);
        if (lock.isLocked() && midiOut != nullptr)
            midiOut->sendBlockOfMessages (outBuffer, juce::Time::getMillisecondCounterHiRes() + 1.0,
                                          currentSampleRate);
    }
}

juce::AudioProcessorEditor* MidinjaProcessor::createEditor()
{
    return new MidinjaEditor (*this);
}

// ---------------------------------------------------------------------------------------------
// Saved state: the standalone app keeps it between runs, the host keeps it per plugin instance.

void MidinjaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto& d = sess.doc;
    juce::XmlElement xml ("Midinja");
    xml.setAttribute ("format", 2);
    xml.setAttribute ("bpm", (double) bpm.load());
    xml.setAttribute ("channel", channel.load());
    xml.setAttribute ("synthOn", synthOn.load());
    xml.setAttribute ("wave", synthWave.load());
    xml.setAttribute ("cutoff", (double) cutoff.load());
    xml.setAttribute ("resonance", (double) resonance.load());
    xml.setAttribute ("decay", (double) decay.load());
    xml.setAttribute ("volume", (double) volume.load());
    xml.setAttribute ("midiOut", midiOutIdentifier);

    xml.setAttribute ("view", (int) sess.view);
    xml.setAttribute ("adv", sess.adv);
    xml.setAttribute ("what", (int) sess.what);
    xml.setAttribute ("mutAmt", (double) sess.mutAmt);

    const auto& g = d.gen;
    xml.setAttribute ("root", g.root);
    xml.setAttribute ("scale", g.scale);
    xml.setAttribute ("low", g.lowNote);
    xml.setAttribute ("high", g.highNote);
    xml.setAttribute ("notes", g.notes);
    xml.setAttribute ("lenMin", g.lenMin);
    xml.setAttribute ("lenMax", g.lenMax);
    xml.setAttribute ("velMin", g.velMin);
    xml.setAttribute ("velMax", g.velMax);
    xml.setAttribute ("repOn", g.repeatsOn);
    xml.setAttribute ("repMin", g.repMin);
    xml.setAttribute ("repMax", g.repMax);
    xml.setAttribute ("repChance", (double) g.repChance);
    xml.setAttribute ("color", (double) g.color);
    xml.setAttribute ("surprise", (double) g.surprise);
    xml.setAttribute ("similarity", (double) g.similarity);
    xml.setAttribute ("startOnRoot", g.startOnRoot);
    xml.setAttribute ("seed", juce::String ((juce::int64) g.seed));
    xml.setAttribute ("rate", d.rate);
    xml.setAttribute ("swing", (double) d.swing);
    xml.setAttribute ("transpose", d.transpose);
    xml.setAttribute ("cur", d.cur);
    xml.setAttribute ("sel", d.sel);
    juce::StringArray arr;
    for (int k : d.arr)
        arr.add (juce::String (k));
    xml.setAttribute ("arr", arr.joinIntoString (","));

    for (int k = 0; k < midinja::kNumParts; ++k)
    {
        auto* part = xml.createNewChildElement ("part");
        part->setAttribute ("lock", d.locks[(size_t) k]);
        part->setAttribute ("src", (int) d.src[(size_t) k]);
        const auto& p = d.parts[(size_t) k];
        part->setAttribute ("numSteps", p.numSteps);
        for (int i = 0; i < midinja::kMaxSteps; ++i)
        {
            const auto& st = p.steps[(size_t) i];
            auto* e = part->createNewChildElement ("step");
            e->setAttribute ("on", st.on);
            e->setAttribute ("note", st.note);
            e->setAttribute ("len", st.length);
            e->setAttribute ("vel", st.velocity);
            e->setAttribute ("rep", st.repeats);
            e->setAttribute ("lock", st.locked);
        }
    }
    copyXmlToBinary (xml, destData);
}

void MidinjaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("Midinja") || xml->getIntAttribute ("format", 1) < 2)
        return; // a v0.1 state (one pattern): start fresh

    bpm = (float) juce::jlimit (40.0, 240.0, xml->getDoubleAttribute ("bpm", 122.0));
    channel = juce::jlimit (1, 16, xml->getIntAttribute ("channel", 1));
    synthOn = xml->getBoolAttribute ("synthOn", true);
    synthWave = juce::jlimit (0, 1, xml->getIntAttribute ("wave", 0));
    cutoff = (float) xml->getDoubleAttribute ("cutoff", 1800.0);
    resonance = (float) xml->getDoubleAttribute ("resonance", 0.3);
    decay = (float) xml->getDoubleAttribute ("decay", 250.0);
    volume = (float) xml->getDoubleAttribute ("volume", 0.6);

    sess.view = (midinja::View) juce::jlimit (0, 2, xml->getIntAttribute ("view", 1));
    sess.adv = xml->getBoolAttribute ("adv", false);
    sess.what = (midinja::Op) juce::jlimit (0, 3, xml->getIntAttribute ("what", 0));
    sess.mutAmt = (float) xml->getDoubleAttribute ("mutAmt", 0.15);

    midinja::Doc d;
    auto& g = d.gen;
    g.mode = midinja::kMusician;
    g.root = juce::jlimit (0, 11, xml->getIntAttribute ("root", g.root));
    g.scale = juce::jlimit (0, (int) midinja::scales().size() - 1, xml->getIntAttribute ("scale", g.scale));
    g.lowNote = juce::jlimit (0, 127, xml->getIntAttribute ("low", g.lowNote));
    g.highNote = juce::jlimit (0, 127, xml->getIntAttribute ("high", g.highNote));
    g.notes = juce::jlimit (1, midinja::kMaxSteps, xml->getIntAttribute ("notes", g.notes));
    g.lenMin = xml->getIntAttribute ("lenMin", g.lenMin);
    g.lenMax = xml->getIntAttribute ("lenMax", g.lenMax);
    g.velMin = xml->getIntAttribute ("velMin", g.velMin);
    g.velMax = xml->getIntAttribute ("velMax", g.velMax);
    g.repeatsOn = xml->getBoolAttribute ("repOn", false);
    g.repMin = xml->getIntAttribute ("repMin", g.repMin);
    g.repMax = xml->getIntAttribute ("repMax", g.repMax);
    g.repChance = (float) xml->getDoubleAttribute ("repChance", g.repChance);
    g.color = (float) xml->getDoubleAttribute ("color", g.color);
    g.surprise = (float) xml->getDoubleAttribute ("surprise", g.surprise);
    g.similarity = (float) xml->getDoubleAttribute ("similarity", g.similarity);
    g.startOnRoot = xml->getBoolAttribute ("startOnRoot", true);
    g.seed = (uint32_t) xml->getStringAttribute ("seed", "1").getLargeIntValue();
    const int rate = xml->getIntAttribute ("rate", 4);
    d.rate = rate == 2 || rate == 6 ? rate : 4;
    d.swing = (float) juce::jlimit (0.0, 1.0, xml->getDoubleAttribute ("swing", 0.18));
    d.transpose = xml->getIntAttribute ("transpose", 0);
    d.cur = juce::jlimit (0, midinja::kNumParts - 1, xml->getIntAttribute ("cur", 0));
    d.arr.clear();
    for (const auto& s : juce::StringArray::fromTokens (xml->getStringAttribute ("arr"), ",", ""))
        if (s.trim().isNotEmpty() && (int) d.arr.size() < midinja::kMaxBars)
            d.arr.push_back (juce::jlimit (0, midinja::kNumParts - 1, s.getIntValue()));
    d.sel = xml->getIntAttribute ("sel", -1);
    if (d.sel >= (int) d.arr.size())
        d.sel = -1;

    int k = 0;
    for (auto* part : xml->getChildWithTagNameIterator ("part"))
    {
        if (k >= midinja::kNumParts)
            break;
        d.locks[(size_t) k] = part->getBoolAttribute ("lock");
        d.src[(size_t) k] = (midinja::Src) juce::jlimit (0, 2, part->getIntAttribute ("src", 0));
        auto& p = d.parts[(size_t) k];
        p.numSteps = d.numSteps();
        int i = 0;
        for (auto* e : part->getChildWithTagNameIterator ("step"))
        {
            if (i >= midinja::kMaxSteps)
                break;
            auto& st = p.steps[(size_t) i++];
            st.on = e->getBoolAttribute ("on");
            st.note = juce::jlimit (0, 127, e->getIntAttribute ("note", 60));
            st.length = juce::jlimit (1, midinja::kMaxLength, e->getIntAttribute ("len", 1));
            st.velocity = juce::jlimit (1, 127, e->getIntAttribute ("vel", 100));
            st.repeats = juce::jlimit (1, midinja::kMaxRepeats, e->getIntAttribute ("rep", 1));
            st.locked = e->getBoolAttribute ("lock");
        }
        ++k;
    }
    for (auto& p : d.parts)
        midinja::fitLengths (p);

    sess.doc = d;
    sess.versions.clear();
    sess.undo.clear();
    sess.vi = -1;
    sess.ui = -1;
    sess.message.clear();
    sess.commitVersion();
    publish();

    const auto port = xml->getStringAttribute ("midiOut");
    if (port.isNotEmpty())
        openMidiOut (port); // stays closed if the device is gone
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MidinjaProcessor();
}
