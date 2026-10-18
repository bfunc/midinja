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
    m.seqLen = juce::jlimit (1, midinja::kMaxBars, (int) seq.size());
    for (int i = 0; i < m.seqLen; ++i)
        m.seq[(size_t) i] = juce::jlimit (0, kNumParts - 1, seq[(size_t) i]);
    m.numSteps = d.numSteps();
    m.rate = d.rate;
    m.swing = d.swing;
    for (int i = 0; i < kNumTabs; ++i)
    {
        const auto& t = d.tabs[(size_t) i];
        auto& pt = m.tabs[(size_t) i];
        pt.on = t.on;
        pt.poly = t.poly || t.drums;
        pt.drums = t.drums;
        pt.channel = t.channel;
        pt.parts = t.parts;
        if (! pt.poly)
            continue;
        // the Chords tab plays the lane: one pattern per bar of what Play goes through
        for (int b = 0; b < m.seqLen; ++b)
        {
            // which bar of the arrangement is this slot of the play sequence?
            int bar = b;
            if (d.sel >= 0 && d.sel < (int) d.arr.size()) bar = d.sel;
            else if (d.arr.empty()) bar = -1;
            auto& p = pt.barPats[(size_t) b];
            p = midinja::Pattern {};
            p.numSteps = m.numSteps;
            if (t.drums)
            {
                // the Beats tab: the hits of the slot's part, several drums on a step share one hit
                std::array<int, midinja::kMaxSteps> idx;
                idx.fill (-1);
                int h = 0;
                for (const auto& e : d.drumHits (m.seq[(size_t) b], bar))
                {
                    if (e.step < 0 || e.step >= m.numSteps)
                        continue;
                    int hi = idx[(size_t) e.step];
                    if (hi < 0)
                    {
                        if (h >= kHitsPerBar)
                            break;
                        hi = idx[(size_t) e.step] = h++;
                        auto& st = p.steps[(size_t) e.step];
                        st.on = true;
                        st.note = b * kHitsPerBar + hi;
                        st.length = 1;
                        st.velocity = juce::jlimit (1, 127, e.velocity);
                    }
                    auto& cnt = pt.noteCount[(size_t) b][(size_t) hi];
                    if (cnt >= midinja::kMaxChordNotes)
                        continue;
                    pt.notes[(size_t) b][(size_t) hi][(size_t) cnt] = midinja::drumNote (e.row);
                    pt.vels[(size_t) b][(size_t) hi][(size_t) cnt] = juce::jlimit (1, 127, e.velocity);
                    ++cnt;
                }
                continue;
            }
            if (bar < 0)
                continue;
            int h = 0;
            for (const auto& hit : d.barHits (bar))
            {
                if (h >= kHitsPerBar || hit.step < 0 || hit.step >= midinja::kMaxSteps)
                    break;
                auto& st = p.steps[(size_t) hit.step];
                if (st.on)
                    continue; // two chords never start on the same step
                st.on = true;
                st.note = b * kHitsPerBar + h;
                st.length = juce::jlimit (1, midinja::kMaxLength, hit.len);
                st.velocity = 100;
                const auto notes = d.voice (d.chords[(size_t) hit.chordIndex]);
                pt.noteCount[(size_t) b][(size_t) h] = std::min ((int) notes.size(), midinja::kMaxChordNotes);
                for (int j = 0; j < pt.noteCount[(size_t) b][(size_t) h]; ++j)
                    pt.notes[(size_t) b][(size_t) h][(size_t) j] = juce::jlimit (0, 127, notes[(size_t) j]);
                ++h;
            }
            midinja::fitLengths (p);
        }
    }
    {
        const juce::SpinLock::ScopedLockType lock (stateLock);
        shared = m;
    }
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
    next.reset();
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

const midinja::Pattern& MidinjaProcessor::patternFor (const PlayTab& t, int bar) const
{
    const int b = juce::jlimit (0, audio.seqLen - 1, bar);
    return t.poly ? t.barPats[(size_t) b] : t.parts[(size_t) audio.seq[(size_t) b]];
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

    {
        const juce::SpinLock::ScopedTryLockType lock (stateLock);
        if (lock.isLocked() && seenVersion != version.load())
        {
            audio = shared;
            seenVersion = version.load();
            barIdx = juce::jlimit (0, audio.seqLen - 1, barIdx);
            for (int i = 0; i < kNumTabs; ++i)
                playPat[(size_t) i] = patternFor (audio.tabs[(size_t) i], barIdx);
        }
    }

    // The host's tempo and transport (VST3): the plugin plays when the host plays, from the host's
    // position, and stops with it. Its own Play button is for the standalone app.
    bool hostStart = false;
    double hostSteps = -1.0;
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
                if (hp)
                    if (const auto ppq = pos->getPpqPosition())
                        hostSteps = std::max (0.0, *ppq) * midinja::stepsPerBeat (rateOf (audio.rate));
            }
        }
    }

    const bool wantPlay = isHosted() ? hostPlaying.load() : playing.load();
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
            monoScratch.resize ((size_t) numSamples);
        std::fill (monoScratch.begin(), monoScratch.begin() + numSamples, 0.0f);
        right = monoScratch.data();
    }

    // Following the host position: the sequencers jump to the host's place in the loop when the
    // transport starts and whenever they have drifted more than half a step from it.
    const int cycleSteps = std::max (1, audio.seqLen * audio.numSteps);
    bool doSeek = false;
    double seekTo = 0.0;
    if (hostSteps >= 0.0 && wantPlay)
    {
        seekTo = std::fmod (hostSteps, (double) cycleSteps);
        const auto& sq0 = sequencers[0];
        const double here = sq0.isPlaying() ? std::fmod (sq0.position(), (double) cycleSteps) : -1.0;
        double diff = std::abs (here - seekTo);
        diff = std::min (diff, (double) cycleSteps - diff);
        doSeek = hostStart || ! sq0.isPlaying() || diff > 0.5;
    }
    const bool doRestart = restart.exchange (false) || (hostStart && ! doSeek);
    outBuffer.clear();
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
            pp = patternFor (pt, 0);
        }
        if (wantPlay && ! sq.isPlaying())
        {
            if (i == 0)
                barIdx = 0;
            pp = patternFor (pt, 0);
            sq.start();
        }
        else if (! wantPlay && sq.isPlaying())
        {
            sq.stop (events);
        }
        if (doSeek && sq.isPlaying())
        {
            sq.stop (events); // note-offs for what sounds, then the new place
            sq.start();
            sq.seek (seekTo);
            if (i == 0)
                barIdx = juce::jlimit (0, audio.seqLen - 1, (int) (seekTo / audio.numSteps));
            pp = patternFor (pt, barIdx);
        }
        sq.process (numSamples, useBpm, rateOf (audio.rate), audio.swing, pp, events,
                    [this, i] (midinja::Pattern& p) {
                        if (i == 0)
                            barIdx = (barIdx + 1) % audio.seqLen;
                        p = patternFor (audio.tabs[(size_t) i], barIdx);
                    });
        if (i == 0)
        {
            playStep = sq.currentStep();
            playBar = sq.isPlaying() ? barIdx : -1;
        }

        const int ch = pt.channel;
        const bool tabSynth = useSynth && pt.on && ! pt.drums; // no drum sound in the preview synth
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
                        break;
                    if (pt.poly)
                    {
                        const int key = e.data1, b = key / kHitsPerBar, h = key % kHitsPerBar;
                        auto& held = sounding[(size_t) key];
                        auto& heldN = soundingN[(size_t) key];
                        for (int j = 0; j < heldN; ++j)
                        {
                            if (tabSynth) ps.noteOff (held[(size_t) j]);
                            outBuffer.addEvent (juce::MidiMessage::noteOff (ch, held[(size_t) j]), at);
                        }
                        heldN = b < midinja::kMaxBars ? pt.noteCount[(size_t) b][(size_t) h] : 0;
                        for (int j = 0; j < heldN; ++j)
                        {
                            held[(size_t) j] = pt.notes[(size_t) b][(size_t) h][(size_t) j];
                            const int v = pt.vels[(size_t) b][(size_t) h][(size_t) j];
                            const int vel = v > 0 ? v : e.data2;
                            if (tabSynth) ps.noteOn (held[(size_t) j], vel);
                            outBuffer.addEvent (juce::MidiMessage::noteOn (ch, held[(size_t) j], (juce::uint8) vel), at);
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
                        auto& held = sounding[(size_t) e.data1];
                        auto& heldN = soundingN[(size_t) e.data1];
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
                    if (pt.poly)
                        for (auto& n : soundingN)
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
        // a chosen device gets everything (note-offs and CC 123 on stop included) and the host nothing
        const juce::SpinLock::ScopedLockType lock (midiOutLock); // held only for the swap in openMidiOut
        if (midiOut != nullptr)
            midiOut->sendBlockOfMessages (outBuffer, juce::Time::getMillisecondCounterHiRes() + 1.0, currentSampleRate);
        else
            midi.addEvents (outBuffer, 0, numSamples, 0);
    }
}

juce::AudioProcessorEditor* MidinjaProcessor::createEditor()
{
    return new MidinjaEditor (*this);
}

// ---------------------------------------------------------------------------------------------
// Saved state

void MidinjaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto& d = sess.doc;
    juce::XmlElement xml ("Midinja");
    xml.setAttribute ("format", 4);
    xml.setAttribute ("bpm", (double) bpm.load());
    xml.setAttribute ("synthOn", synthOn.load());
    xml.setAttribute ("wave", synthWave.load());
    xml.setAttribute ("cutoff", (double) cutoff.load());
    xml.setAttribute ("resonance", (double) resonance.load());
    xml.setAttribute ("decay", (double) decay.load());
    xml.setAttribute ("volume", (double) volume.load());
    xml.setAttribute ("midiOut", midiOutIdentifier);
    xml.setAttribute ("adv", sess.adv);
    xml.setAttribute ("doRhythm", sess.doRhythm);
    xml.setAttribute ("doNotes", sess.doNotes);
    xml.setAttribute ("mutateMode", sess.mutateMode);
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
    xml.setAttribute ("csel", d.csel);
    xml.setAttribute ("nextChordId", d.nextChordId);
    juce::StringArray arr;
    for (int k : d.arr)
        arr.add (juce::String (k));
    xml.setAttribute ("arr", arr.joinIntoString (","));

    for (const auto& c : d.chords)
    {
        auto* cx = xml.createNewChildElement ("chord");
        cx->setAttribute ("id", c.id);
        cx->setAttribute ("start", c.start);
        cx->setAttribute ("len", c.len);
        cx->setAttribute ("deg", c.deg);
        cx->setAttribute ("type", (int) c.type);
        cx->setAttribute ("inv", c.inv);
        cx->setAttribute ("lock", c.lock);
        cx->setAttribute ("rhythm", (int) c.rhythm);
    }
    for (int i = 0; i < kNumTabs; ++i)
    {
        const auto& t = d.tabs[(size_t) i];
        auto* tx = xml.createNewChildElement ("tab");
        tx->setAttribute ("channel", t.channel);
        tx->setAttribute ("on", t.on);
        tx->setAttribute ("rootNote", t.rootNote);
        tx->setAttribute ("low", t.lowNote);
        tx->setAttribute ("high", t.highNote);
        if (t.poly)
            continue;
        if (t.drums)
        {
            for (int k = 0; k < kNumParts; ++k)
            {
                const auto& p = t.beats[(size_t) k];
                auto* dx = tx->createNewChildElement ("drum");
                dx->setAttribute ("lock", t.locks[(size_t) k]);
                dx->setAttribute ("hat", p.hat);
                dx->setAttribute ("open", p.open);
                dx->setAttribute ("clap", p.clap);
                dx->setAttribute ("fill", p.fill);
                dx->setAttribute ("fillOn", p.fillOn);
                dx->setAttribute ("kickFill", p.kickFill);
                for (int r = 0; r < midinja::kNumDrums; ++r)
                {
                    juce::StringArray v;
                    for (const auto& h : p.hits[(size_t) r])
                        v.add (juce::String (h.on ? h.velocity : 0) + (h.locked ? "L" : ""));
                    dx->setAttribute ("r" + juce::String (r), v.joinIntoString (","));
                }
            }
            continue;
        }
        for (int k = 0; k < kNumParts; ++k)
        {
            auto* part = tx->createNewChildElement ("part");
            part->setAttribute ("lock", t.locks[(size_t) k]);
            part->setAttribute ("src", (int) t.src[(size_t) k]);
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
    if (xml == nullptr || ! xml->hasTagName ("Midinja") || xml->getIntAttribute ("format", 1) < 4)
        return; // an older state: start fresh

    bpm = (float) juce::jlimit (40.0, 240.0, xml->getDoubleAttribute ("bpm", 122.0));
    synthOn = xml->getBoolAttribute ("synthOn", false);
    synthWave = juce::jlimit (0, 1, xml->getIntAttribute ("wave", 0));
    cutoff = (float) xml->getDoubleAttribute ("cutoff", 1800.0);
    resonance = (float) xml->getDoubleAttribute ("resonance", 0.3);
    decay = (float) xml->getDoubleAttribute ("decay", 250.0);
    volume = (float) xml->getDoubleAttribute ("volume", 0.6);
    sess.adv = xml->getBoolAttribute ("adv", false);
    sess.doRhythm = xml->getBoolAttribute ("doRhythm", true);
    sess.doNotes = xml->getBoolAttribute ("doNotes", true);
    sess.mutateMode = xml->getBoolAttribute ("mutateMode", false);
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
    d.csel = xml->getIntAttribute ("csel", -1);
    d.nextChordId = std::max (1, xml->getIntAttribute ("nextChordId", 1));
    d.arr.clear();
    for (const auto& s : juce::StringArray::fromTokens (xml->getStringAttribute ("arr"), ",", ""))
        if (s.trim().isNotEmpty() && (int) d.arr.size() < midinja::kMaxBars)
            d.arr.push_back (juce::jlimit (0, kNumParts - 1, s.getIntValue()));
    d.sel = xml->getIntAttribute ("sel", -1);
    if (d.sel >= (int) d.arr.size())
        d.sel = -1;
    d.chords.clear();
    for (auto* cx : xml->getChildWithTagNameIterator ("chord"))
    {
        midinja::Chord c;
        c.id = cx->getIntAttribute ("id", d.nextChordId++);
        c.start = juce::jlimit (0, 15, cx->getIntAttribute ("start", 0));
        c.len = juce::jlimit (1, 16, cx->getIntAttribute ("len", 4));
        c.deg = juce::jlimit (0, 11, cx->getIntAttribute ("deg", 0));
        c.type = (midinja::ChordType) juce::jlimit (0, 5, cx->getIntAttribute ("type", 1));
        c.inv = juce::jlimit (0, 3, cx->getIntAttribute ("inv", 0));
        c.lock = cx->getBoolAttribute ("lock", false);
        c.rhythm = (midinja::Rhythm) juce::jlimit (0, midinja::kNumRhythms - 1, cx->getIntAttribute ("rhythm", 0));
        d.chords.push_back (c);
        d.nextChordId = std::max (d.nextChordId, c.id + 1);
    }
    int i = 0;
    for (auto* tx : xml->getChildWithTagNameIterator ("tab"))
    {
        if (i >= kNumTabs)
            break;
        auto& t = d.tabs[(size_t) i];
        t.channel = juce::jlimit (1, 16, tx->getIntAttribute ("channel", i + 1));
        t.on = tx->getBoolAttribute ("on", i == 0);
        t.rootNote = juce::jlimit (0, 127, tx->getIntAttribute ("rootNote", t.rootNote));
        t.lowNote = juce::jlimit (0, 127, tx->getIntAttribute ("low", t.lowNote));
        t.highNote = juce::jlimit (t.lowNote, 127, tx->getIntAttribute ("high", t.highNote));
        int part = 0;
        for (auto* dx : tx->getChildWithTagNameIterator ("drum"))
        {
            if (! t.drums || part >= kNumParts)
                break;
            t.locks[(size_t) part] = dx->getBoolAttribute ("lock");
            auto& p = t.beats[(size_t) part];
            p.hat = juce::jlimit (0, midinja::kNumHats - 1, dx->getIntAttribute ("hat", 0));
            p.open = dx->getIntAttribute ("open", 1) == 3 ? 3 : 1;
            p.clap = juce::jlimit (0, 1, dx->getIntAttribute ("clap", 0));
            p.fill = juce::jlimit (0, midinja::kNumFills - 1, dx->getIntAttribute ("fill", 0));
            p.fillOn = dx->getBoolAttribute ("fillOn", part > 0);
            p.kickFill = dx->getBoolAttribute ("kickFill", false);
            for (int r = 0; r < midinja::kNumDrums; ++r)
            {
                int s = 0;
                for (const auto& tok : juce::StringArray::fromTokens (dx->getStringAttribute ("r" + juce::String (r)), ",", ""))
                {
                    if (s >= midinja::kMaxSteps)
                        break;
                    auto& h = p.hits[(size_t) r][(size_t) s++];
                    h.locked = tok.endsWithChar ('L');
                    const int v = tok.trimCharactersAtEnd ("L").getIntValue();
                    h.on = v > 0;
                    h.velocity = juce::jlimit (1, 127, v > 0 ? v : 100);
                }
            }
            ++part;
        }
        part = 0;
        for (auto* px : tx->getChildWithTagNameIterator ("part"))
        {
            if (part >= kNumParts)
                break;
            t.locks[(size_t) part] = px->getBoolAttribute ("lock");
            t.src[(size_t) part] = (midinja::Src) juce::jlimit (0, 2, px->getIntAttribute ("src", 0));
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
    if (port.isNotEmpty() && ! openMidiOut (port))
        sess.say (isHosted() ? "The saved MIDI Out device is missing: MIDI goes to the host" : "The saved MIDI Out device is missing: no MIDI out", true);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MidinjaProcessor();
}
