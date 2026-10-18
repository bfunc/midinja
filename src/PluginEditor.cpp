#include "PluginEditor.h"
#include "core/Scales.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace
{
constexpr int kW = 1120, kH = 720;
constexpr int kRowA = 56, kRowB = 66, kFoot = 50, kSide = 156;

juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
const juce::String kDot = u8 (" \xC2\xB7 ");
const juce::String kArrow = u8 ("\xE2\x86\x92");

juce::String lenName (int l)
{
    switch (l)
    {
        case 1: return "1 step";
        case 2: return "2 steps";
        case 4: return "1 beat";
        case 8: return "2 beats";
        default: return juce::String (l) + " steps";
    }
}

// Notes from a Standard MIDI File, every track; ppq from the header.
bool readMidiFile (const juce::File& f, std::vector<midinja::RawNote>& out, double& ppq, juce::String& error)
{
    juce::FileInputStream in (f);
    if (! in.openedOk())
    {
        error = "could not open the file";
        return false;
    }
    juce::MidiFile mf;
    if (! mf.readFrom (in))
    {
        error = "not a MIDI file";
        return false;
    }
    if (mf.getTimeFormat() <= 0)
    {
        error = "SMPTE timing is not supported";
        return false;
    }
    ppq = (double) mf.getTimeFormat();
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (t));
        seq.updateMatchedPairs();
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            auto* ev = seq.getEventPointer (i);
            if (! ev->message.isNoteOn())
                continue;
            const double tick = ev->message.getTimeStamp();
            const double off = ev->noteOffObject != nullptr ? ev->noteOffObject->message.getTimeStamp() : tick + ppq;
            out.push_back ({ tick, std::max (1.0, off - tick), ev->message.getNoteNumber(), ev->message.getVelocity() });
        }
    }
    return true;
}
} // namespace

// ---------------------------------------------------------------------------------------------

MidinjaEditor::MidinjaEditor (MidinjaProcessor& p)
    : AudioProcessorEditor (&p), proc (p), sess (p.session())
{
    setLookAndFeel (&look);
    setWantsKeyboardFocus (true);

    // ---- Row A ----
    dice.onClick = [this] { sess.apply (sess.what); changed(); };
    addAndMakeVisible (dice);

    what.items = { { "Both", {}, "New rhythm and new notes" },
                   { u8 ("\xE2\x99\xA9 Rhythm"), {}, "New rhythm, keep the pitches" },
                   { u8 ("\xE2\x99\xAA Notes"), {}, "New pitches, keep the rhythm" },
                   { "~ Mutate", "15%", "Change some notes, keep the riff. Click the % to change how much" } };
    what.tooltip = "What the dice change";
    what.onSelect = [this] (int i) { sess.what = (midinja::Op) i; render(); };
    what.onSuffixClick = [this] (int) { sess.cycleMutAmt(); };
    addAndMakeVisible (what);

    rollAll.setTooltip (u8 ("Roll all four parts the same way: A, then A\xE2\x80\xB2 B C from it (Shift+Space). Locked parts stay"));
    rollAll.onClick = [this] { sess.applyAll (sess.what); changed(); };
    addAndMakeVisible (rollAll);

    versions.sess = &sess;
    versions.onGoto = [this] (int i) { sess.gotoVersion (i); changed(); };
    versions.onStar = [this] (int i) { sess.versions[(size_t) i].star = ! sess.versions[(size_t) i].star; versions.repaint(); };
    versions.onPrev = [this] { sess.histPrev(); versions.repaint(); };
    versions.onNext = [this] { sess.histNext(); versions.repaint(); };
    addAndMakeVisible (versions);

    undoB.setTooltip ("Undo (Ctrl+Z)");
    undoB.onClick = [this] { if (sess.canUndo()) { sess.undoTo (sess.ui - 1); changed(); } };
    redoB.setTooltip ("Redo (Ctrl+Shift+Z)");
    redoB.onClick = [this] { if (sess.canRedo()) { sess.undoTo (sess.ui + 1); changed(); } };
    addAndMakeVisible (undoB);
    addAndMakeVisible (redoB);

    // ---- Row B ----
    for (int i = 0; i < 12; ++i)
        rootBox.addItem (midinja::noteNames()[(size_t) i], i + 1);
    for (size_t i = 0; i < midinja::scales().size(); ++i)
        scaleBox.addItem (midinja::scales()[i].name, (int) i + 1);
    const auto rekey = [this] { sess.rekey (rootBox.getSelectedId() - 1, scaleBox.getSelectedId() - 1); changed(); };
    rootBox.onChange = rekey;
    scaleBox.onChange = rekey;
    addAndMakeVisible (rootBox);
    addAndMakeVisible (scaleBox);

    rateBox.addItem (u8 ("1/8 \xC2\xB7 8"), 2);
    rateBox.addItem (u8 ("1/16 \xC2\xB7 16"), 4);
    rateBox.addItem (u8 ("1/16T \xC2\xB7 12"), 6);
    rateBox.setTooltip ("One part is one bar of 4/4; the rate sets how many steps it has");
    rateBox.onChange = [this] { sess.setRate (rateBox.getSelectedId()); changed(); };
    addAndMakeVisible (rateBox);

    setupKnob (density, [this] { return (double) sess.doc.gen.notes; }, [this] (double v) { sess.doc.gen.notes = (int) v; },
               [this] (double v) { return juce::String (std::min ((int) v, sess.doc.numSteps())) + " / " + juce::String (sess.doc.numSteps()); },
               10.0, nullptr);
    density.setTooltip ("How many steps get a note (next roll). Ctrl+click resets");
    setupKnob (swingK, [this] { return (double) sess.doc.swing; }, [this] (double v) { sess.doc.swing = (float) v; },
               [] (double v) { return juce::String ((int) std::lround (v * 100)) + "%"; }, 0.18, [this] { proc.publish(); });
    setupKnob (similarity, [this] { return (double) sess.doc.gen.similarity; }, [this] (double v) { sess.doc.gen.similarity = (float) v; },
               [] (double v) { return juce::String ((int) std::lround (v * 100)) + "%"; }, 0.65, nullptr);
    similarity.setTooltip (u8 ("How close A\xE2\x80\xB2 stays to A (next roll of A\xE2\x80\xB2): high changes only the last beat, low keeps just the shape. Ctrl+click resets"));
    setupKnob (color, [this] { return (double) sess.doc.gen.color; }, [this] (double v) { sess.doc.gen.color = (float) v; },
               [this] (double v) { return colorLabel() + kDot + juce::String ((int) std::lround (v * 100)) + "%"; }, 0.4, nullptr);
    setupKnob (surprise, [this] { return (double) sess.doc.gen.surprise; }, [this] (double v) { sess.doc.gen.surprise = (float) v; },
               [] (double v) { return juce::String ((int) std::lround (v * 100)) + "%"; }, 0.15, nullptr);
    surprise.setTooltip ("How often the riff breaks the rules (next roll)");

    trD.setTooltip ("All four parts down a scale step");
    trD.onClick = [this] { sess.transposeAll (-1); changed(); };
    trU.setTooltip ("All four parts up a scale step");
    trU.onClick = [this] { sess.transposeAll (1); changed(); };
    trVal.setEditable (false, false, false);
    trVal.get = [this] { return (double) sess.doc.transpose; };
    addAndMakeVisible (trD);
    addAndMakeVisible (trVal);
    addAndMakeVisible (trU);

    viewSeg.items = { { "Riff", {}, "Only the pitches the riff uses" },
                      { "Scale", {}, "All notes of the scale in the pitch range" },
                      { "All", {}, "Every semitone in the pitch range" } };
    viewSeg.onSelect = [this] (int i) { sess.view = (midinja::View) i; render(); };
    addAndMakeVisible (viewSeg);

    advSw.setTooltip ("Ranges, velocity, length, ratchets, seed and the undo buttons");
    advSw.onClick = [this] { sess.adv = ! sess.adv; render(); };
    addAndMakeVisible (advSw);

    // ---- Side panel ----
    pitchR.get = [this] { return std::pair { sess.doc.gen.lowNote, sess.doc.gen.highNote }; };
    pitchR.set = [this] (int a, int b) { sess.doc.gen.lowNote = a; sess.doc.gen.highNote = std::max (b, a + 5); };
    pitchR.format = [] (int v) { return theme::noteName (v); };
    pitchR.onChange = [this] { render(); };
    lenR.get = [this] { return std::pair { sess.doc.gen.lenMin, sess.doc.gen.lenMax }; };
    lenR.set = [this] (int a, int b) { sess.doc.gen.lenMin = a; sess.doc.gen.lenMax = b; };
    lenR.format = [] (int v) { return lenName (v); };
    velR.get = [this] { return std::pair { sess.doc.gen.velMin, sess.doc.gen.velMax }; };
    velR.set = [this] (int a, int b) { sess.doc.gen.velMin = a; sess.doc.gen.velMax = b; };
    velR.format = [] (int v) { return juce::String (v); };
    repR.get = [this] { return std::pair { sess.doc.gen.repMin, sess.doc.gen.repMax }; };
    repR.set = [this] (int a, int b) { sess.doc.gen.repMin = a; sess.doc.gen.repMax = b; };
    repR.format = [] (int v) { return u8 ("\xC3\x97") + juce::String (v); };
    for (auto* r : { &pitchR, &lenR, &velR, &repR })
        addAndMakeVisible (*r);
    repPill.onClick = [this] { sess.doc.gen.repeatsOn = ! sess.doc.gen.repeatsOn; render(); };
    rootPill.setTooltip (u8 ("Every bar begins with the root of its chord on step 1: I for A and A\xE2\x80\xB2, IV for B, V for C (ii or vii when the scale has no fifth). Imported bars are left as they are"));
    rootPill.onClick = [this] { sess.doc.gen.startOnRoot = ! sess.doc.gen.startOnRoot; render(); };
    addAndMakeVisible (repPill);
    addAndMakeVisible (rootPill);
    seedBox.setTooltip ("The seed of the last roll. Type one and press Enter to roll the whole form from it");
    seedBox.get = [this] { return (double) sess.doc.gen.seed; };
    seedBox.set = [this] (double v) { sess.doc.gen.seed = (uint32_t) std::llround (v); if (sess.doc.gen.seed == 0) sess.doc.gen.seed = 1; };
    seedBox.onChange = [this] { sess.applyAll (midinja::Op::Roll, true); changed(); };
    addAndMakeVisible (seedBox);

    // ---- Grid area ----
    parts.sess = &sess;
    parts.onSelectSlot = [this] (int i) { sess.selectSlot (i); proc.restartPlayback(); changed(); };
    parts.onToggleLock = [this] (int k) { sess.toggleLock (k); changed(); };
    parts.onRemoveBar = [this] (int i) { sess.removeBar (i); changed(); };
    parts.onAddBar = [this] (int k) { sess.addBar (k); changed(); };
    addAndMakeVisible (parts);

    grid.sess = &sess;
    grid.onEdit = [this] { edited(); };
    grid.onPreview = [this] { preview(); };
    addAndMakeVisible (grid);
    for (auto* l : { &velLane, &lenLane, &repLane })
    {
        l->sess = &sess;
        l->onEdit = [this] { edited(); };
        l->onPreview = [this] { preview(); };
        addAndMakeVisible (*l);
    }

    // ---- Footer ----
    play.setTooltip ("Play / Stop: loops the arrangement, or only the selected bar");
    play.onClick = [this] {
        const bool start = ! proc.playing.load();
        if (start)
            proc.restartPlayback();
        proc.playing = start;
        render();
    };
    addAndMakeVisible (play);

    bpmBox.get = [this] { return (double) proc.bpm.load(); };
    bpmBox.set = [this] (double v) { proc.bpm = (float) v; };
    addAndMakeVisible (bpmBox);

    synthSw.setTooltip ("Built-in synth through the audio output");
    synthSw.onClick = [this] { proc.synthOn = ! proc.synthOn.load(); render(); };
    addAndMakeVisible (synthSw);
    waveBtn.setTooltip ("Waveform of the built-in synth");
    waveBtn.onClick = [this] { proc.synthWave = proc.synthWave.load() == 0 ? 1 : 0; render(); };
    addAndMakeVisible (waveBtn);
    const auto mini = [this] (ui::Knob& k, std::function<double()> get, std::function<void (double)> set,
                              std::function<juce::String (double)> fmt, double def) {
        setupKnob (k, std::move (get), std::move (set), std::move (fmt), def, nullptr);
        k.showText = false;
    };
    mini (cutoffK, [this] { return (double) proc.cutoff.load(); }, [this] (double v) { proc.cutoff = (float) v; },
          [] (double v) { return juce::String ((int) v) + " Hz"; }, 1800.0);
    mini (resoK, [this] { return (double) proc.resonance.load() * 100.0; }, [this] (double v) { proc.resonance = (float) v / 100.0f; },
          [] (double v) { return juce::String ((int) v) + "%"; }, 30.0);
    mini (decayK, [this] { return (double) proc.decay.load(); }, [this] (double v) { proc.decay = (float) v; },
          [] (double v) { return juce::String ((int) v) + " ms"; }, 250.0);
    mini (volK, [this] { return (double) proc.volume.load() * 100.0; }, [this] (double v) { proc.volume = (float) v / 100.0f; },
          [] (double v) { return juce::String ((int) v) + "%"; }, 60.0);
    for (auto* k : { &cutoffK, &resoK, &decayK, &volK })
        k->onChange = [this, k] { k->setTooltip (k->getName() + ": " + k->format (k->get()) + ". Drag or scroll; Ctrl+click resets"); };

    midiOutBox.setTooltip ("A system MIDI port: a hardware synth, loopMIDI, IAC");
    midiOutBox.onChange = [this] {
        const int idx = midiOutBox.getSelectedId() - 2; // id 1 = None
        const auto id = idx >= 0 && idx < midiPortIds.size() ? midiPortIds[idx] : juce::String();
        if (! proc.openMidiOut (id))
        {
            sess.message = "Can't open " + midiOutBox.getText().toStdString() + " (in use by another app?)";
            refreshMidiPorts();
            render();
        }
    };
    midiOutBox.addMouseListener (this, false); // rescan the ports when the list is opened
    addAndMakeVisible (midiOutBox);
    midiOutBox.setVisible (! proc.isHosted()); // in a host the MIDI goes through the host's routing only
    for (int i = 1; i <= 16; ++i)
        channelBox.addItem ("Ch " + juce::String (i), i);
    channelBox.setTooltip ("MIDI channel of the notes: live MIDI out, Export and Drag to DAW");
    channelBox.onChange = [this] { proc.setChannel (channelBox.getSelectedId()); };
    addAndMakeVisible (channelBox);

    importB.setTooltip (u8 ("Import a .mid file (or drop one on the window): up to 4 bars become A A\xE2\x80\xB2 B C"));
    importB.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> ("Import MIDI", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.mid;*.midi");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) importFile (fc.getResult()); });
    };
    exportB.setTooltip ("Save the arrangement as one .mid file");
    exportB.onClick = [this] { exportMidi(); };
    dragB.setTooltip ("Drag this onto your DAW (FL Studio: the playlist or a channel) to drop the arrangement there as a .mid file");
    dragB.onClick = [this] { sess.message = "Drag the handle onto the DAW: the arrangement lands there as a .mid file"; render(); };
    dragB.onDragStart = [this] {
        const auto f = writeTempMidi();
        if (f == juce::File())
            return;
        dragging = true;
        performExternalDragDropOfFiles ({ f.getFullPathName() }, false, &dragB, [this] { dragging = false; });
    };
    addAndMakeVisible (importB);
    addAndMakeVisible (exportB);
    addAndMakeVisible (dragB);

    // Keys go to the editor, not to the buttons.
    for (auto* c : getChildren())
    {
        c->setWantsKeyboardFocus (false);
        c->setMouseClickGrabsKeyboardFocus (false);
    }
    seedBox.setWantsKeyboardFocus (true);
    bpmBox.setWantsKeyboardFocus (true);

    // Old drag files (the DAW keeps reading the one it got, so they are not removed right away).
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Midinja");
    for (const auto& f : tmp.findChildFiles (juce::File::findFiles, false, "*.mid"))
        if (f.getLastModificationTime() < juce::Time::getCurrentTime() - juce::RelativeTime::days (1))
            f.deleteFile();

    setSize (kW, kH);
    refreshMidiPorts();
    render();
    startTimerHz (30);
}

MidinjaEditor::~MidinjaEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MidinjaEditor::setupKnob (ui::Knob& k, std::function<double()> get, std::function<void (double)> set,
                               std::function<juce::String (double)> fmt, double def, std::function<void()> onChange)
{
    k.get = std::move (get);
    k.set = std::move (set);
    k.format = std::move (fmt);
    k.resetValue = def;
    k.onChange = std::move (onChange);
    addAndMakeVisible (k);
}

// ---------------------------------------------------------------------------------------------

void MidinjaEditor::edited()
{
    sess.push();
    changed();
}

void MidinjaEditor::changed()
{
    proc.publish();
    render();
}

void MidinjaEditor::preview()
{
    proc.publish();
    grid.refresh();
    resized();
    velLane.repaint();
    lenLane.repaint();
    repLane.repaint();
    parts.repaint();
    versions.repaint();
}

juce::String MidinjaEditor::colorLabel() const
{
    const auto mi = midinja::modeInfo (sess.doc.gen.scale);
    juce::StringArray out;
    std::set<int> used;
    for (int pc : mi.colors)
    {
        if (used.count (pc))
            continue;
        bool found = false;
        for (const auto& mv : mi.moves)
            if (mv.first == pc || mv.second == pc)
            {
                out.add (u8 (midinja::degreeName (mv.first)) + kArrow + u8 (midinja::degreeName (mv.second)));
                used.insert (mv.first);
                used.insert (mv.second);
                found = true;
                break;
            }
        if (! found)
        {
            out.add (u8 (midinja::degreeName (pc)));
            used.insert (pc);
        }
    }
    return out.isEmpty() ? juce::String ("none") : out.joinIntoString (" ");
}

juce::String MidinjaEditor::colorWords() const
{
    const auto& sc = midinja::scales()[(size_t) sess.doc.gen.scale];
    const auto mi = midinja::modeInfo (sess.doc.gen.scale);
    if (mi.colors.empty())
        return juce::String (sc.name) + " has no colour note";
    juce::StringArray cs;
    for (int pc : mi.colors)
        cs.add (u8 (midinja::degreeName (pc)));
    juce::String t = "Colour notes of " + juce::String (sc.name) + ": " + cs.joinIntoString (", ") + " (the first gets about half the colour)";
    if (! mi.moves.empty())
    {
        juce::StringArray ms;
        for (const auto& mv : mi.moves)
            ms.add (u8 (midinja::degreeName (mv.first)) + kArrow + u8 (midinja::degreeName (mv.second)));
        t += juce::String (". Signature move") + (mi.moves.size() > 1 ? "s" : "") + ": " + ms.joinIntoString (", ")
           + ", played as two notes in a row into a strong step";
    }
    return t + ". The knob is the total amount of colour on the next roll. Ctrl+click resets";
}

void MidinjaEditor::render()
{
    shownVersion = proc.publishedVersion();
    const auto& d = sess.doc;
    const bool adv = sess.adv;
    const bool hosted = proc.isHosted() && proc.hostBpm.load() > 0.0f;

    dice.setEnabled (! d.locks[(size_t) d.cur]);
    static const char* whatTips[] = { "Roll the part shown: new rhythm and notes", "Roll a new rhythm for the part shown, keep its pitches",
                                      "Roll new pitches for the part shown, keep its rhythm", "Mutate the part shown: change some notes, keep the riff" };
    dice.setTooltip (d.locks[(size_t) d.cur] ? u8 (midinja::partName (d.cur)) + " is locked: unlock it on its bar to roll"
                                              : juce::String (whatTips[(int) sess.what]) + " (Space). Locked notes stay");
    what.on = (int) sess.what;
    what.items[3].suffix = juce::String ((int) std::lround (sess.mutAmt * 100)) + "%";
    what.repaint();
    undoB.setEnabled (sess.canUndo());
    redoB.setEnabled (sess.canRedo());
    undoB.setVisible (adv);
    redoB.setVisible (adv);
    versions.repaint();

    rootBox.setSelectedId (d.gen.root + 1, juce::dontSendNotification);
    scaleBox.setSelectedId (d.gen.scale + 1, juce::dontSendNotification);
    rateBox.setSelectedId (d.rate, juce::dontSendNotification);
    color.setTooltip (colorWords());
    for (auto* k : { &density, &swingK, &similarity, &color, &surprise, &cutoffK, &resoK, &decayK, &volK })
        k->repaint();
    for (auto* k : { &cutoffK, &resoK, &decayK, &volK })
        k->setTooltip (k->getName() + ": " + k->format (k->get()) + ". Drag or scroll; Ctrl+click resets");
    trVal.setText ((d.transpose > 0 ? "+" : "") + juce::String (d.transpose), juce::dontSendNotification);
    viewSeg.on = (int) sess.view;
    viewSeg.repaint();
    advSw.on = adv;
    advSw.repaint();

    for (auto* c : std::initializer_list<juce::Component*> { &pitchR, &lenR, &velR, &repR, &repPill, &rootPill, &seedBox, &velLane, &lenLane, &repLane })
        c->setVisible (adv);
    repR.dimmed = ! d.gen.repeatsOn;
    repPill.on = d.gen.repeatsOn;
    rootPill.on = d.gen.startOnRoot;
    for (auto* c : std::initializer_list<juce::Component*> { &pitchR, &lenR, &velR, &repR, &repPill, &rootPill })
        c->repaint();
    seedBox.refresh();

    const bool playingNow = proc.isPlayingNow();
    const auto seq = d.playSeq();
    const int bar = juce::jlimit (0, (int) seq.size() - 1, proc.currentBar());
    parts.playingSlot = ! playingNow || d.arr.empty() ? -1 : d.sel >= 0 ? d.sel : bar;
    parts.repaint();
    grid.playStep = playingNow && seq[(size_t) bar] == d.cur ? proc.currentStep() : -1;
    grid.refresh();
    for (auto* l : { &velLane, &lenLane, &repLane })
        l->repaint();

    play.setToggleState (proc.playing.load() || (proc.isHosted() && proc.hostPlaying.load()), juce::dontSendNotification);
    play.repaint();
    bpmBox.setEnabled (! hosted);
    bpmBox.setTooltip (hosted ? "Tempo from the host" : "Tempo. Click to type, scroll to nudge");
    if (hosted)
        bpmBox.setText (juce::String ((int) std::lround (proc.hostBpm.load())), juce::dontSendNotification);
    else
        bpmBox.refresh();
    synthSw.on = proc.synthOn.load();
    synthSw.repaint();
    waveBtn.setButtonText (proc.synthWave.load() == 1 ? "Square" : "Saw");
    for (auto* k : { &cutoffK, &resoK, &decayK, &volK })
        k->setEnabled (proc.synthOn.load());
    channelBox.setSelectedId (proc.channel.load(), juce::dontSendNotification);

    resized();
    repaint();
}

// ---------------------------------------------------------------------------------------------

void MidinjaEditor::timerCallback()
{
    if (proc.publishedVersion() != shownVersion)
    {
        render(); // a state restore from the host
        return;
    }
    const auto& d = sess.doc;
    const bool playingNow = proc.isPlayingNow();
    const auto seq = d.playSeq();
    const int bar = juce::jlimit (0, (int) seq.size() - 1, proc.currentBar());
    // The view follows the playing bar while no bar is selected.
    if (playingNow && d.sel < 0 && ! d.arr.empty() && seq[(size_t) bar] != d.cur)
    {
        sess.selectPart (seq[(size_t) bar]);
        render();
        return;
    }
    const int step = playingNow && seq[(size_t) bar] == d.cur ? proc.currentStep() : -1;
    const int slot = ! playingNow || d.arr.empty() ? -1 : d.sel >= 0 ? d.sel : bar;
    bool dirty = false;
    if (step != grid.playStep)
    {
        grid.playStep = step;
        grid.repaint();
        repaint (rPlayRow);
        dirty = true;
    }
    if (slot != parts.playingSlot)
    {
        parts.playingSlot = slot;
        parts.repaint();
    }
    const bool shownPlaying = play.getToggleState();
    const bool wantPlaying = proc.playing.load() || (proc.isHosted() && proc.hostPlaying.load());
    if (shownPlaying != wantPlaying || (proc.isHosted() && bpmBox.isEnabled() == (proc.hostBpm.load() > 0.0f)))
        render();
    else if (proc.isHosted() && proc.hostBpm.load() > 0.0f && ! dirty)
        bpmBox.setText (juce::String ((int) std::lround (proc.hostBpm.load())), juce::dontSendNotification);

    if (! proc.isHosted() && ++portTick >= 90) // every 3 s: the MIDI port list follows plugging and unplugging
    {
        portTick = 0;
        juce::StringArray ids;
        for (const auto& dev : proc.midiOutputs())
            ids.add (dev.identifier);
        if (ids != midiPortIds && juce::Component::getCurrentlyModalComponent() == nullptr)
            refreshMidiPorts();
    }

    // Space, Ctrl+Z and the number keys work without clicking the window first.
    if (auto* peer = getPeer())
        if (peer->isFocused() && ! hasKeyboardFocus (true) && ! dragging && juce::Component::getCurrentlyModalComponent() == nullptr)
            grabKeyboardFocus();
}

bool MidinjaEditor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    if (k.getKeyCode() == juce::KeyPress::spaceKey)
    {
        if (mods.isShiftDown()) sess.applyAll (sess.what);
        else sess.apply (sess.what);
        changed();
        return true;
    }
    // Ctrl+Z arrives as the key code Z, or as the control character 26 when the scan code is missing
    const bool isZ = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) k.getKeyCode()) == 'z' || k.getKeyCode() == 26 || k.getTextCharacter() == 26;
    if (mods.isCommandDown() && isZ)
    {
        if (mods.isShiftDown()) redoB.triggerClick();
        else undoB.triggerClick();
        return true;
    }
    if (! mods.isCommandDown() && ! mods.isAltDown() && k.getTextCharacter() >= '1' && k.getTextCharacter() <= '4')
    {
        sess.selectPart ((int) (k.getTextCharacter() - '1'));
        render();
        return true;
    }
    if (k.getKeyCode() == juce::KeyPress::escapeKey && sess.doc.sel >= 0)
    {
        sess.selectSlot (sess.doc.sel);
        proc.restartPlayback();
        changed();
        return true;
    }
    return false;
}

void MidinjaEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.eventComponent == &midiOutBox || midiOutBox.isParentOf (e.eventComponent))
        refreshMidiPorts();
}

// ---------------------------------------------------------------------------------------------

bool MidinjaEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi"))
            return true;
    return false;
}

void MidinjaEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi"))
        {
            importFile (juce::File (f));
            return;
        }
    repaint();
}

void MidinjaEditor::importFile (const juce::File& f)
{
    std::vector<midinja::RawNote> raw;
    double ppq = 480.0;
    juce::String err;
    if (! readMidiFile (f, raw, ppq, err))
    {
        sess.message = ("Could not read " + f.getFileName() + ": " + err).toStdString();
        render();
        return;
    }
    sess.importMidi (raw, ppq, f.getFileName().toStdString());
    changed();
}

void MidinjaEditor::exportMidi()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Midinja");
    dir.createDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Export MIDI", dir.getChildFile (juce::String (sess.exportName()) + ".mid"), "*.mid");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              file = file.withFileExtension ("mid");
                              const auto data = proc.exportMidi();
                              sess.message = (file.replaceWithData (data.getData(), data.getSize()) ? "Saved " : "Could not save ")
                                           + file.getFullPathName().toStdString();
                              render();
                          });
}

juce::File MidinjaEditor::writeTempMidi()
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Midinja");
    dir.createDirectory();
    const auto file = dir.getChildFile (juce::String (sess.exportName()) + "-" + juce::String ((juce::int64) sess.doc.gen.seed) + ".mid");
    const auto data = proc.exportMidi();
    if (! file.replaceWithData (data.getData(), data.getSize()))
    {
        sess.message = "Could not write the .mid file to drag";
        render();
        return {};
    }
    return file;
}

void MidinjaEditor::refreshMidiPorts()
{
    const auto current = proc.midiOutId();
    midiOutBox.clear (juce::dontSendNotification);
    midiPortIds.clear();
    midiOutBox.addItem ("None", 1);
    int selected = 1;
    const auto ports = proc.midiOutputs();
    for (int i = 0; i < ports.size(); ++i)
    {
        midiOutBox.addItem (ports[i].name, i + 2);
        midiPortIds.add (ports[i].identifier);
        if (ports[i].identifier == current)
            selected = i + 2;
    }
    midiOutBox.setSelectedId (selected, juce::dontSendNotification);
}

// ---------------------------------------------------------------------------------------------
// Layout

void MidinjaEditor::resized()
{
    const int W = getWidth(), H = getHeight();
    const bool adv = sess.adv;
    labels.clear();
    vseps.clear();
    sideTitles.clear();

    // ---- Row A: logo, preset, dice, selector, All, versions, undo / redo ----
    rRowA = { 0, 0, W, kRowA };
    int x = 14;
    const auto logoFont = theme::font (18.0f, true);
    rLogo = { x, 0, (int) theme::textWidth (logoFont, "Midinja") + 2, kRowA };
    x = rLogo.getRight() + 8;
    rPreset = { x, 8, 22 + 3 + 80 + 3 + 22, kRowA - 8 };
    labels.push_back ({ { x, 8, rPreset.getWidth(), 12 }, "Preset" });
    x = rPreset.getRight() + 8;
    vseps.push_back ({ x, 8, 1, kRowA - 16 });
    x += 1 + 8;
    dice.setBounds (x, (kRowA - 38) / 2, 38, 38);
    x = dice.getRight() + 6;
    what.setBounds (x, (kRowA - 26) / 2, what.preferredWidth(), 26);
    x = what.getRight() + 6;
    rollAll.setBounds (x, (kRowA - 26) / 2, rollAll.preferredWidth(), 26);
    int rx = W - 14;
    if (adv)
    {
        redoB.setBounds (rx - 30, (kRowA - 30) / 2, 30, 30);
        undoB.setBounds (rx - 62, (kRowA - 30) / 2, 30, 30);
        rx -= 62 + 8;
    }
    versions.setBounds (rx - VersionsStrip::width, 21, VersionsStrip::width, 30);
    labels.push_back ({ { versions.getX() + 4, 8, VersionsStrip::width, 12 }, "Versions" });

    // ---- Row B: key, rate, knobs, transpose, colour, view, advanced ----
    rRowB = { 0, kRowA, W, kRowB };
    const int lblY = kRowA + 7, ctlY = lblY + 15, ctlH = 28;
    const int knobY = kRowA + 7, knobH = 43;
    rx = W - 14;
    advSw.setBounds (rx - advSw.preferredWidth(), kRowA + (kRowB - 20) / 2, advSw.preferredWidth(), 20);
    rx = advSw.getX() - 10;
    viewSeg.setBounds (rx - viewSeg.preferredWidth(), ctlY, viewSeg.preferredWidth(), 26);
    labels.push_back ({ { viewSeg.getX(), lblY, viewSeg.getWidth(), 12 }, "View" });
    const int rowBEnd = viewSeg.getX() - 8;
    // Two passes: the natural spacing first; if the knobs run into View, tighter gaps and a
    // shorter Color label.
    for (int pass = 0; pass < 2; ++pass)
    {
        const int gap = pass == 0 ? 10 : 6, scaleW = pass == 0 ? 124 : 112, colorMax = pass == 0 ? 150 : 104;
        const size_t nLabels = labels.size(), nSeps = vseps.size();
        x = 14;
        labels.push_back ({ { x, lblY, 100, 12 }, "Key" });
        rootBox.setBounds (x, ctlY, 48, ctlH);
        scaleBox.setBounds (rootBox.getRight() + 4, ctlY, scaleW, ctlH);
        x = scaleBox.getRight() + gap;
        labels.push_back ({ { x, lblY, 120, 12 }, u8 ("Rate \xC2\xB7 steps/bar") });
        rateBox.setBounds (x, ctlY, 82, ctlH);
        x = rateBox.getRight() + gap;
        vseps.push_back ({ x, kRowA + 8, 1, kRowB - 16 });
        x += 1 + gap;
        for (auto* k : { &density, &swingK, &similarity })
        {
            k->setBounds (x, knobY, k->preferredWidth(), knobH);
            x = k->getRight() + gap;
        }
        labels.push_back ({ { x, lblY, 100, 12 }, "Transpose" });
        trD.setBounds (x, ctlY, 24, ctlH);
        trVal.setBounds (trD.getRight() + 3, ctlY, 40, ctlH);
        trU.setBounds (trVal.getRight() + 3, ctlY, 24, ctlH);
        x = trU.getRight() + gap;
        vseps.push_back ({ x, kRowA + 8, 1, kRowB - 16 });
        x += 1 + gap + 2;
        color.setBounds (x, knobY, std::min (color.preferredWidth(), colorMax), knobH);
        x = color.getRight() + gap + 2;
        surprise.setBounds (x, knobY, surprise.preferredWidth(), knobH);
        if (surprise.getRight() <= rowBEnd || pass == 1)
            break;
        labels.resize (nLabels);
        vseps.resize (nSeps);
    }

    // ---- Footer ----
    rFoot = { 0, H - kFoot, W, kFoot };
    const int fy = H - kFoot;
    x = 14;
    play.setBounds (x, fy + 8, 34, 34);
    x = play.getRight() + 12;
    labels.push_back ({ { x, fy + 17, 28, 16 }, "BPM" });
    bpmBox.setBounds (x + 30, fy + 12, 54, 26);
    x = bpmBox.getRight() + 12;
    vseps.push_back ({ x, fy + 8, 1, kFoot - 16 });
    x += 1 + 12;
    synthSw.setBounds (x, fy + 15, synthSw.preferredWidth(), 20);
    x = synthSw.getRight() + 8;
    waveBtn.setBounds (x, fy + 12, 58, 26);
    x = waveBtn.getRight() + 10;
    for (auto* k : { &cutoffK, &resoK, &decayK, &volK })
    {
        k->setBounds (x, fy + 13, 24, 24);
        x = k->getRight() + 8;
    }
    rx = W - 14;
    dragB.setBounds (rx - dragB.preferredWidth(), fy + 12, dragB.preferredWidth(), 26);
    rx = dragB.getX() - 8;
    exportB.setBounds (rx - exportB.preferredWidth(), fy + 12, exportB.preferredWidth(), 26);
    rx = exportB.getX() - 8;
    importB.setBounds (rx - importB.preferredWidth(), fy + 12, importB.preferredWidth(), 26);
    rx = importB.getX() - 12;
    vseps.push_back ({ rx, fy + 8, 1, kFoot - 16 });
    rx -= 12;
    channelBox.setBounds (rx - 64, fy + 12, 64, 26);
    rx = channelBox.getX() - 6;
    if (! proc.isHosted()) // the VST3 has no port of its own
    {
        const int midiW = std::max (90, std::min (170, rx - x - 60));
        midiOutBox.setBounds (rx - midiW, fy + 12, midiW, 26);
        labels.push_back ({ { midiOutBox.getX() - 58, fy + 17, 54, 16 }, "MIDI Out" });
    }

    // ---- Main: side panel and the grid area ----
    const int mainY = kRowA + kRowB, mainH = fy - mainY;
    rSide = adv ? juce::Rectangle<int> (0, mainY, kSide, mainH) : juce::Rectangle<int>();
    if (adv)
    {
        int y = mainY + 12;
        const int sx = 12, sw = kSide - 24;
        const auto block = [&] (const juce::String& title, ui::RangeSlider& r, ui::Pill* pill) {
            sideTitles.push_back ({ { sx, y, pill != nullptr ? sw - 40 : sw, 12 }, title });
            if (pill != nullptr)
                pill->setBounds (sx + sw - 34, y - 2, 34, 15);
            r.setBounds (sx, y + 12, sw, ui::RangeSlider::height);
            y += 12 + ui::RangeSlider::height + 13;
        };
        block ("Pitch range", pitchR, nullptr);
        block ("Length", lenR, nullptr);
        block ("Velocity", velR, nullptr);
        block ("Ratchets", repR, &repPill);
        sideTitles.push_back ({ { sx, y, sw - 40, 24 }, "Start on chord root" });
        rootPill.setBounds (sx + sw - 34, y + 4, 34, 15);
        y += 24 + 13;
        sideTitles.push_back ({ { sx, y, sw, 12 }, "Seed" });
        seedBox.setBounds (sx, y + 15, sw, 26);
    }
    const int gx = (adv ? kSide : 0) + 12, gw = W - gx - 12;
    int y = mainY + 8;
    parts.setBounds (gx, y, gw, PartsBar::height);
    y = parts.getBottom() + 6;
    const bool hasMsg = ! sess.message.empty();
    rMsg = hasMsg ? juce::Rectangle<int> (gx, y - 2, gw, 15) : juce::Rectangle<int>();
    if (hasMsg)
        y += 17;
    const int lanesH = adv ? velLane.preferredHeight() + 5 + lenLane.preferredHeight() + 5 + repLane.preferredHeight() + 6 : 0;
    const int legendH = 46, stepsH = 14, playH = 4 + 10;
    const int budget = fy - 10 - legendH - stepsH - lanesH - playH - y;
    const int gh = grid.layoutHeight (budget);
    grid.setBounds (gx, y, gw, gh);
    y = grid.getBottom() + 4;
    rPlayRow = { gx, y, gw, 4 };
    y += 4 + 6;
    if (adv)
    {
        velLane.setBounds (gx, y, gw, velLane.preferredHeight());
        y = velLane.getBottom() + 5;
        lenLane.setBounds (gx, y, gw, lenLane.preferredHeight());
        y = lenLane.getBottom() + 5;
        repLane.setBounds (gx, y, gw, repLane.preferredHeight());
        y = repLane.getBottom() + 6;
    }
    rSteps = { gx, y, gw, stepsH };
    y += stepsH + 4;
    rLegend = { gx, y, gw, legendH };
}

// ---------------------------------------------------------------------------------------------
// Painting the parts of the layout that are not components

void MidinjaEditor::paint (juce::Graphics& g)
{
    const auto& d = sess.doc;
    g.fillAll (theme::bg);

    // Row A
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff22262f), 0.0f, 0.0f, juce::Colour (0xff1b1e25), 0.0f, (float) kRowA, false));
    g.fillRect (rRowA);
    g.setColour (juce::Colours::black);
    g.fillRect (0, kRowA - 1, getWidth(), 1);
    g.setFont (theme::font (18.0f, true));
    g.setColour (theme::ink);
    const float midiW = theme::textWidth (theme::font (18.0f, true), "Midi");
    g.drawText ("Midi", rLogo, juce::Justification::centredLeft);
    g.setColour (theme::accent);
    g.drawText ("nja", rLogo.withTrimmedLeft ((int) midiW), juce::Justification::centredLeft);
    {
        // Presets come later: drawn inert, as in the preview.
        juce::Graphics::ScopedSaveState ss (g);
        g.setOpacity (0.45f);
        const int py = rPreset.getY() + 15;
        g.setColour (theme::ink2);
        juce::Path l, r;
        l.addTriangle ((float) rPreset.getX() + 15.0f, (float) py + 9.5f, (float) rPreset.getX() + 15.0f, (float) py + 18.5f, (float) rPreset.getX() + 8.0f, (float) py + 14.0f);
        r.addTriangle ((float) rPreset.getRight() - 15.0f, (float) py + 9.5f, (float) rPreset.getRight() - 15.0f, (float) py + 18.5f, (float) rPreset.getRight() - 8.0f, (float) py + 14.0f);
        g.fillPath (l);
        g.fillPath (r);
        const auto box = juce::Rectangle<float> ((float) rPreset.getX() + 25.0f, (float) py, 80.0f, 28.0f);
        g.setColour (theme::panel2);
        g.fillRoundedRectangle (box, 7.0f);
        g.setColour (theme::line);
        g.drawRoundedRectangle (box.reduced (0.5f), 7.0f, 1.0f);
        g.setColour (theme::ink);
        g.setFont (theme::font (12.0f));
        g.drawText ("Init", box, juce::Justification::centred);
    }

    // Row B
    g.setColour (theme::panel);
    g.fillRect (rRowB);
    g.setColour (theme::line);
    g.fillRect (0, rRowB.getBottom() - 1, getWidth(), 1);

    // Side panel
    if (! rSide.isEmpty())
    {
        g.setColour (theme::panel);
        g.fillRect (rSide);
        g.setColour (theme::line);
        g.fillRect (rSide.getRight() - 1, rSide.getY(), 1, rSide.getHeight());
        for (const auto& t : sideTitles)
            theme::drawLabel (g, t.s, t.r);
    }

    // Footer
    g.setColour (theme::foot);
    g.fillRect (rFoot);
    g.setColour (juce::Colours::black);
    g.fillRect (0, rFoot.getY(), getWidth(), 1);

    for (const auto& t : labels)
        theme::drawLabel (g, t.s, t.r);
    g.setColour (theme::line);
    for (const auto& v : vseps)
        g.fillRect (v);

    // the message line
    if (! rMsg.isEmpty())
    {
        g.setColour (theme::teal);
        g.setFont (theme::font (11.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 (sess.message.c_str())), rMsg, juce::Justification::centredLeft, true);
    }

    // playhead row and step numbers
    const int n = std::max (1, d.shown().numSteps);
    const float cw = (float) (rSteps.getWidth() - theme::keyWidth) / (float) n;
    if (grid.playStep >= 0 && grid.playStep < n)
    {
        const auto cell = juce::Rectangle<float> ((float) (rPlayRow.getX() + theme::keyWidth) + (float) grid.playStep * cw, (float) rPlayRow.getY(), cw, 4.0f);
        g.setColour (theme::teal.withAlpha (0.35f));
        g.fillRoundedRectangle (cell.expanded (2.0f), 3.0f);
        g.setColour (theme::teal);
        g.fillRoundedRectangle (cell, 2.0f);
    }
    g.setColour (theme::ink3);
    g.setFont (theme::font (9.5f));
    for (int s = 0; s < n; ++s)
        g.drawText (s % 4 == 0 ? juce::String (s / 4 + 1) : u8 ("\xC2\xB7"),
                    juce::Rectangle<float> ((float) (rSteps.getX() + theme::keyWidth) + (float) s * cw, (float) rSteps.getY(), cw, (float) rSteps.getHeight()),
                    juce::Justification::centred);

    // legend
    {
        int lx = rLegend.getX();
        const int ly = rLegend.getY();
        const auto f = theme::font (10.5f);
        g.setFont (f);
        const auto item = [&] (juce::Colour fill, bool outline, const juce::String& text) {
            const auto sw = juce::Rectangle<float> ((float) lx, (float) ly + 3.0f, 14.0f, 8.0f);
            if (outline)
            {
                g.setColour (theme::lock);
                g.drawRoundedRectangle (sw.reduced (1.0f), 2.0f, 2.0f);
            }
            else
            {
                g.setColour (fill);
                g.fillRoundedRectangle (sw, 2.0f);
            }
            lx += 19;
            g.setColour (theme::ink3);
            g.drawText (text, lx, ly, 300, 14, juce::Justification::centredLeft);
            lx += (int) theme::textWidth (f, text) + 14;
        };
        item (theme::accent, false, "note");
        item (theme::teal, false, "colour note of the scale");
        item ({}, true, "locked note");
        g.setColour (theme::ink3);
        g.drawFittedText (u8 ("click = add / remove \xC2\xB7 drag = pitch \xC2\xB7 Shift+click = lock note \xC2\xB7 right-click = longer \xC2\xB7 "
                              "bars (left): click = select and edit, click again or Esc = deselect, lock or Shift+click = lock the part, \xC3\x97 = remove \xC2\xB7 "
                              "parts (right): click = add a bar \xC2\xB7 keys 1\xE2\x80\x93" "4 = part \xC2\xB7 Space = dice, Shift+Space = All"),
                          rLegend.getX(), ly + 16, rLegend.getWidth(), 28, juce::Justification::topLeft, 2, 1.0f);
    }
}

void MidinjaEditor::paintOverChildren (juce::Graphics& g)
{
    if (! dragOver)
        return;
    const auto r = getLocalBounds().toFloat().reduced (12.0f);
    g.setColour (juce::Colour (0xd90d0e11));
    g.fillRoundedRectangle (r, 14.0f);
    juce::Path border;
    border.addRoundedRectangle (r, 14.0f);
    juce::Path dashed;
    const float dashes[] = { 12.0f, 8.0f };
    juce::PathStrokeType (3.0f).createDashedStroke (dashed, border, dashes, 2);
    g.setColour (theme::teal);
    g.fillPath (dashed);
    g.setColour (theme::ink);
    g.setFont (theme::font (18.0f));
    g.drawText ("Drop the .mid file here", r.withTrimmedBottom (30.0f), juce::Justification::centred);
    g.setColour (theme::ink2);
    g.setFont (theme::font (13.0f));
    g.drawText (u8 ("up to 4 bars \xE2\x86\x92 A A\xE2\x80\xB2 B C \xC2\xB7 monophonic \xC2\xB7 quantised to 1/16"), r.withTrimmedTop (30.0f), juce::Justification::centred);
}
