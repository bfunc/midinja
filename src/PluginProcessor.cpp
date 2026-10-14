#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "core/MidiFileWriter.h"

MidinjaProcessor::MidinjaProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    midinja::generate (shared.pattern, shared.gen);
    events.reserve (512);
}

MidinjaProcessor::~MidinjaProcessor()
{
    sendAllNotesOffNow();
    openMidiOut ({});
}

// ---------------------------------------------------------------------------------------------
// UI side

MidinjaProcessor::State MidinjaProcessor::getState() const
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    return shared;
}

void MidinjaProcessor::editState (const std::function<void (State&)>& fn)
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    fn (shared);
    ++version;
}

void MidinjaProcessor::roll()
{
    const auto seed = (uint32_t) juce::Random::getSystemRandom().nextInt (0x7fffffff) + 1u;
    editState ([seed] (State& s) {
        s.gen.seed = seed;
        midinja::generate (s.pattern, s.gen);
    });
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
    const auto s = getState();
    const auto r = (midinja::Rate) rate.load();
    const auto notes = midinja::renderCycle (s.pattern, swing.load());
    const auto bytes = midinja::writeMidiFile (notes, midinja::stepsPerBeat (r), bpm.load(), channel.load(),
                                               "Midinja", (double) s.pattern.numSteps);
    return juce::MemoryBlock (bytes.data(), bytes.size());
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

    // Pick up UI edits, or hand an Infinity re-roll back to the UI.
    {
        const juce::SpinLock::ScopedTryLockType lock (stateLock);
        if (lock.isLocked())
        {
            if (seenVersion != version.load())
            {
                audio = shared; // the UI wins over a pending re-roll
                seenVersion = version.load();
                pendingWriteBack = false;
            }
            else if (pendingWriteBack)
            {
                shared = audio;
                seenVersion = ++version;
                pendingWriteBack = false;
            }
        }
    }

    events.clear();
    const bool wantPlay = playing.load();
    if (wantPlay && ! sequencer.isPlaying())
        sequencer.start();
    else if (! wantPlay && sequencer.isPlaying())
        sequencer.stop (events);

    sequencer.process (numSamples, (double) bpm.load(), (midinja::Rate) rate.load(), swing.load(),
                       audio.pattern, events, [this] (midinja::Pattern& p) {
                           if (! infinity.load())
                               return;
                           audio.gen.seed = audio.gen.seed * 1664525u + 1013904223u;
                           midinja::generate (p, audio.gen);
                           pendingWriteBack = true;
                       });
    playStep = sequencer.currentStep();

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
// Saved state (the standalone app keeps it between runs)

void MidinjaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto s = getState();
    juce::XmlElement xml ("Midinja");
    xml.setAttribute ("format", 1);
    xml.setAttribute ("bpm", (double) bpm.load());
    xml.setAttribute ("swing", (double) swing.load());
    xml.setAttribute ("rate", rate.load());
    xml.setAttribute ("channel", channel.load());
    xml.setAttribute ("infinity", infinity.load());
    xml.setAttribute ("synthOn", synthOn.load());
    xml.setAttribute ("wave", synthWave.load());
    xml.setAttribute ("cutoff", (double) cutoff.load());
    xml.setAttribute ("resonance", (double) resonance.load());
    xml.setAttribute ("decay", (double) decay.load());
    xml.setAttribute ("volume", (double) volume.load());
    xml.setAttribute ("midiOut", midiOutIdentifier);

    const auto& g = s.gen;
    xml.setAttribute ("numSteps", s.pattern.numSteps);
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
    xml.setAttribute ("seed", juce::String ((juce::int64) g.seed));

    for (int i = 0; i < midinja::kMaxSteps; ++i)
    {
        const auto& st = s.pattern.steps[(size_t) i];
        auto* e = xml.createNewChildElement ("step");
        e->setAttribute ("on", st.on);
        e->setAttribute ("note", st.note);
        e->setAttribute ("len", st.length);
        e->setAttribute ("vel", st.velocity);
        e->setAttribute ("rep", st.repeats);
        e->setAttribute ("lock", st.locked);
    }
    copyXmlToBinary (xml, destData);
}

void MidinjaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("Midinja"))
        return;

    bpm = (float) juce::jlimit (40.0, 240.0, xml->getDoubleAttribute ("bpm", 120.0));
    swing = (float) juce::jlimit (0.0, 1.0, xml->getDoubleAttribute ("swing", 0.0));
    rate = juce::jlimit (0, 3, xml->getIntAttribute ("rate", 1));
    channel = juce::jlimit (1, 16, xml->getIntAttribute ("channel", 1));
    infinity = xml->getBoolAttribute ("infinity", false);
    synthOn = xml->getBoolAttribute ("synthOn", true);
    synthWave = juce::jlimit (0, 1, xml->getIntAttribute ("wave", 0));
    cutoff = (float) xml->getDoubleAttribute ("cutoff", 1800.0);
    resonance = (float) xml->getDoubleAttribute ("resonance", 0.3);
    decay = (float) xml->getDoubleAttribute ("decay", 250.0);
    volume = (float) xml->getDoubleAttribute ("volume", 0.6);

    State s;
    auto& g = s.gen;
    s.pattern.numSteps = juce::jlimit (1, midinja::kMaxSteps, xml->getIntAttribute ("numSteps", 16));
    g.root = juce::jlimit (0, 11, xml->getIntAttribute ("root", g.root));
    g.scale = xml->getIntAttribute ("scale", g.scale);
    g.lowNote = xml->getIntAttribute ("low", g.lowNote);
    g.highNote = xml->getIntAttribute ("high", g.highNote);
    g.notes = xml->getIntAttribute ("notes", g.notes);
    g.lenMin = xml->getIntAttribute ("lenMin", g.lenMin);
    g.lenMax = xml->getIntAttribute ("lenMax", g.lenMax);
    g.velMin = xml->getIntAttribute ("velMin", g.velMin);
    g.velMax = xml->getIntAttribute ("velMax", g.velMax);
    g.repeatsOn = xml->getBoolAttribute ("repOn", false);
    g.repMin = xml->getIntAttribute ("repMin", g.repMin);
    g.repMax = xml->getIntAttribute ("repMax", g.repMax);
    g.repChance = (float) xml->getDoubleAttribute ("repChance", g.repChance);
    g.seed = (uint32_t) xml->getStringAttribute ("seed", "1").getLargeIntValue();

    int i = 0;
    for (auto* e : xml->getChildWithTagNameIterator ("step"))
    {
        if (i >= midinja::kMaxSteps)
            break;
        auto& st = s.pattern.steps[(size_t) i++];
        st.on = e->getBoolAttribute ("on");
        st.note = juce::jlimit (0, 127, e->getIntAttribute ("note", 60));
        st.length = juce::jlimit (1, midinja::kMaxLength, e->getIntAttribute ("len", 1));
        st.velocity = juce::jlimit (1, 127, e->getIntAttribute ("vel", 100));
        st.repeats = juce::jlimit (1, midinja::kMaxRepeats, e->getIntAttribute ("rep", 1));
        st.locked = e->getBoolAttribute ("lock");
    }
    editState ([&s] (State& target) { target = s; });

    const auto port = xml->getStringAttribute ("midiOut");
    if (port.isNotEmpty())
        openMidiOut (port); // stays closed if the device is gone
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MidinjaProcessor();
}
