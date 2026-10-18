#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "core/Scales.h"

using midinja::kNumParts;
using midinja::kNumTabs;

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
    PlayModel m;
    for (int i = 0; i < kNumTabs; ++i)
    {
        const auto& t = d.tabs[(size_t) i];
        auto& pt = m.tabs[(size_t) i];
        pt.on = t.on;
        pt.poly = t.poly;
        pt.channel = t.channel;
        pt.parts = t.parts;
        if (t.poly)
            for (int k = 0; k < kNumParts; ++k)
            {
                for (auto& st : pt.parts[(size_t) k].steps)
                    st.note = k; // the part index stands in for the chord
                const auto notes = d.chordNotes (k, t);
                pt.chordN[(size_t) k] = std::min ((int) notes.size(), midinja::kMaxChordNotes);
                for (int j = 0; j < pt.chordN[(size_t) k]; ++j)
                    pt.chord[(size_t) k][(size_t) j] = juce::jlimit (0, 127, notes[(size_t) j]);
            }
    }
    m.seqLen = juce::jlimit (1, midinja::kMaxBars, (int) seq.size());
    for (int i = 0; i < m.seqLen; ++i)
        m.seq[(size_t) i] = juce::jlimit (0, kNumParts - 1, seq[(size_t) i]);
    m.rate = d.rate;
    m.swing = d.swing;
    {
        const juce::SpinLock::ScopedLockType lock (stateLock);
        shared = m;
    }
    ++version;
}

bool MidinjaProcessor::openMidiOut (const juce::String& identifier)
{
    std::unique_ptr<juce::MidiOutput> next;
    if (isHosted() && identifier.isNotEmpty())
        return false; // in a host the MIDI goes to the host only, and the host routes it (FL: wrapper MIDI output port)
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

void MidinjaProcessor::sendAllNotesOffNow()
{
    const juce::SpinLock::ScopedLockType lock (midiOutLock);
    if (midiOut != nullptr)
        for (const auto& t : sess.doc.tabs)
            midiOut->sendMessageNow (juce::MidiMessage::allNotesOff (t.channel));
}

juce::MemoryBlock MidinjaProcessor::exportMidi()
{
    const auto bytes = sess.midiFile ((double) bpm.load());
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
    for (int i = 0; i < kNumTabs; ++i)
    {
        sequencers[(size_t) i].prepare (sampleRate);
        mono[(size_t) i].prepare (sampleRate);
        pad[(size_t) i].prepare (sampleRate);
    }
    outBuffer.ensureSize (8192);
    events.reserve (512);
    monoScratch.assign ((size_t) juce::jmax (1, samplesPerBlock) * 2, 0.0f);
    playStep = -1;
    playBar = -1;
}

void MidinjaProcessor::releaseResources()
{
    for (auto& s : mono) s.reset();
    for (auto& s : pad) s.reset();
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
            for (int i = 0; i < kNumTabs; ++i)
                playPat[(size_t) i] = audio.tabs[(size_t) i].parts[(size_t) audio.seq[(size_t) barIdx]];
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

    const bool wantPlay = playing.load() || (isHosted() && hostPlaying.load());
    const double useBpm = isHosted() && hostBpm.load() > 0.0f ? (double) hostBpm.load() : (double) bpm.load();
    const bool useSynth = synthOn.load();
    for (int i = 0; i < kNumTabs; ++i)
    {
        auto& ms = mono[(size_t) i];
        ms.wave = synthWave.load() == 1 ? midinja::MonoSynth::Wave::Square : midinja::MonoSynth::Wave::Saw;
        ms.cutoffHz = cutoff.load();
        ms.resonance = resonance.load();
        ms.decayMs = decay.load();
        ms.volume = volume.load();
        auto& ps = pad[(size_t) i];
        ps.cutoffHz = cutoff.load();
        ps.resonance = resonance.load();
        ps.volume = volume.load();
        if (! useSynth)
        {
            ms.reset();
            ps.reset();
        }
    }

    float* left = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    if (right == nullptr)
    {
        if ((int) monoScratch.size() < numSamples)
            monoScratch.resize ((size_t) numSamples); // only if the host lied about the block size
        std::fill (monoScratch.begin(), monoScratch.begin() + numSamples, 0.0f);
        right = monoScratch.data();
    }

    const bool doRestart = restart.exchange (false) || hostStart;
    outBuffer.clear();
    // Every tab's sequencer runs in step with the others; the first one advances the bar.
    for (int i = 0; i < kNumTabs; ++i)
    {
        auto& sq = sequencers[(size_t) i];
        auto& pt = audio.tabs[(size_t) i];
        auto& pp = playPat[(size_t) i];
        events.clear();
        if (doRestart)
        {
            if (sq.isPlaying())
                sq.stop (events);
            if (i == 0)
                barIdx = 0;
            pp = pt.parts[(size_t) audio.seq[0]];
        }
        if (wantPlay && ! sq.isPlaying())
        {
            if (i == 0)
                barIdx = 0;
            pp = pt.parts[(size_t) audio.seq[0]];
            sq.start();
        }
        else if (! wantPlay && sq.isPlaying())
        {
            sq.stop (events);
        }
        sq.process (numSamples, useBpm, rateOf (audio.rate), audio.swing, pp, events,
                    [this, i] (midinja::Pattern& p) {
                        if (i == 0)
                            barIdx = (barIdx + 1) % audio.seqLen;
                        p = audio.tabs[(size_t) i].parts[(size_t) audio.seq[(size_t) barIdx]];
                    });
        if (i == 0)
        {
            playStep = sq.currentStep();
            playBar = sq.isPlaying() ? barIdx : -1;
        }

        const int ch = pt.channel;
        const bool tabSynth = useSynth && pt.on;
        auto& ms = mono[(size_t) i];
        auto& ps = pad[(size_t) i];
        int pos = 0;
        for (const auto& e : events)
        {
            const int at = juce::jlimit (0, numSamples - 1, e.sampleOffset);
            if (tabSynth && at > pos)
            {
                if (pt.poly) ps.render (left, right, pos, at - pos);
                else ms.render (left, right, pos, at - pos);
            }
            pos = juce::jmax (pos, at);

            switch (e.status)
            {
                case 0x90:
                {
                    if (! pt.on)
                        break; // an off tab is silent
                    if (pt.poly)
                    {
                        const int k = juce::jlimit (0, kNumParts - 1, (int) e.data1);
                        auto& held = sounding[(size_t) i][(size_t) k];
                        auto& heldN = soundingN[(size_t) i][(size_t) k];
                        for (int j = 0; j < heldN; ++j) // a hit still sounding: release it first
                        {
                            if (tabSynth) ps.noteOff (held[(size_t) j]);
                            outBuffer.addEvent (juce::MidiMessage::noteOff (ch, held[(size_t) j]), at);
                        }
                        heldN = pt.chordN[(size_t) k];
                        for (int j = 0; j < heldN; ++j)
                        {
                            held[(size_t) j] = pt.chord[(size_t) k][(size_t) j];
                            if (tabSynth) ps.noteOn (held[(size_t) j], e.data2);
                            outBuffer.addEvent (juce::MidiMessage::noteOn (ch, held[(size_t) j], (juce::uint8) e.data2), at);
                        }
                    }
                    else
                    {
                        if (tabSynth) ms.noteOn (e.data1, e.data2);
                        outBuffer.addEvent (juce::MidiMessage::noteOn (ch, e.data1, (juce::uint8) e.data2), at);
                    }
                    break;
                }
                case 0x80:
                {
                    if (pt.poly)
                    {
                        const int k = juce::jlimit (0, kNumParts - 1, (int) e.data1);
                        auto& held = sounding[(size_t) i][(size_t) k];
                        auto& heldN = soundingN[(size_t) i][(size_t) k];
                        for (int j = 0; j < heldN; ++j)
                        {
                            ps.noteOff (held[(size_t) j]);
                            outBuffer.addEvent (juce::MidiMessage::noteOff (ch, held[(size_t) j]), at);
                        }
                        heldN = 0;
                    }
                    else
                    {
                        ms.noteOff (e.data1);
                        outBuffer.addEvent (juce::MidiMessage::noteOff (ch, e.data1), at);
                    }
                    break;
                }
                default:
                    ms.allOff();
                    ps.allOff();
                    for (auto& n : soundingN[(size_t) i])
                        n = 0;
                    outBuffer.addEvent (juce::MidiMessage::controllerEvent (ch, e.data1, e.data2), at);
                    break;
            }
        }
        if (tabSynth && pos < numSamples)
        {
            if (pt.poly) ps.render (left, right, pos, numSamples - pos);
            else ms.render (left, right, pos, numSamples - pos);
        }
    }

    if (! outBuffer.isEmpty())
    {
        midi.addEvents (outBuffer, 0, numSamples, 0); // to the host (VST3); midiOut is only ever open in the Standalone
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
    xml.setAttribute ("format", 3);
    xml.setAttribute ("bpm", (double) bpm.load());
    xml.setAttribute ("synthOn", synthOn.load());
    xml.setAttribute ("wave", synthWave.load());
    xml.setAttribute ("cutoff", (double) cutoff.load());
    xml.setAttribute ("resonance", (double) resonance.load());
    xml.setAttribute ("decay", (double) decay.load());
    xml.setAttribute ("volume", (double) volume.load());
    xml.setAttribute ("midiOut", midiOutIdentifier);

    xml.setAttribute ("adv", sess.adv);
    xml.setAttribute ("what", (int) sess.what);
    xml.setAttribute ("mutAmt", (double) sess.mutAmt);

    const auto& g = d.gen;
    xml.setAttribute ("root", g.root);
    xml.setAttribute ("scale", g.scale);
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
    xml.setAttribute ("cur", d.cur);
    xml.setAttribute ("sel", d.sel);
    xml.setAttribute ("tab", d.tab);
    juce::StringArray arr;
    for (int k : d.arr)
        arr.add (juce::String (k));
    xml.setAttribute ("arr", arr.joinIntoString (","));

    for (int k = 0; k < kNumParts; ++k)
    {
        auto* c = xml.createNewChildElement ("chord");
        c->setAttribute ("degree", d.chords[(size_t) k].degree);
        c->setAttribute ("type", (int) d.chords[(size_t) k].type);
        c->setAttribute ("inv", d.chords[(size_t) k].inv);
    }
    for (int i = 0; i < kNumTabs; ++i)
    {
        const auto& t = d.tabs[(size_t) i];
        auto* tx = xml.createNewChildElement ("tab");
        tx->setAttribute ("channel", t.channel);
        tx->setAttribute ("poly", t.poly);
        tx->setAttribute ("on", t.on);
        tx->setAttribute ("rootNote", t.rootNote);
        tx->setAttribute ("low", t.lowNote);
        tx->setAttribute ("high", t.highNote);
        for (int k = 0; k < kNumParts; ++k)
        {
            auto* part = tx->createNewChildElement ("part");
            part->setAttribute ("lock", t.locks[(size_t) k]);
            part->setAttribute ("src", (int) t.src[(size_t) k]);
            part->setAttribute ("tpl", t.tpl[(size_t) k]);
            const auto& p = t.parts[(size_t) k];
            part->setAttribute ("numSteps", p.numSteps);
            const auto& b = t.base[(size_t) k];
            if (b.valid)
            {
                part->setAttribute ("baseScale", b.scale);
                juce::StringArray bn;
                for (int n : b.notes)
                    bn.add (juce::String (n));
                part->setAttribute ("base", bn.joinIntoString (","));
            }
            for (int s = 0; s < midinja::kMaxSteps; ++s)
            {
                const auto& st = p.steps[(size_t) s];
                auto* e = part->createNewChildElement ("step");
                e->setAttribute ("on", st.on);
                e->setAttribute ("note", st.note);
                e->setAttribute ("len", st.length);
                e->setAttribute ("vel", st.velocity);
                e->setAttribute ("rep", st.repeats);
                e->setAttribute ("lock", st.locked);
            }
        }
    }
    copyXmlToBinary (xml, destData);
}

void MidinjaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("Midinja") || xml->getIntAttribute ("format", 1) < 3)
        return; // an older state (no tabs): start fresh

    bpm = (float) juce::jlimit (40.0, 240.0, xml->getDoubleAttribute ("bpm", 122.0));
    synthOn = xml->getBoolAttribute ("synthOn", true);
    synthWave = juce::jlimit (0, 1, xml->getIntAttribute ("wave", 0));
    cutoff = (float) xml->getDoubleAttribute ("cutoff", 1800.0);
    resonance = (float) xml->getDoubleAttribute ("resonance", 0.3);
    decay = (float) xml->getDoubleAttribute ("decay", 250.0);
    volume = (float) xml->getDoubleAttribute ("volume", 0.6);

    sess.adv = xml->getBoolAttribute ("adv", false);
    sess.what = (midinja::Op) juce::jlimit (0, 3, xml->getIntAttribute ("what", 0));
    sess.mutAmt = (float) xml->getDoubleAttribute ("mutAmt", 0.15);

    midinja::Doc d = sess.doc;
    auto& g = d.gen;
    g.mode = midinja::kMusician;
    g.root = juce::jlimit (0, 11, xml->getIntAttribute ("root", g.root));
    g.scale = juce::jlimit (0, (int) midinja::scales().size() - 1, xml->getIntAttribute ("scale", g.scale));
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
    d.cur = juce::jlimit (0, kNumParts - 1, xml->getIntAttribute ("cur", 0));
    d.tab = juce::jlimit (0, kNumTabs - 1, xml->getIntAttribute ("tab", 0));
    d.arr.clear();
    for (const auto& s : juce::StringArray::fromTokens (xml->getStringAttribute ("arr"), ",", ""))
        if (s.trim().isNotEmpty() && (int) d.arr.size() < midinja::kMaxBars)
            d.arr.push_back (juce::jlimit (0, kNumParts - 1, s.getIntValue()));
    d.sel = xml->getIntAttribute ("sel", -1);
    if (d.sel >= (int) d.arr.size())
        d.sel = -1;

    int k = 0;
    for (auto* c : xml->getChildWithTagNameIterator ("chord"))
    {
        if (k >= kNumParts)
            break;
        d.chords[(size_t) k].degree = juce::jlimit (0, 11, c->getIntAttribute ("degree", 0));
        d.chords[(size_t) k].type = (midinja::ChordType) juce::jlimit (0, 3, c->getIntAttribute ("type", 0));
        d.chords[(size_t) k].inv = juce::jlimit (0, 3, c->getIntAttribute ("inv", 0));
        ++k;
    }
    int i = 0;
    for (auto* tx : xml->getChildWithTagNameIterator ("tab"))
    {
        if (i >= kNumTabs)
            break;
        auto& t = d.tabs[(size_t) i];
        t.channel = juce::jlimit (1, 16, tx->getIntAttribute ("channel", i + 1));
        t.poly = tx->getBoolAttribute ("poly", false);
        t.on = tx->getBoolAttribute ("on", i == 0);
        t.rootNote = juce::jlimit (0, 127, tx->getIntAttribute ("rootNote", 40));
        t.lowNote = juce::jlimit (0, 127, tx->getIntAttribute ("low", 40));
        t.highNote = juce::jlimit (t.lowNote, 127, tx->getIntAttribute ("high", 64));
        int part = 0;
        for (auto* px : tx->getChildWithTagNameIterator ("part"))
        {
            if (part >= kNumParts)
                break;
            t.locks[(size_t) part] = px->getBoolAttribute ("lock");
            t.src[(size_t) part] = (midinja::Src) juce::jlimit (0, 2, px->getIntAttribute ("src", 0));
            t.tpl[(size_t) part] = px->getIntAttribute ("tpl", -1);
            auto& p = t.parts[(size_t) part];
            p = midinja::Pattern {};
            p.numSteps = d.numSteps();
            auto& b = t.base[(size_t) part];
            b = midinja::Base {};
            if (px->hasAttribute ("base"))
            {
                b.valid = true;
                b.scale = juce::jlimit (0, (int) midinja::scales().size() - 1, px->getIntAttribute ("baseScale", g.scale));
                int j = 0;
                for (const auto& s : juce::StringArray::fromTokens (px->getStringAttribute ("base"), ",", ""))
                    if (j < midinja::kMaxSteps)
                        b.notes[(size_t) j++] = s.getIntValue();
            }
            int s = 0;
            for (auto* e : px->getChildWithTagNameIterator ("step"))
            {
                if (s >= midinja::kMaxSteps)
                    break;
                auto& st = p.steps[(size_t) s++];
                st.on = e->getBoolAttribute ("on");
                st.note = juce::jlimit (0, 127, e->getIntAttribute ("note", 60));
                st.length = juce::jlimit (1, midinja::kMaxLength, e->getIntAttribute ("len", 1));
                st.velocity = juce::jlimit (1, 127, e->getIntAttribute ("vel", 100));
                st.repeats = juce::jlimit (1, midinja::kMaxRepeats, e->getIntAttribute ("rep", 1));
                st.locked = e->getBoolAttribute ("lock");
            }
            midinja::fitLengths (p);
            ++part;
        }
        ++i;
    }

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
