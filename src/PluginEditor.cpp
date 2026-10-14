#include "PluginEditor.h"
#include "core/Scales.h"

namespace
{
constexpr int kW = 1120, kH = 680;
constexpr int kTop = 72, kFoot = 76, kLeft = 220;
} // namespace

MidinjaEditor::MidinjaEditor (MidinjaProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);

    // ---- Top bar ----
    dice.setColour (juce::TextButton::buttonColourId, theme::accent);
    dice.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff1a1206));
    dice.setTooltip ("Generate a new riff (locked notes stay)");
    dice.onClick = [this] { proc.roll(); refreshFromProcessor(); };
    addAndMakeVisible (dice);

    infinity.setClickingTogglesState (true);
    infinity.setTooltip ("New riff at the start of every cycle");
    infinity.onClick = [this] { proc.infinity = infinity.getToggleState(); };
    addAndMakeVisible (infinity);

    play.setClickingTogglesState (true);
    play.onClick = [this] {
        proc.playing = play.getToggleState();
        play.setButtonText (play.getToggleState() ? "Stop" : "Play");
    };
    addAndMakeVisible (play);

    exportButton.onClick = [this] { exportMidi(); };
    addAndMakeVisible (exportButton);

    setupCombo (rootBox, rootLabel, "Root");
    for (int i = 0; i < 12; ++i)
        rootBox.addItem (midinja::noteNames()[(size_t) i], i + 1);
    rootBox.onChange = [this] {
        const int root = rootBox.getSelectedId() - 1;
        applyEdit ([root] (MidinjaProcessor::State& s) {
            midinja::conformToScale (s.pattern, s.gen.root, root, s.gen.scale);
            s.gen.root = root;
        });
    };

    setupCombo (scaleBox, scaleLabel, "Scale");
    for (size_t i = 0; i < midinja::scales().size(); ++i)
        scaleBox.addItem (midinja::scales()[i].name, (int) i + 1);
    scaleBox.onChange = [this] {
        const int scale = scaleBox.getSelectedId() - 1;
        applyEdit ([scale] (MidinjaProcessor::State& s) {
            midinja::conformToScale (s.pattern, s.gen.root, s.gen.root, scale);
            s.gen.scale = scale;
        });
    };

    setupCombo (stepsBox, stepsLabel, "Steps");
    for (int i = 2; i <= midinja::kMaxSteps; ++i)
        stepsBox.addItem (juce::String (i), i);
    stepsBox.onChange = [this] {
        const int n = stepsBox.getSelectedId();
        applyEdit ([n] (MidinjaProcessor::State& s) {
            s.pattern.numSteps = n;
            midinja::fitLengths (s.pattern);
        });
    };

    setupCombo (rateBox, rateLabel, "Rate");
    for (int i = 0; i < 4; ++i)
        rateBox.addItem (midinja::rateName ((midinja::Rate) i), i + 1);
    rateBox.onChange = [this] { proc.rate = rateBox.getSelectedId() - 1; };

    setupKnob (swingKnob, swingLabel, "Swing", 0.0, 100.0, 1.0);
    swingKnob.setTextValueSuffix ("%");
    swingKnob.onValueChange = [this] { proc.swing = (float) swingKnob.getValue() / 100.0f; };

    setupKnob (notesKnob, notesLabel, "Notes", 1.0, midinja::kMaxSteps, 1.0);
    notesKnob.setTooltip ("How many steps the dice fills");
    notesKnob.onValueChange = [this] {
        const int v = (int) notesKnob.getValue();
        applyEdit ([v] (MidinjaProcessor::State& s) { s.gen.notes = v; });
    };

    transposeLabel.setText ("Transpose", juce::dontSendNotification);
    transposeLabel.setFont (juce::FontOptions (10.5f));
    addAndMakeVisible (transposeLabel);
    transposeDown.setTooltip ("Whole riff one scale step down");
    transposeUp.setTooltip ("Whole riff one scale step up");
    transposeDown.onClick = [this] { applyEdit ([] (MidinjaProcessor::State& s) { midinja::transposeScaleSteps (s.pattern, -1, s.gen.root, s.gen.scale); }); };
    transposeUp.onClick = [this] { applyEdit ([] (MidinjaProcessor::State& s) { midinja::transposeScaleSteps (s.pattern, 1, s.gen.root, s.gen.scale); }); };
    addAndMakeVisible (transposeDown);
    addAndMakeVisible (transposeUp);

    bpmLabel.setText ("BPM", juce::dontSendNotification);
    bpmLabel.setFont (juce::FontOptions (10.5f));
    addAndMakeVisible (bpmLabel);
    bpmSlider.setSliderStyle (juce::Slider::LinearBar);
    bpmSlider.setRange (40.0, 240.0, 1.0);
    bpmSlider.setColour (juce::Slider::trackColourId, theme::teal.withAlpha (0.35f));
    bpmSlider.setColour (juce::Slider::textBoxTextColourId, theme::ink);
    bpmSlider.onValueChange = [this] { proc.bpm = (float) bpmSlider.getValue(); };
    addAndMakeVisible (bpmSlider);

    // ---- Left panel ----
    setupRange (pitchRange, pitchLabel, "Pitch range", 24.0, 96.0);
    pitchRange.onValueChange = [this] {
        const int lo = (int) pitchRange.getMinValue(), hi = (int) pitchRange.getMaxValue();
        applyEdit ([lo, hi] (MidinjaProcessor::State& s) { s.gen.lowNote = lo; s.gen.highNote = hi; });
    };
    setupRange (lengthRange, lengthLabel, "Length (steps)", 1.0, midinja::kMaxLength);
    lengthRange.onValueChange = [this] {
        const int lo = (int) lengthRange.getMinValue(), hi = (int) lengthRange.getMaxValue();
        applyEdit ([lo, hi] (MidinjaProcessor::State& s) { s.gen.lenMin = lo; s.gen.lenMax = hi; });
    };
    setupRange (velRange, velLabel, "Velocity", 1.0, 127.0);
    velRange.onValueChange = [this] {
        const int lo = (int) velRange.getMinValue(), hi = (int) velRange.getMaxValue();
        applyEdit ([lo, hi] (MidinjaProcessor::State& s) { s.gen.velMin = lo; s.gen.velMax = hi; });
    };

    repeatsToggle.setTooltip ("Ratchets: a note repeated 1-4 times within its length");
    repeatsToggle.onClick = [this] {
        const bool on = repeatsToggle.getToggleState();
        applyEdit ([on] (MidinjaProcessor::State& s) {
            s.gen.repeatsOn = on;
            if (! on) // off means no ratchets play
                for (auto& st : s.pattern.steps)
                    st.repeats = 1;
        });
    };
    addAndMakeVisible (repeatsToggle);
    setupRange (repRange, repRangeLabel, "Repeats", 1.0, midinja::kMaxRepeats);
    repRange.onValueChange = [this] {
        const int lo = (int) repRange.getMinValue(), hi = (int) repRange.getMaxValue();
        applyEdit ([lo, hi] (MidinjaProcessor::State& s) { s.gen.repMin = lo; s.gen.repMax = hi; });
    };
    repChanceLabel.setText ("Repeat chance", juce::dontSendNotification);
    repChanceLabel.setFont (juce::FontOptions (10.5f));
    addAndMakeVisible (repChanceLabel);
    repChance.setSliderStyle (juce::Slider::LinearHorizontal);
    repChance.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18);
    repChance.setRange (0.0, 100.0, 1.0);
    repChance.setTextValueSuffix ("%");
    repChance.onValueChange = [this] {
        const float v = (float) repChance.getValue() / 100.0f;
        applyEdit ([v] (MidinjaProcessor::State& s) { s.gen.repChance = v; });
    };
    addAndMakeVisible (repChance);

    for (auto* l : { &pitchValue, &lengthValue, &velValue, &repValue })
    {
        l->setJustificationType (juce::Justification::centredRight);
        l->setFont (juce::FontOptions (11.0f));
        l->setColour (juce::Label::textColourId, theme::ink);
        addAndMakeVisible (*l);
    }
    seedLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (seedLabel);

    // ---- Centre ----
    for (auto* c : { (juce::Component*) &grid, (juce::Component*) &velLane, (juce::Component*) &lenLane, (juce::Component*) &repLane })
        addAndMakeVisible (*c);
    grid.onEdit = [this] (const Edit& fn) { applyEdit (fn); };
    velLane.onEdit = [this] (const Edit& fn) { applyEdit (fn); };
    lenLane.onEdit = [this] (const Edit& fn) { applyEdit (fn); };
    repLane.onEdit = [this] (const Edit& fn) { applyEdit (fn); };

    // ---- Footer ----
    synthToggle.setTooltip ("Built-in synth through the audio output");
    synthToggle.onClick = [this] { proc.synthOn = synthToggle.getToggleState(); };
    addAndMakeVisible (synthToggle);
    setupCombo (waveBox, waveLabel, "Wave");
    waveBox.addItem ("Saw", 1);
    waveBox.addItem ("Square", 2);
    waveBox.onChange = [this] { proc.synthWave = waveBox.getSelectedId() - 1; };
    setupKnob (cutoffKnob, cutoffLabel, "Cutoff", 60.0, 12000.0, 1.0);
    cutoffKnob.setSkewFactorFromMidPoint (1200.0);
    cutoffKnob.setTextValueSuffix (" Hz");
    cutoffKnob.onValueChange = [this] { proc.cutoff = (float) cutoffKnob.getValue(); };
    setupKnob (resoKnob, resoLabel, "Reso", 0.0, 100.0, 1.0);
    resoKnob.onValueChange = [this] { proc.resonance = (float) resoKnob.getValue() / 100.0f; };
    setupKnob (decayKnob, decayLabel, "Decay", 30.0, 1500.0, 1.0);
    decayKnob.setSkewFactorFromMidPoint (300.0);
    decayKnob.setTextValueSuffix (" ms");
    decayKnob.onValueChange = [this] { proc.decay = (float) decayKnob.getValue(); };
    setupKnob (volumeKnob, volumeLabel, "Volume", 0.0, 100.0, 1.0);
    volumeKnob.onValueChange = [this] { proc.volume = (float) volumeKnob.getValue() / 100.0f; };

    setupCombo (midiOutBox, midiOutLabel, "MIDI Out");
    midiOutBox.onChange = [this] {
        const int idx = midiOutBox.getSelectedId() - 2; // id 1 = None
        const auto id = idx >= 0 && idx < midiPortIds.size() ? midiPortIds[idx] : juce::String();
        if (proc.openMidiOut (id))
        {
            setStatus (id.isEmpty() ? "MIDI Out off" : "MIDI Out: " + midiOutBox.getText());
        }
        else
        {
            setStatus ("Can't open " + midiOutBox.getText() + " (in use by another app?)");
            refreshMidiPorts();
        }
    };
    refreshPorts.setTooltip ("Look for MIDI ports again");
    refreshPorts.onClick = [this] { refreshMidiPorts(); };
    addAndMakeVisible (refreshPorts);
    setupCombo (channelBox, channelLabel, "Channel");
    for (int i = 1; i <= 16; ++i)
        channelBox.addItem (juce::String (i), i);
    channelBox.onChange = [this] { proc.setChannel (channelBox.getSelectedId()); };

    status.setFont (juce::FontOptions (11.0f));
    status.setJustificationType (juce::Justification::centredLeft);
    status.setText ("Audio device: Options (top-left of the window) > Audio/MIDI Settings", juce::dontSendNotification);
    addAndMakeVisible (status);

    setSize (kW, kH);
    refreshMidiPorts();
    refreshFromProcessor();
    startTimerHz (30);
}

MidinjaEditor::~MidinjaEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MidinjaEditor::setupCombo (juce::ComboBox& box, juce::Label& label, const juce::String& title)
{
    label.setText (title, juce::dontSendNotification);
    label.setFont (juce::FontOptions (10.5f));
    addAndMakeVisible (label);
    addAndMakeVisible (box);
}

void MidinjaEditor::setupKnob (juce::Slider& s, juce::Label& label, const juce::String& title, double lo, double hi, double step)
{
    label.setText (title, juce::dontSendNotification);
    label.setFont (juce::FontOptions (10.5f));
    label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (label);
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 14);
    s.setRange (lo, hi, step);
    addAndMakeVisible (s);
}

void MidinjaEditor::setupRange (juce::Slider& s, juce::Label& label, const juce::String& title, double lo, double hi)
{
    label.setText (title, juce::dontSendNotification);
    label.setFont (juce::FontOptions (10.5f));
    addAndMakeVisible (label);
    s.setSliderStyle (juce::Slider::TwoValueHorizontal);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setRange (lo, hi, 1.0);
    addAndMakeVisible (s);
}

// ---------------------------------------------------------------------------------------------

void MidinjaEditor::applyEdit (const Edit& fn)
{
    proc.editState (fn);
    refreshFromProcessor();
}

void MidinjaEditor::refreshFromProcessor()
{
    shownVersion = proc.stateVersion();
    view = proc.getState();
    grid.setState (view);
    velLane.setState (view);
    lenLane.setState (view);
    repLane.setState (view);
    syncControls();
}

void MidinjaEditor::syncControls()
{
    const auto& g = view.gen;
    const auto ns = juce::dontSendNotification;
    rootBox.setSelectedId (g.root + 1, ns);
    scaleBox.setSelectedId (g.scale + 1, ns);
    stepsBox.setSelectedId (view.pattern.numSteps, ns);
    rateBox.setSelectedId (proc.rate.load() + 1, ns);
    swingKnob.setValue (proc.swing.load() * 100.0, ns);
    notesKnob.setValue (g.notes, ns);
    bpmSlider.setValue (proc.bpm.load(), ns);
    infinity.setToggleState (proc.infinity.load(), ns);
    play.setToggleState (proc.playing.load(), ns);
    play.setButtonText (proc.playing.load() ? "Stop" : "Play");

    pitchRange.setMinAndMaxValues (juce::jmin (g.lowNote, g.highNote), juce::jmax (g.lowNote, g.highNote), ns);
    lengthRange.setMinAndMaxValues (juce::jmin (g.lenMin, g.lenMax), juce::jmax (g.lenMin, g.lenMax), ns);
    velRange.setMinAndMaxValues (juce::jmin (g.velMin, g.velMax), juce::jmax (g.velMin, g.velMax), ns);
    repRange.setMinAndMaxValues (juce::jmin (g.repMin, g.repMax), juce::jmax (g.repMin, g.repMax), ns);
    repChance.setValue (g.repChance * 100.0, ns);
    repeatsToggle.setToggleState (g.repeatsOn, ns);
    repRange.setEnabled (g.repeatsOn);
    repChance.setEnabled (g.repeatsOn);

    pitchValue.setText (theme::noteName (juce::jmin (g.lowNote, g.highNote)) + " - " + theme::noteName (juce::jmax (g.lowNote, g.highNote)), ns);
    lengthValue.setText (juce::String (juce::jmin (g.lenMin, g.lenMax)) + " - " + juce::String (juce::jmax (g.lenMin, g.lenMax)), ns);
    velValue.setText (juce::String (juce::jmin (g.velMin, g.velMax)) + " - " + juce::String (juce::jmax (g.velMin, g.velMax)), ns);
    repValue.setText ("x" + juce::String (juce::jmin (g.repMin, g.repMax)) + " - x" + juce::String (juce::jmax (g.repMin, g.repMax)), ns);
    seedLabel.setText ("Seed  #" + juce::String ((juce::int64) g.seed), ns);

    synthToggle.setToggleState (proc.synthOn.load(), ns);
    waveBox.setSelectedId (proc.synthWave.load() + 1, ns);
    cutoffKnob.setValue (proc.cutoff.load(), ns);
    resoKnob.setValue (proc.resonance.load() * 100.0, ns);
    decayKnob.setValue (proc.decay.load(), ns);
    volumeKnob.setValue (proc.volume.load() * 100.0, ns);
    channelBox.setSelectedId (proc.channel.load(), ns);
}

void MidinjaEditor::refreshMidiPorts()
{
    midiOutBox.clear (juce::dontSendNotification);
    midiPortIds.clear();
    midiOutBox.addItem ("None", 1);
    int selected = 1;
    const auto current = proc.midiOutId();
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

void MidinjaEditor::exportMidi()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Midinja");
    dir.createDirectory();
    const auto& g = view.gen;
    const auto name = juce::String ("Midinja-") + midinja::noteNames()[(size_t) g.root] + "-"
                    + juce::String (midinja::scales()[(size_t) juce::jlimit (0, (int) midinja::scales().size() - 1, g.scale)].name)
                          .removeCharacters (" ()#-.").toLowerCase()
                    + "-" + juce::String ((int) proc.bpm.load()) + "bpm-" + juce::String ((juce::int64) g.seed) + ".mid";

    chooser = std::make_unique<juce::FileChooser> ("Export MIDI", dir.getChildFile (name), "*.mid");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              file = file.withFileExtension ("mid");
                              const auto data = proc.exportMidi();
                              if (file.replaceWithData (data.getData(), data.getSize()))
                                  setStatus ("Saved " + file.getFullPathName());
                              else
                                  setStatus ("Could not save " + file.getFullPathName());
                          });
}

void MidinjaEditor::timerCallback()
{
    if (proc.stateVersion() != shownVersion)
        refreshFromProcessor(); // an Infinity re-roll, or a state restore

    const int step = proc.currentStep();
    grid.setPlayStep (step);
    velLane.setPlayStep (step);
    lenLane.setPlayStep (step);
    repLane.setPlayStep (step);
}

// ---------------------------------------------------------------------------------------------

void MidinjaEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);

    const auto top = juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), (float) kTop);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff22262f), 0.0f, 0.0f, juce::Colour (0xff1b1e25), 0.0f, (float) kTop, false));
    g.fillRect (top);

    g.setColour (theme::panel);
    g.fillRect (0, kTop, kLeft, getHeight() - kTop - kFoot);
    g.setColour (juce::Colour (0xff1a1d23));
    g.fillRect (0, getHeight() - kFoot, getWidth(), kFoot);

    g.setColour (juce::Colours::black);
    g.drawHorizontalLine (kTop, 0.0f, (float) getWidth());
    g.drawHorizontalLine (getHeight() - kFoot, 0.0f, (float) getWidth());
    g.setColour (theme::line);
    g.drawVerticalLine (kLeft, (float) kTop, (float) (getHeight() - kFoot));

    // Footer section titles and the logo.
    g.setColour (theme::ink3);
    g.setFont (juce::FontOptions (10.0f).withStyle ("Bold"));
    g.drawText ("SYNTH", 12, getHeight() - kFoot + 6, 80, 12, juce::Justification::left);
    g.drawText ("MIDI", 470, getHeight() - kFoot + 6, 80, 12, juce::Justification::left);
    g.setColour (theme::ink);
    g.setFont (juce::FontOptions (18.0f).withStyle ("Bold"));
    g.drawText ("Midi", getWidth() - 140, getHeight() - kFoot + 26, 80, 24, juce::Justification::right);
    g.setColour (theme::accent);
    g.drawText ("nja", getWidth() - 60, getHeight() - kFoot + 26, 50, 24, juce::Justification::left);
}

void MidinjaEditor::resized()
{
    const int W = getWidth(), H = getHeight();

    // Top bar
    dice.setBounds (12, 11, 72, 50);
    infinity.setBounds (94, 22, 82, 28);
    auto combo = [] (juce::Label& l, juce::ComboBox& b, int x, int w) {
        l.setBounds (x, 6, w, 16);
        b.setBounds (x, 24, w, 28);
    };
    combo (rootLabel, rootBox, 188, 62);
    combo (scaleLabel, scaleBox, 258, 160);
    combo (stepsLabel, stepsBox, 426, 60);
    combo (rateLabel, rateBox, 494, 72);
    auto knob = [] (juce::Label& l, juce::Slider& s, int x, int y) {
        l.setBounds (x, y, 64, 14);
        s.setBounds (x, y + 14, 64, 54);
    };
    knob (swingLabel, swingKnob, 576, 2);
    knob (notesLabel, notesKnob, 644, 2);
    transposeLabel.setBounds (716, 6, 70, 16);
    transposeDown.setBounds (716, 24, 30, 28);
    transposeUp.setBounds (750, 24, 30, 28);
    bpmLabel.setBounds (792, 6, 110, 16);
    bpmSlider.setBounds (792, 24, 110, 28);
    play.setBounds (W - 220, 14, 86, 44);
    exportButton.setBounds (W - 124, 14, 112, 44);

    // Left panel
    int y = kTop + 12;
    auto range = [&y] (juce::Label& l, juce::Label& v, juce::Slider& s) {
        l.setBounds (12, y, 110, 16);
        v.setBounds (110, y, 98, 16);
        s.setBounds (8, y + 18, 204, 22);
        y += 52;
    };
    range (pitchLabel, pitchValue, pitchRange);
    range (lengthLabel, lengthValue, lengthRange);
    range (velLabel, velValue, velRange);
    y += 6;
    repeatsToggle.setBounds (8, y, 120, 24);
    y += 30;
    range (repRangeLabel, repValue, repRange);
    repChanceLabel.setBounds (12, y, 120, 16);
    repChance.setBounds (8, y + 18, 204, 22);
    seedLabel.setBounds (12, H - kFoot - 30, 196, 18);

    // Centre: grid + three lanes
    const int cx = kLeft + 12, cw = W - cx - 12;
    const int laneH = 46, gap = 6;
    const int lanesTop = H - kFoot - 12 - 3 * laneH - 2 * gap;
    grid.setBounds (cx, kTop + 12, cw, lanesTop - gap - (kTop + 12));
    velLane.setBounds (cx, lanesTop, cw, laneH);
    lenLane.setBounds (cx, lanesTop + laneH + gap, cw, laneH);
    repLane.setBounds (cx, lanesTop + 2 * (laneH + gap), cw, laneH);

    // Footer
    const int fy = H - kFoot;
    synthToggle.setBounds (8, fy + 26, 76, 28);
    waveLabel.setBounds (90, fy + 8, 84, 16);
    waveBox.setBounds (90, fy + 26, 84, 28);
    auto footKnob = [fy] (juce::Label& l, juce::Slider& s, int x) {
        l.setBounds (x, fy + 4, 64, 14);
        s.setBounds (x, fy + 18, 64, 56);
    };
    footKnob (cutoffLabel, cutoffKnob, 184);
    footKnob (resoLabel, resoKnob, 252);
    footKnob (decayLabel, decayKnob, 320);
    footKnob (volumeLabel, volumeKnob, 388);
    midiOutLabel.setBounds (510, fy + 8, 230, 16);
    midiOutBox.setBounds (470, fy + 26, 230, 28);
    refreshPorts.setBounds (706, fy + 26, 66, 28);
    channelLabel.setBounds (780, fy + 8, 64, 16);
    channelBox.setBounds (780, fy + 26, 64, 28);
    status.setBounds (852, fy + 8, W - 852 - 120, kFoot - 16);
}
