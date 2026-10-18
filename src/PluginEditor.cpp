#include "PluginEditor.h"
#include "core/Scales.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace
{
constexpr int kW = 1216, kH = 900;
constexpr int kRowA = 56, kRowB = 66, kFoot = 50, kSide = 156;
constexpr int kTabs = TabBar::height;
constexpr int kUnlockW = 78, kGenH = 26, kInspH = 26, kGhostH = 18;

juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
juce::String u8 (const std::string& s) { return juce::String (juce::CharPointer_UTF8 (s.c_str())); }
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

int textButtonWidth (const juce::String& text) { return (int) theme::textWidth (theme::font (12.0f), text) + 18; }

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

    // ---- Row A: the dice, Rhythm / Notes toggles, Mutate as a mode, All ----
    dice.onClick = [this] { midinja::Op op = midinja::Op::Roll; if (sess.doc.T().drums || sess.currentOp (op)) { sess.apply (op); changed(); } };
    addAndMakeVisible (dice);
    rhythmB.setButtonText (u8 ("\xE2\x99\xA9 Rhythm"));
    rhythmB.setTooltip ("New rhythm (keeps the pitches unless Notes is on too)");
    rhythmB.onClick = [this] {
        if (sess.mutateMode) { sess.mutateMode = false; sess.doRhythm = true; sess.doNotes = false; }
        else sess.doRhythm = ! sess.doRhythm;
        render();
    };
    notesB.setButtonText (u8 ("\xE2\x99\xAA Notes"));
    notesB.setTooltip ("New pitches (keeps the rhythm unless Rhythm is on too)");
    notesB.onClick = [this] {
        if (sess.mutateMode) { sess.mutateMode = false; sess.doRhythm = false; sess.doNotes = true; }
        else sess.doNotes = ! sess.doNotes;
        render();
    };
    mutateB.setTooltip ("Mutate: change some notes, keep the riff. Click again for 15 / 33 / 50 %");
    mutateB.onClick = [this] {
        if (sess.mutateMode) sess.cycleMutAmt();
        else { sess.mutateMode = true; sess.doRhythm = false; sess.doNotes = false; }
        render();
    };
    for (auto* b : { &rhythmB, &notesB, &mutateB })
        addAndMakeVisible (*b);
    rollAll.onClick = [this] { midinja::Op op = midinja::Op::Roll; if (sess.doc.T().drums || sess.currentOp (op)) { sess.applyAll (op); changed(); } };
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
    rootBox.setTooltip (u8 ("The root: pick the note here (both tabs move with it), \xE2\x88\x92 / + move this tab an octave. The range and the parts follow the root"));
    rootBox.onChange = [this] { sess.setRootPc (rootBox.getSelectedId() - 1); changed(); };
    ocD.setTooltip ("This tab an octave down: its root, range and parts");
    ocD.onClick = [this] { sess.setOctave (-1); changed(); };
    ocU.setTooltip ("This tab an octave up: its root, range and parts");
    ocU.onClick = [this] { sess.setOctave (1); changed(); };
    for (size_t i = 0; i < midinja::scales().size(); ++i)
        scaleBox.addItem (midinja::scales()[i].name, (int) i + 1);
    scaleBox.setTooltip ("The scale: notes keep their degree, colour notes and augmented-second pairs move to the new scale's; locked notes stay; chords take their quality from the scale");
    scaleBox.onChange = [this] { sess.setScale (scaleBox.getSelectedId() - 1); changed(); };
    addAndMakeVisible (rootBox);
    addAndMakeVisible (ocD);
    addAndMakeVisible (ocU);
    addAndMakeVisible (scaleBox);
    rateBox.addItem (u8 ("1/8 \xC2\xB7 8"), 2);
    rateBox.addItem (u8 ("1/16 \xC2\xB7 16"), 4);
    rateBox.addItem (u8 ("1/16T \xC2\xB7 12"), 6);
    rateBox.setTooltip ("One part is one bar of 4/4; the rate sets how many steps it has");
    rateBox.onChange = [this] { sess.setRate (rateBox.getSelectedId()); changed(); };
    addAndMakeVisible (rateBox);

    setupKnob (density, [this] { return (double) sess.doc.gen.notes; }, [this] (double v) { sess.doc.gen.notes = (int) v; },
               [this] (double v) { return juce::String (std::min ((int) v, sess.doc.numSteps())) + " / " + juce::String (sess.doc.numSteps()); }, 10.0, nullptr);
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
    advSw.setTooltip ("Velocity, length, ratchets, seed and the undo buttons");
    advSw.onClick = [this] { sess.adv = ! sess.adv; render(); };
    addAndMakeVisible (advSw);

    // ---- Tabs ----
    tabBar.sess = &sess;
    tabBar.onSelect = [this] (int i) { sess.selectTab (i); render(); };
    tabBar.onChannel = [this] (int i, int ch) { sess.setTabChannel (i, ch); changed(); };
    tabBar.onOn = [this] (int i, bool on) { sess.setTabOn (i, on); changed(); };
    addAndMakeVisible (tabBar);

    // ---- Side panel ----
    lenR.get = [this] { return std::pair { sess.doc.gen.lenMin, sess.doc.gen.lenMax }; };
    lenR.set = [this] (int a, int b) { sess.doc.gen.lenMin = a; sess.doc.gen.lenMax = b; };
    lenR.format = [] (int v) { return lenName (v); };
    velR.get = [this] { return std::pair { sess.doc.gen.velMin, sess.doc.gen.velMax }; };
    velR.set = [this] (int a, int b) { sess.doc.gen.velMin = a; sess.doc.gen.velMax = b; };
    velR.format = [] (int v) { return juce::String (v); };
    repR.get = [this] { return std::pair { sess.doc.gen.repMin, sess.doc.gen.repMax }; };
    repR.set = [this] (int a, int b) { sess.doc.gen.repMin = a; sess.doc.gen.repMax = b; };
    repR.format = [] (int v) { return u8 ("\xC3\x97") + juce::String (v); };
    for (auto* r : { &lenR, &velR, &repR })
        addAndMakeVisible (*r);
    repPill.onClick = [this] { sess.doc.gen.repeatsOn = ! sess.doc.gen.repeatsOn; render(); };
    rootPill.setTooltip ("Every bar begins with the root of the chord it starts in. Imported bars are left as they are");
    rootPill.onClick = [this] { sess.doc.gen.startOnRoot = ! sess.doc.gen.startOnRoot; render(); };
    addAndMakeVisible (repPill);
    addAndMakeVisible (rootPill);
    seedBox.setTooltip ("The seed of the last roll. Type one and press Enter to roll the whole form from it");
    seedBox.get = [this] { return (double) sess.doc.gen.seed; };
    seedBox.set = [this] (double v) { sess.doc.gen.seed = (uint32_t) std::llround (v); if (sess.doc.gen.seed == 0) sess.doc.gen.seed = 1; };
    seedBox.onChange = [this] { sess.applyAll (midinja::Op::Roll, true); changed(); };
    addAndMakeVisible (seedBox);

    // ---- Arrangement, the chord lane, the generators, the selected chord ----
    parts.sess = &sess;
    parts.reserveW = kUnlockW;
    parts.onSelectSlot = [this] (int i) { sess.selectSlot (i); proc.restartPlayback(); changed(); };
    parts.onToggleLock = [this] (int k) { sess.toggleLock (k); changed(); };
    parts.onRemoveBar = [this] (int i) { sess.removeBar (i); changed(); };
    parts.onAddBar = [this] (int k) { sess.addBar (k); changed(); };
    parts.onRepeatBar = [this] { sess.repeatLastBar(); changed(); };
    addAndMakeVisible (parts);
    unlockBarB.onClick = [this] { sess.unlockBar(); changed(); };
    addAndMakeVisible (unlockBarB);

    lane.sess = &sess;
    lane.onChanged = [this] { changed(); };
    lane.onPreview = [this] { preview(); };
    lane.onSelect = [this] { render(); };
    addAndMakeVisible (lane);

    progB.setButtonText (u8 ("\xE2\x9A\x84 Progression"));
    progB.onClick = [this] { sess.progression(); changed(); };
    addAndMakeVisible (progB);
    for (int i = 0; i < midinja::kNumMoods; ++i)
    {
        auto& b = moodBs[(size_t) i];
        b.setButtonText (midinja::moodName (i));
        b.onClick = [this, i] { sess.mood (i); changed(); };
        addAndMakeVisible (b);
    }
    for (int i = 0; i < midinja::kNumStyles; ++i)
    {
        auto& b = styleBs[(size_t) i];
        b.setButtonText (midinja::styleName (i));
        b.onClick = [this, i] { sess.style (i); changed(); };
        addAndMakeVisible (b);
    }
    typedEd.setTextToShowWhenEmpty (u8 ("type a progression: i IV i VII \xC2\xB7 one bar per chord; more chords than bars share the bars"), theme::ink3);
    typedEd.setFont (theme::font (11.0f));
    typedEd.setTooltip ("Numerals like i, IV, bVII, V7, iisus4; Enter or Apply sets the lane");
    typedEd.onReturnKey = [this] { applyTyped(); };
    addAndMakeVisible (typedEd);
    applyB.setTooltip ("Set the lane to the typed numerals");
    applyB.onClick = [this] { applyTyped(); };
    addAndMakeVisible (applyB);

    degBox.setTooltip ("The chord's scale degree; the riff plays its tones where it starts");
    degBox.onChange = [this] { if (degBox.getSelectedId() > 0) { sess.setChordDeg (sess.doc.csel, degBox.getSelectedId() - 1); changed(); } };
    typeBox.setTooltip ("Chord type; the quality comes from the scale");
    typeBox.onChange = [this] { if (typeBox.getSelectedId() > 0) { sess.setChordType (sess.doc.csel, (midinja::ChordType) (typeBox.getSelectedId() - 1)); changed(); } };
    for (int i = 0; i < 6; ++i)
        typeBox.addItem (midinja::chordTypeName ((midinja::ChordType) i), i + 1);
    rhythmBox.setTooltip ("The chord rhythm (Chords tab)");
    for (int i = 0; i < midinja::kNumRhythms; ++i)
        rhythmBox.addItem (midinja::rhythmName ((midinja::Rhythm) i), i + 1);
    rhythmBox.onChange = [this] { if (rhythmBox.getSelectedId() > 0) { sess.setChordRhythm (sess.doc.csel, (midinja::Rhythm) (rhythmBox.getSelectedId() - 1)); changed(); } };
    for (auto* b : { &degBox, &typeBox, &rhythmBox })
        addAndMakeVisible (*b);
    static const char* invNames[] = { "Root", "1st", "2nd", "3rd" };
    for (int i = 0; i < 4; ++i)
    {
        auto& b = invBs[(size_t) i];
        b.setButtonText (invNames[i]);
        b.onClick = [this, i] { sess.setChordInv (sess.doc.csel, i); changed(); };
        addAndMakeVisible (b);
    }
    rhythmAllB.setTooltip ("Use this rhythm for every chord");
    rhythmAllB.onClick = [this] { sess.rhythmToAll (sess.doc.csel); changed(); };
    lockB.onClick = [this] { sess.toggleChordLock (sess.doc.csel); changed(); };
    addAndMakeVisible (rhythmAllB);
    addAndMakeVisible (lockB);

    edgeTop.plus.onClick = [this] { sess.growRange (true); changed(); };
    edgeTop.minus.onClick = [this] { sess.shrinkRange (true); changed(); };
    edgeBot.plus.onClick = [this] { sess.growRange (false); changed(); };
    edgeBot.minus.onClick = [this] { sess.shrinkRange (false); changed(); };
    addAndMakeVisible (edgeTop);
    addAndMakeVisible (edgeBot);

    grid.sess = &sess;
    grid.onEdit = [this] { edited(); };
    grid.onPreview = [this] { preview(); };
    grid.onSelectChord = [this] { render(); };
    grid.onChanged = [this] { changed(); };
    fillB.setClickingTogglesState (false);
    fillB.onClick = [this] { const int k = sess.doc.cur; sess.setFill (k, ! sess.doc.beatTab().beats[(size_t) k].fillOn); changed(); };
    addAndMakeVisible (fillB);
    unlockHitsB.onClick = [this] { sess.unlockHits (sess.doc.cur); changed(); };
    addAndMakeVisible (unlockHitsB);
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
    play.setVisible (! proc.isHosted()); // in a host the host's transport plays and stops
    bpmBox.get = [this] { return (double) proc.bpm.load(); };
    bpmBox.set = [this] (double v) { proc.bpm = (float) v; };
    addAndMakeVisible (bpmBox);
    synthSw.setTooltip ("Built-in preview synths through the audio output (off by default): a mono saw / square for the riff, a strings pad for the chords");
    synthSw.onClick = [this] { proc.synthOn = ! proc.synthOn.load(); render(); };
    addAndMakeVisible (synthSw);
    waveBtn.setTooltip ("Waveform of the riff synth");
    waveBtn.onClick = [this] { proc.synthWave = proc.synthWave.load() == 0 ? 1 : 0; render(); };
    addAndMakeVisible (waveBtn);
    const auto mini = [this] (ui::Knob& k, std::function<double()> get, std::function<void (double)> set, std::function<juce::String (double)> fmt, double def) {
        setupKnob (k, std::move (get), std::move (set), std::move (fmt), def, nullptr);
        k.showText = false;
    };
    mini (cutoffK, [this] { return (double) proc.cutoff.load(); }, [this] (double v) { proc.cutoff = (float) v; }, [] (double v) { return juce::String ((int) v) + " Hz"; }, 1800.0);
    mini (resoK, [this] { return (double) proc.resonance.load() * 100.0; }, [this] (double v) { proc.resonance = (float) v / 100.0f; }, [] (double v) { return juce::String ((int) v) + "%"; }, 30.0);
    mini (decayK, [this] { return (double) proc.decay.load(); }, [this] (double v) { proc.decay = (float) v; }, [] (double v) { return juce::String ((int) v) + " ms"; }, 250.0);
    mini (volK, [this] { return (double) proc.volume.load() * 100.0; }, [this] (double v) { proc.volume = (float) v / 100.0f; }, [] (double v) { return juce::String ((int) v) + "%"; }, 60.0);
    for (auto* k : { &cutoffK, &resoK, &decayK, &volK })
        k->onChange = [this, k] { k->setTooltip (k->getName() + ": " + k->format (k->get()) + ". Drag or scroll; Ctrl+click resets"); };

    midiOutBox.setTooltip ("A system MIDI port: a hardware synth, loopMIDI, IAC. Every tab that is on goes out on its channel");
    midiOutBox.onChange = [this] {
        const int idx = midiOutBox.getSelectedId() - 2;
        const auto id = idx >= 0 && idx < midiPortIds.size() ? midiPortIds[idx] : juce::String();
        if (! proc.openMidiOut (id))
        {
            sess.say ("Can't open " + midiOutBox.getText().toStdString() + " (in use by another app?)", true);
            refreshMidiPorts();
            render();
        }
    };
    midiOutBox.addMouseListener (this, false);
    addAndMakeVisible (midiOutBox);
    midiOutBox.setVisible (! proc.isHosted());

    importB.setTooltip (u8 ("Import a .mid file (or drop one on the window) into the Riff tab: up to 4 bars become A A\xE2\x80\xB2 B C"));
    importB.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> ("Import MIDI", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.mid;*.midi");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) importFile (fc.getResult()); });
    };
    exportB.setTooltip ("Save the arrangement as one .mid file: a track per tab that is on, each on its channel");
    exportB.onClick = [this] { exportMidi(); };
    dragB.setTooltip ("Drag this onto your DAW (FL Studio: the playlist or a channel) to drop the arrangement there as a .mid file");
    dragB.onClick = [this] { sess.say ("Drag the handle onto the DAW: the arrangement lands there as a .mid file"); render(); };
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

    for (auto* c : getChildren())
    {
        c->setWantsKeyboardFocus (false);
        c->setMouseClickGrabsKeyboardFocus (false);
    }
    seedBox.setWantsKeyboardFocus (true);
    bpmBox.setWantsKeyboardFocus (true);
    typedEd.setWantsKeyboardFocus (true);
    typedEd.setMouseClickGrabsKeyboardFocus (true);

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
    sess.push (sess.doc.T().poly ? -1 : sess.doc.cur);
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
    lane.repaint();
    resized();
    velLane.repaint();
    lenLane.repaint();
    repLane.repaint();
    parts.repaint();
    versions.repaint();
}

void MidinjaEditor::applyTyped()
{
    sess.applyTyped (typedEd.getText().toStdString());
    changed();
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
        t += juce::String (". Signature move") + (mi.moves.size() > 1 ? "s" : "") + ": " + ms.joinIntoString (", ") + ", played as two notes in a row into a strong step";
    }
    return t + ". The knob is the total amount of colour on the next roll. Ctrl+click resets";
}

void MidinjaEditor::render()
{
    shownVersion = proc.publishedVersion();
    const auto& d = sess.doc;
    const auto& t = d.T();
    const bool adv = sess.adv, poly = t.poly, drums = t.drums;
    const bool hosted = proc.isHosted() && proc.hostBpm.load() > 0.0f;

    // the dice block
    midinja::Op op;
    const bool hasOp = sess.currentOp (op);
    rhythmB.setToggleState (! sess.mutateMode && sess.doRhythm, juce::dontSendNotification);
    notesB.setToggleState (! sess.mutateMode && sess.doNotes, juce::dontSendNotification);
    mutateB.setToggleState (sess.mutateMode, juce::dontSendNotification);
    mutateB.setButtonText ("~ Mutate " + juce::String ((int) std::lround (sess.mutAmt * 100)) + "%");
    const bool lockedPart = ! poly && (drums ? t : d.riff()).locks[(size_t) d.cur];
    const auto* csel = d.chordById (d.csel);
    dice.setEnabled (drums ? ! lockedPart : (hasOp && ! lockedPart && ! (poly && csel == nullptr)));
    for (auto* b : { &rhythmB, &notesB, &mutateB })
        b->setVisible (! drums);
    static const char* whatTips[] = { "Roll the part shown: new rhythm and new notes", "Roll a new rhythm for the part shown, keep its pitches",
                                      "Roll new pitches for the part shown, keep its rhythm", "Mutate the part shown: change some notes, keep the riff" };
    dice.setTooltip (drums ? (lockedPart ? u8 (midinja::partName (d.cur)) + " is locked: unlock it on its bar to roll"
                                        : "Other hats and another fill for " + u8 (midinja::partName (d.cur)) + " (Space). Locked hits stay")
                   : ! hasOp ? juce::String ("Choose what the dice change: Rhythm, Notes or Mutate")
                   : lockedPart ? u8 (midinja::partName (d.cur)) + " is locked: unlock it on its bar to roll"
                   : poly ? juce::String (csel != nullptr ? "A new rhythm for the selected chord (Space)" : "Select a chord in the lane first")
                          : juce::String (whatTips[(int) op]) + " (Space). Locked notes stay; the riff plays a chord tone where a chord starts");
    rollAll.setEnabled (hasOp || drums);
    rollAll.setTooltip (drums ? "Other hats and fills for every unlocked part (Shift+Space)" : poly ? "New rhythms for every unlocked chord (Shift+Space)" : u8 ("The same for all four parts: A, then A\xE2\x80\xB2 B C from it (Shift+Space). Locked parts stay"));
    undoB.setEnabled (sess.canUndo());
    redoB.setEnabled (sess.canRedo());
    undoB.setVisible (adv);
    redoB.setVisible (adv);
    versions.repaint();

    const int oct = (drums ? d.riff().rootNote : t.rootNote) / 12 - 1;
    ocD.setEnabled (! drums);
    ocU.setEnabled (! drums);
    if (oct != rootOctShown)
    {
        rootOctShown = oct;
        rootBox.clear (juce::dontSendNotification);
        for (int i = 0; i < 12; ++i)
            rootBox.addItem (juce::String (midinja::noteNames()[(size_t) i]) + juce::String (oct), i + 1);
    }
    rootBox.setSelectedId (d.gen.root + 1, juce::dontSendNotification);
    scaleBox.setSelectedId (d.gen.scale + 1, juce::dontSendNotification);
    rateBox.setSelectedId (d.rate, juce::dontSendNotification);
    color.setTooltip (colorWords());
    for (auto* k : { &density, &swingK, &similarity, &color, &surprise, &cutoffK, &resoK, &decayK, &volK })
        k->repaint();
    for (auto* k : { &cutoffK, &resoK, &decayK, &volK })
        k->setTooltip (k->getName() + ": " + k->format (k->get()) + ". Drag or scroll; Ctrl+click resets");
    advSw.on = adv;
    advSw.repaint();
    tabBar.refresh();

    for (auto* c : std::initializer_list<juce::Component*> { &lenR, &velR, &repR, &repPill, &rootPill, &seedBox })
        c->setVisible (adv);
    for (auto* c : std::initializer_list<juce::Component*> { &velLane, &lenLane, &repLane })
        c->setVisible (adv && ! poly && ! drums);
    repR.dimmed = ! d.gen.repeatsOn;
    repPill.on = d.gen.repeatsOn;
    rootPill.on = d.gen.startOnRoot;
    for (auto* c : std::initializer_list<juce::Component*> { &lenR, &velR, &repR, &repPill, &rootPill })
        c->repaint();
    seedBox.refresh();

    // Unlock bar
    const int nl = sess.barLocks();
    unlockBarB.setEnabled (nl > 0);
    unlockBarB.setTooltip (nl == 0 ? juce::String ("Nothing is locked in this bar")
                         : poly ? "Unlock the " + juce::String (nl) + (nl > 1 ? " locked chords of bar " : " locked chord of bar ") + juce::String (d.shownBar() + 1)
                         : drums ? "Remove every lock of " + u8 (midinja::partName (d.cur)) + ": the part's lock and its locked hits"
                                : "Remove every lock of " + u8 (midinja::partName (d.cur)) + ": the part's lock and its locked notes, chromatic ones included");

    // the generators
    progB.setTooltip (juce::String ("Pick a progression typical for ") + midinja::scales()[(size_t) d.gen.scale].name + ". Locked chords stay");
    for (int i = 0; i < midinja::kNumMoods; ++i)
    {
        const auto degs = sess.moodDegrees (i);
        std::set<int> uniq (degs.begin(), degs.end());
        juce::StringArray names;
        for (int dg : uniq)
            names.add (u8 (d.numeral (dg)));
        moodBs[(size_t) i].setEnabled (! degs.empty());
        moodBs[(size_t) i].setTooltip (degs.empty() ? juce::String ("No ") + juce::String (midinja::moodName (i)).toLowerCase() + " move exists in " + midinja::scales()[(size_t) d.gen.scale].name + ". Try another scale"
                                                    : juce::String (midinja::moodTip (i)) + kDot + names.joinIntoString (", "));
    }
    for (int i = 0; i < midinja::kNumStyles; ++i)
        styleBs[(size_t) i].setTooltip (juce::String (midinja::styleName (i)) + ": " + midinja::styleTip (i)
                                        + (sess.styleHasOwnList (i) ? juce::String() : kDot + midinja::scales()[(size_t) d.gen.scale].name + " uses the " + (d.tonicMinor() ? "minor" : "major") + " list"));

    // the selected chord
    for (auto* c : std::initializer_list<juce::Component*> { &degBox, &typeBox, &rhythmBox, &rhythmAllB, &lockB })
        c->setVisible (csel != nullptr && ! drums);
    for (auto& b : invBs)
        b.setVisible (csel != nullptr && ! drums);
    fillB.setVisible (drums);
    unlockHitsB.setVisible (drums);
    if (drums)
    {
        const auto& bp = d.beatTab().beats[(size_t) d.cur];
        const bool fillOn = bp.fillOn && d.cur > 0;
        fillB.setButtonText (fillOn ? "Fill on" : "Fill off");
        fillB.setToggleState (fillOn, juce::dontSendNotification);
        fillB.setEnabled (d.cur > 0);
        fillB.setTooltip (d.cur == 0 ? juce::String ("A is the plain groove: fills belong to A\xE2\x80\xB2, B and C")
                        : fillOn ? "Turn the fill off: " + u8 (midinja::partName (d.cur)) + " ends like a plain bar"
                                 : "Turn the fill on: " + u8 (midinja::partName (d.cur)) + (d.cur == 3 ? " ends with a fill over two beats" : " ends with a fill on the last beat"));
        const int nh = sess.lockedHits (d.cur);
        unlockHitsB.setButtonText ("Unlock " + juce::String (nh) + (nh == 1 ? " hit" : " hits"));
        unlockHitsB.setEnabled (nh > 0);
        unlockHitsB.setTooltip (nh > 0 ? "Remove the locks of the hits in " + u8 (midinja::partName (d.cur)) : "No locked hit in " + u8 (midinja::partName (d.cur)) + " (Shift+click a hit to lock it)");
    }
    if (csel != nullptr)
    {
        degBox.clear (juce::dontSendNotification);
        for (int dg = 0; dg < d.scaleSize(); ++dg)
            degBox.addItem (u8 (d.numeral (dg)) + kDot + midinja::noteNames()[(size_t) (((d.gen.root + d.at (dg)) % 12 + 12) % 12)], dg + 1);
        degBox.setSelectedId (((csel->deg % d.scaleSize()) + d.scaleSize()) % d.scaleSize() + 1, juce::dontSendNotification);
        typeBox.setSelectedId ((int) csel->type + 1, juce::dontSendNotification);
        rhythmBox.setSelectedId ((int) csel->rhythm + 1, juce::dontSendNotification);
        for (int i = 0; i < 4; ++i)
        {
            invBs[(size_t) i].setToggleState (i == csel->inv, juce::dontSendNotification);
            invBs[(size_t) i].setEnabled (i <= d.maxInv (*csel));
            invBs[(size_t) i].setTooltip (i > d.maxInv (*csel) ? "Only chords with four notes have a 3rd inversion" : "Which chord note is at the bottom");
        }
        lockB.setButtonText (csel->lock ? "Locked" : "Lock");
        lockB.setToggleState (csel->lock, juce::dontSendNotification);
        lockB.setTooltip (csel->lock ? "Unlock: rolls may change this chord again" : "Lock: rolls keep this chord");
    }

    // the range edges (not on the Beats tab)
    edgeTop.setVisible (! drums);
    edgeBot.setVisible (! drums);
    std::string why;
    edgeTop.label = theme::noteName (t.highNote);
    edgeBot.label = theme::noteName (t.lowNote);
    edgeTop.right = juce::String (poly ? "Chords" : "Riff") + " range " + theme::noteName (t.lowNote) + u8 ("\xE2\x80\x93") + theme::noteName (t.highNote);
    edgeTop.plus.setEnabled (sess.canGrow (true));
    edgeTop.plus.setTooltip (sess.canGrow (true) ? "Add a row above: " + theme::noteName (t.highNote + 1) : juce::String ("The range is three octaves already"));
    edgeTop.minus.setEnabled (sess.canShrink (true, &why));
    edgeTop.minus.setTooltip (edgeTop.minus.isEnabled() ? (poly ? "Remove the top row " + theme::noteName (t.highNote) + ": chord notes beyond it show as ghosts" : "Remove the top row " + theme::noteName (t.highNote) + ": notes on it fold into the range by octave") : u8 (why));
    edgeBot.plus.setEnabled (sess.canGrow (false));
    edgeBot.plus.setTooltip (sess.canGrow (false) ? "Add a row below: " + theme::noteName (t.lowNote - 1) : juce::String ("The range is three octaves already"));
    edgeBot.minus.setEnabled (sess.canShrink (false, &why));
    edgeBot.minus.setTooltip (edgeBot.minus.isEnabled() ? (poly ? "Remove the bottom row " + theme::noteName (t.lowNote) + ": chord notes beyond it show as ghosts" : "Remove the bottom row " + theme::noteName (t.lowNote) + ": notes on it fold into the range by octave") : u8 (why));
    edgeTop.repaint();
    edgeBot.repaint();

    const bool playingNow = proc.isPlayingNow();
    const auto seq = d.playSeq();
    const int bar = juce::jlimit (0, (int) seq.size() - 1, proc.currentBar());
    const int slot = ! playingNow || d.arr.empty() ? -1 : d.sel >= 0 ? d.sel : bar;
    parts.playingSlot = slot;
    parts.repaint();
    const bool onShown = playingNow && (poly ? slot == d.shownBar() : seq[(size_t) bar] == d.cur);
    grid.playStep = onShown ? proc.currentStep() : -1;
    lane.playBeat = playingNow && slot >= 0 ? slot * 4 + (double) proc.currentStep() / d.stepsPerBeat() : -1.0;
    grid.refresh();
    lane.repaint();
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

    resized();
    repaint();
}

// ---------------------------------------------------------------------------------------------

void MidinjaEditor::timerCallback()
{
    if (proc.publishedVersion() != shownVersion)
    {
        render();
        return;
    }
    const auto& d = sess.doc;
    const bool playingNow = proc.isPlayingNow();
    const auto seq = d.playSeq();
    const int bar = juce::jlimit (0, (int) seq.size() - 1, proc.currentBar());
    if (playingNow && d.sel < 0 && ! d.arr.empty() && seq[(size_t) bar] != d.cur)
    {
        sess.selectPart (seq[(size_t) bar]);
        render();
        return;
    }
    const int slot = ! playingNow || d.arr.empty() ? -1 : d.sel >= 0 ? d.sel : bar;
    const bool onShown = playingNow && (d.T().poly ? slot == d.shownBar() : seq[(size_t) bar] == d.cur);
    const int step = onShown ? proc.currentStep() : -1;
    bool dirty = false;
    if (step != grid.playStep)
    {
        grid.playStep = step;
        grid.repaint();
        repaint (rPlayRow);
        dirty = true;
    }
    const double beat = playingNow && slot >= 0 ? slot * 4 + (double) proc.currentStep() / d.stepsPerBeat() : -1.0;
    if (std::abs (beat - lane.playBeat) > 1.0e-6)
    {
        lane.playBeat = beat;
        lane.repaint();
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

    if (! proc.isHosted() && ++portTick >= 90)
    {
        portTick = 0;
        juce::StringArray ids;
        for (const auto& dev : proc.midiOutputs())
            ids.add (dev.identifier);
        if (ids != midiPortIds && juce::Component::getCurrentlyModalComponent() == nullptr)
            refreshMidiPorts();
    }

    if (auto* peer = getPeer())
        if (peer->isFocused() && ! hasKeyboardFocus (true) && ! dragging && juce::Component::getCurrentlyModalComponent() == nullptr)
            grabKeyboardFocus();
}

bool MidinjaEditor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    if (k.getKeyCode() == juce::KeyPress::spaceKey)
    {
        midinja::Op op = midinja::Op::Roll;
        if (sess.doc.T().drums || sess.currentOp (op))
        {
            if (mods.isShiftDown()) sess.applyAll (op);
            else sess.apply (op);
            changed();
        }
        return true;
    }
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
    if ((k.getKeyCode() == juce::KeyPress::deleteKey || k.getKeyCode() == juce::KeyPress::backspaceKey) && sess.doc.T().poly && sess.doc.chordById (sess.doc.csel) != nullptr)
    {
        sess.removeChord (sess.doc.csel);
        changed();
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
        sess.say (("Could not read " + f.getFileName() + ": " + err).toStdString(), true);
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
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              file = file.withFileExtension ("mid");
                              const auto data = proc.exportMidi();
                              if (data.getSize() == 0)
                              {
                                  render();
                                  return;
                              }
                              // the status shows the file name only
                              sess.say ((file.replaceWithData (data.getData(), data.getSize()) ? "Saved " : "Could not save ") + file.getFileName().toStdString());
                              render();
                          });
}

juce::File MidinjaEditor::writeTempMidi()
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Midinja");
    dir.createDirectory();
    const auto file = dir.getChildFile (juce::String (sess.exportName()) + "-" + juce::String ((juce::int64) sess.doc.gen.seed) + ".mid");
    const auto data = proc.exportMidi();
    if (data.getSize() == 0)
    {
        render();
        return {};
    }
    if (! file.replaceWithData (data.getData(), data.getSize()))
    {
        sess.say ("Could not write the .mid file to drag", true);
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
    const auto& d = sess.doc;
    const bool adv = sess.adv, poly = d.T().poly;
    labels.clear();
    smallLabels.clear();
    vseps.clear();
    sideTitles.clear();

    // ---- Row A ----
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
    rhythmB.setBounds (x, (kRowA - 26) / 2, textButtonWidth (rhythmB.getButtonText()), 26);
    x = rhythmB.getRight() + 4;
    notesB.setBounds (x, (kRowA - 26) / 2, textButtonWidth (notesB.getButtonText()), 26);
    x = notesB.getRight() + 8;
    vseps.push_back ({ x, 16, 1, kRowA - 32 });
    x += 1 + 8;
    mutateB.setBounds (x, (kRowA - 26) / 2, textButtonWidth ("~ Mutate 33%"), 26);
    x = mutateB.getRight() + 8;
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

    // ---- Row B ----
    rRowB = { 0, kRowA, W, kRowB };
    const int lblY = kRowA + 7, ctlY = lblY + 15, ctlH = 28;
    const int knobY = kRowA + 7, knobH = 43;
    rx = W - 14;
    advSw.setBounds (rx - advSw.preferredWidth(), kRowA + (kRowB - 20) / 2, advSw.preferredWidth(), 20);
    const int rowBEnd = advSw.getX() - 10;
    for (int pass = 0; pass < 2; ++pass)
    {
        const int gap = pass == 0 ? 10 : 6, scaleW = pass == 0 ? 124 : 112, colorMax = pass == 0 ? 150 : 104;
        const size_t nLabels = labels.size(), nSeps = vseps.size();
        x = 14;
        labels.push_back ({ { x, lblY, 100, 12 }, "Key" });
        rootBox.setBounds (x, ctlY, 62, ctlH);
        ocD.setBounds (rootBox.getRight() + 4, ctlY, 24, ctlH);
        ocU.setBounds (ocD.getRight() + 3, ctlY, 24, ctlH);
        scaleBox.setBounds (ocU.getRight() + 4, ctlY, scaleW, ctlH);
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

    tabBar.setBounds (0, kRowA + kRowB, W, kTabs);

    // ---- Footer ----
    rFoot = { 0, H - kFoot, W, kFoot };
    const int fy = H - kFoot;
    x = 14;
    if (proc.isHosted())
    {
        rHostNote = { x, fy + 8, 120, 34 };
        x = rHostNote.getRight() + 8;
    }
    else
    {
        rHostNote = {};
        play.setBounds (x, fy + 8, 34, 34);
        x = play.getRight() + 12;
    }
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
    if (! proc.isHosted())
    {
        const int midiW = std::max (90, std::min (170, rx - x - 60));
        midiOutBox.setBounds (rx - midiW, fy + 12, midiW, 26);
        labels.push_back ({ { midiOutBox.getX() - 58, fy + 17, 54, 16 }, "MIDI Out" });
    }

    // ---- Main ----
    const int mainY = kRowA + kRowB + kTabs, mainH = fy - mainY;
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
    {
        const auto ra = parts.reservedArea().translated (gx, y);
        unlockBarB.setBounds (ra.getX(), y + 3, ra.getWidth(), 24);
    }
    y = parts.getBottom() + 6;
    lane.setBounds (gx, y, gw, ChordLane::height);
    y = lane.getBottom() + 6;

    // the generators row
    rGen = { gx, y, gw, kGenH };
    x = gx;
    progB.setBounds (x, y, textButtonWidth (progB.getButtonText()), kGenH);
    x = progB.getRight() + 10;
    smallLabels.push_back ({ { x, y, 34, kGenH }, "MOOD" });
    x += 36;
    for (auto& b : moodBs)
    {
        b.setBounds (x, y, textButtonWidth (b.getButtonText()), kGenH);
        x = b.getRight() + 4;
    }
    x += 6;
    smallLabels.push_back ({ { x, y, 36, kGenH }, "STYLE" });
    x += 38;
    for (auto& b : styleBs)
    {
        b.setBounds (x, y, textButtonWidth (b.getButtonText()), kGenH);
        x = b.getRight() + 4;
    }
    x += 6;
    applyB.setBounds (gx + gw - 56, y, 56, kGenH);
    typedEd.setBounds (x, y, std::max (80, applyB.getX() - 6 - x), kGenH);
    y = rGen.getBottom() + 6;

    // the selected chord row
    rInsp = { gx, y, gw, kInspH };
    rInspName = { gx, y, d.T().drums ? 300 : 170, kInspH };
    x = rInspName.getRight() + 6;
    fillB.setBounds (x, y, textButtonWidth ("Fill off"), kInspH);
    unlockHitsB.setBounds (fillB.getRight() + 6, y, textButtonWidth ("Unlock 99 hits"), kInspH);
    smallLabels.push_back ({ { x, y, 46, kInspH }, "DEGREE" });
    x += 48;
    degBox.setBounds (x, y, 110, kInspH);
    x = degBox.getRight() + 8;
    smallLabels.push_back ({ { x, y, 30, kInspH }, "TYPE" });
    x += 32;
    typeBox.setBounds (x, y, 74, kInspH);
    x = typeBox.getRight() + 8;
    smallLabels.push_back ({ { x, y, 62, kInspH }, "INVERSION" });
    x += 64;
    for (auto& b : invBs)
    {
        b.setBounds (x, y, 40, kInspH);
        x = b.getRight() + 2;
    }
    x += 6;
    smallLabels.push_back ({ { x, y, 50, kInspH }, "RHYTHM" });
    x += 52;
    rhythmBox.setBounds (x, y, 98, kInspH);
    x = rhythmBox.getRight() + 6;
    rhythmAllB.setBounds (x, y, textButtonWidth (rhythmAllB.getButtonText()), kInspH);
    x = rhythmAllB.getRight() + 6;
    lockB.setBounds (x, y, textButtonWidth ("Locked"), kInspH);
    y = rInsp.getBottom() + 6;

    const bool hasMsg = ! sess.message.empty();
    rMsg = hasMsg ? juce::Rectangle<int> (gx, y - 2, gw, 15) : juce::Rectangle<int>();
    if (hasMsg)
        y += 17;
    edgeTop.setBounds (gx, y, gw, EdgeBar::height);
    y = edgeTop.getBottom() + 2;
    bool anyUp = false, anyDown = false;
    for (const auto& c : grid.cuts)
    {
        anyUp = anyUp || c.above;
        anyDown = anyDown || ! c.above;
    }
    rGhostTop = anyUp ? juce::Rectangle<int> (gx, y, gw, kGhostH) : juce::Rectangle<int>();
    if (anyUp)
        y += kGhostH + 2;
    const int lanesH = adv && ! poly && ! d.T().drums ? velLane.preferredHeight() + 5 + lenLane.preferredHeight() + 5 + repLane.preferredHeight() + 6 : 0;
    const int legendH = 46, stepsH = 14, playH = 4 + 10, edgeBotH = EdgeBar::height + 2 + (anyDown ? kGhostH + 2 : 0);
    const int budget = fy - 10 - legendH - stepsH - lanesH - playH - edgeBotH - y;
    const int gh = grid.layoutHeight (budget);
    grid.setBounds (gx, y, gw, gh);
    y = grid.getBottom() + 2;
    rGhostBot = anyDown ? juce::Rectangle<int> (gx, y, gw, kGhostH) : juce::Rectangle<int>();
    if (anyDown)
        y += kGhostH + 2;
    edgeBot.setBounds (gx, y, gw, EdgeBar::height);
    y = edgeBot.getBottom() + 2;
    rPlayRow = { gx, y, gw, 4 };
    y += 4 + 6;
    if (adv && ! poly && ! d.T().drums)
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

void MidinjaEditor::paint (juce::Graphics& g)
{
    const auto& d = sess.doc;
    g.fillAll (theme::bg);

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

    g.setColour (theme::panel);
    g.fillRect (rRowB);
    g.setColour (theme::line);
    g.fillRect (0, rRowB.getBottom() - 1, getWidth(), 1);

    if (! rSide.isEmpty())
    {
        g.setColour (theme::panel);
        g.fillRect (rSide);
        g.setColour (theme::line);
        g.fillRect (rSide.getRight() - 1, rSide.getY(), 1, rSide.getHeight());
        for (const auto& t : sideTitles)
            theme::drawLabel (g, t.s, t.r);
    }

    g.setColour (theme::foot);
    g.fillRect (rFoot);
    g.setColour (juce::Colours::black);
    g.fillRect (0, rFoot.getY(), getWidth(), 1);
    if (! rHostNote.isEmpty())
    {
        g.setColour (theme::ink3);
        g.setFont (theme::font (10.5f));
        g.drawFittedText ("Play and Stop follow the host", rHostNote, juce::Justification::centredLeft, 2, 1.0f);
    }

    for (const auto& t : labels)
        theme::drawLabel (g, t.s, t.r);
    g.setColour (theme::ink3);
    g.setFont (theme::font (9.5f));
    for (const auto& t : smallLabels)
        g.drawText (t.s, t.r, juce::Justification::centredLeft);
    g.setColour (theme::line);
    for (const auto& v : vseps)
        g.fillRect (v);

    // the generators and the selected chord rows
    g.setColour (theme::line);
    g.drawRoundedRectangle (rGen.toFloat().expanded (4.0f, 3.0f).reduced (0.5f), 7.0f, 1.0f);
    g.drawRoundedRectangle (rInsp.toFloat().expanded (4.0f, 3.0f).reduced (0.5f), 7.0f, 1.0f);
    if (d.T().drums)
    {
        const auto& bp = d.beatTab().beats[(size_t) d.cur];
        const int L = d.fillLen (d.cur), b = d.shownBar();
        g.setColour (theme::ink);
        g.setFont (theme::font (14.0f, true));
        g.drawText ((b >= 0 ? "Bar " + juce::String (b + 1) + kDot : juce::String()) + u8 (midinja::partName (d.cur)), rInspName.withHeight (14), juce::Justification::centredLeft, true);
        g.setColour (theme::ink3);
        g.setFont (theme::font (9.5f));
        g.drawText (juce::String ("hats: ") + midinja::hatName (bp.hat) + (bp.clap ? kDot + "clap" : juce::String())
                    + kDot + (L > 0 ? juce::String ("fill: ") + midinja::fillName (bp.fill) + (d.cur == 3 ? " (two beats)" : "") : juce::String ("no fill")),
                    rInspName.withTop (rInspName.getY() + 14), juce::Justification::centredLeft, true);
    }
    else if (const auto* c = d.chordById (d.csel))
    {
        g.setColour (theme::ink);
        g.setFont (theme::font (14.0f, true));
        g.drawText (u8 (d.chordName (*c)), rInspName.withHeight (14), juce::Justification::centredLeft, true);
        g.setColour (theme::ink3);
        g.setFont (theme::font (9.5f));
        const auto type = c->type;
        g.drawText (u8 (d.numeral (c->deg, &type)) + kDot + "beats " + juce::String (c->start + 1) + u8 ("\xE2\x80\x93") + juce::String (c->start + c->len)
                    + " (" + juce::String (c->len) + (c->len > 1 ? " beats)" : " beat)"), rInspName.withTop (rInspName.getY() + 14), juce::Justification::centredLeft, true);
    }
    else
    {
        g.setColour (theme::ink3);
        g.setFont (theme::font (11.0f));
        g.drawText ("Select a chord in the lane to edit it", rInsp, juce::Justification::centredLeft);
    }

    if (! rMsg.isEmpty())
    {
        g.setColour (sess.messageIsError ? juce::Colour (0xffe76f51) : theme::teal);
        g.setFont (theme::font (11.0f));
        g.drawText (u8 (sess.message), rMsg, juce::Justification::centredLeft, true);
    }

    // chord notes the range cuts off: named ghosts above and below the roll
    const auto drawCuts = [&] (juce::Rectangle<int> strip, bool above) {
        if (strip.isEmpty())
            return;
        for (const auto& c : grid.cuts)
        {
            if (c.above != above)
                continue;
            const float x = (float) grid.getX() + grid.columnX (c.step), w = (float) c.len * grid.colWidth() - 3.0f;
            const auto r = juce::Rectangle<float> (x, (float) strip.getY() + 1.0f, std::max (12.0f, w), (float) strip.getHeight() - 2.0f);
            g.setColour (theme::accent.withAlpha (0.07f));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (c.selected ? theme::accent : juce::Colour (0xff8a5a2c));
            const float dashes[] = { 3.0f, 2.0f };
            juce::Path p;
            p.addRoundedRectangle (r.reduced (0.5f), 3.0f);
            juce::Path dashed;
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, p, dashes, 2);
            g.fillPath (dashed);
            g.setColour (c.selected ? theme::ink : theme::ink2);
            g.setFont (theme::font (10.0f));
            g.drawText (c.text, r.reduced (4.0f, 0.0f), juce::Justification::centredLeft, true);
        }
    };
    drawCuts (rGhostTop, true);
    drawCuts (rGhostBot, false);

    const int n = std::max (1, d.numSteps());
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

    {
        int lx = rLegend.getX();
        const int ly = rLegend.getY();
        const auto f = theme::font (10.5f);
        g.setFont (f);
        const auto item = [&] (juce::Colour fill, int kind, const juce::String& text) {
            const auto sw = juce::Rectangle<float> ((float) lx, (float) ly + 3.0f, 14.0f, 8.0f);
            if (kind == 1)
            {
                g.setColour (theme::lock);
                g.drawRoundedRectangle (sw.reduced (1.0f), 2.0f, 2.0f);
            }
            else if (kind == 2)
            {
                juce::Path arc;
                arc.startNewSubPath (sw.getX(), sw.getBottom());
                arc.quadraticTo (sw.getCentreX(), sw.getY() - 3.0f, sw.getRight(), sw.getBottom());
                g.setColour (theme::teal);
                g.strokePath (arc, juce::PathStrokeType (1.5f));
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
        item (theme::accent, 0, "note");
        item (theme::teal, 0, "colour note of the scale");
        item ({}, 1, "locked note");
        item (theme::ink2.withAlpha (0.3f), 0, "the other tab (ghost)");
        item ({}, 2, "augmented-second pair");
        g.setColour (theme::ink3);
        g.drawFittedText (u8 ("click = add / remove \xC2\xB7 drag = pitch (Alt = chromatic, locks the note) \xC2\xB7 Shift+click = lock \xC2\xB7 right-click = longer \xC2\xB7 "
                              "chord lane: drag a chord to move it, drag its edge for the length, one-beat snap, double-click empty lane to add, Del removes, the lock keeps it when you roll \xC2\xB7 "
                              "keys 1\xE2\x80\x93" "4 = part \xC2\xB7 Space = dice, Shift+Space = All"),
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
    g.drawText (u8 ("up to 4 bars \xE2\x86\x92 A A\xE2\x80\xB2 B C \xC2\xB7 monophonic \xC2\xB7 quantised to 1/16 \xC2\xB7 into the Riff tab"), r.withTrimmedTop (30.0f), juce::Justification::centred);
}
