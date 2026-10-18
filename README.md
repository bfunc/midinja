# Midinja

**MIDI riff & chord generator** (pronounced *mi-DIN-ja*, MIDI + ninja).

A riff generator in the spirit of Audiomodern Riffer 3: roll a random riff in any key and
scale, lock the notes you like, re-roll the rest, play it through the built-in synth or a MIDI
port, and save it as a `.mid` file.

v0.2 is a **standalone app** and a **VST3 plugin** for Windows (and macOS, untested). Next: chords,
then AU. The project was called bRiffer up to v0.1.0.

## Download

- Latest build from `main`: [Releases → latest](../../releases/tag/latest)
- `Midinja-windows.zip`: `Midinja.exe` (nothing to install) and `Midinja.vst3`. Copy the `.vst3`
  folder to `C:\Program Files\Common Files\VST3\` and rescan plugins in the DAW.
- `Midinja-macos.zip`: `Midinja.app` and `Midinja.vst3` (Apple Silicon + Intel), not notarized, so
  macOS blocks them the first time. After unzipping, run once in Terminal:

  ```sh
  xattr -dr com.apple.quarantine /path/to/Midinja.app /path/to/Midinja.vst3
  ```

## Using it

One **part** is one bar of 4/4. There are four parts, **A A′ B C**, each on a chord degree of the
key (by default A and A′ on I, B on IV, C on V); A is the motif, A′ a variation of A (the
**Similarity** knob says how close), B a contrast, C leads home. Every bar starts on the root of its
chord (switch it off under Advanced). The degree is shown under each letter.

- **Tabs** (under the key row), three like Riffer: tab 1 is a riff on channel 1 and on, tab 2 holds
  chords on channel 2, tab 3 a second riff on channel 3; the last two start off. Every tab has its own
  parts, locks, octave, range and channel (Riff / Chords and the channel are selects on the tab, the
  switch turns it on or off); the key, the rate, the arrangement and the chord degrees are shared. An
  off tab does not play or export.
- **Dice** roll the part shown; the selector next to them says what changes: **Both**, **♩ Rhythm**
  only, **♪ Notes** only, or **~ Mutate** a little (click the % for 15 / 33 / 50). **All** does the
  same to all four parts of the tab (A first, then A′ B C from it). On a chord tab the dice only pick a
  new rhythm for the held chord. Space = dice, Shift+Space = All. Locked notes and locked parts stay.
- **Progression** sets the chord degrees of A A′ B C from the idiom of the mode (A = A′, C leads
  home) and moves the riffs to them; locked parts keep their degree. Seven-note scales only.
- **Arrangement** (left of the grid): up to 4 bars, each one a part; a new riff starts with one bar A.
  Click a part in the **palette** on the right to add it as a bar: an empty part comes in as a copy of
  A moved to its degree (A′ as it is, C ending on the root or the fifth). Click a bar to select it and
  edit its part (Play then loops only that bar; click again or Esc to deselect), Shift+click or the
  padlock locks the part, × removes the bar. Keys 1–4 show a part.
- **Chords** (tab 2): three selects above the grid give the part's **degree**, chord **type** (triad,
  7th, sus2, sus4; the quality comes from the scale) and **inversion**; the chord is voiced inside the
  tab's range and held on every hit. The degree also decides where the riffs of that part start.
- **Versions**: every roll and import is a version in the strip; hand edits change the current one.
  Click a thumbnail to go back, double-click to star it (starred versions are kept). Undo / redo
  (Ctrl+Z, Ctrl+Shift+Z) steps through everything; the buttons show with Advanced.
- **Grid**: every semitone of the tab's range is a row; scale rows are full height, the others half and
  dimmed. Click an empty cell to add a note (an off-scale one is locked at once), click a note to
  remove it, drag it to change the pitch (it snaps to scale rows, Alt for every row), **Shift+click**
  to lock it, **right-click** to make it longer. Teal notes are the colour notes of the scale; a thin
  arc joins an augmented-second pair. The **[+] [−]** at the top and bottom edges grow or shrink the
  range one semitone at a time (up to three octaves, never past a locked note or the last scale note);
  notes on a removed row fold back in by octave.
- **Key**: the root with its octave (the note moves every tab, **−** / **+** move the tab shown an
  octave; range and parts follow), the scale (a switch keeps every note's degree, moves colour notes
  and augmented-second pairs to the new scale's, and leaves locked notes alone; switching back restores
  the notes), **Rate** (1/8, 1/16 or 1/16 triplets per bar), **Density**, **Swing**, **Color** (how
  much of the scale's colour notes and signature moves the next roll uses) and **Surprise** (how often
  a rule is broken). Ctrl+click resets a knob.
- **Advanced**: length and velocity ranges, ratchets (off by default), the chord-root rule, the seed
  (type one and press Enter to roll the whole form from it), and the Vel / Len / Ratch lanes.
- **Play** loops the arrangement, or the selected bar. In a DAW the plugin follows the host's tempo and
  transport.
- **Synth**: the built-in synth, on by default: a mono saw / square for riff tabs (cutoff, resonance,
  decay, volume) and a strings pad for chord tabs. Switch it off to use another instrument. In the
  standalone app pick the audio device with **Options → Audio/MIDI Settings** (top-left of the window).
- **MIDI Out**: every tab that is on goes out as MIDI on its channel. The VST3 sends them only to the
  host, which routes them (in FL Studio: wrapper settings → MIDI → **Output port**, and the same
  **Input port** on the instrument that should play; hardware then follows FL's MIDI output settings).
  The Standalone app has a **MIDI Out** port picker instead (hardware, loopMIDI, IAC).
- **Import MIDI** (or drop a `.mid` on the window): up to 4 bars become A A′ B C of the tab shown,
  monophonic, on a 1/16 grid, key guessed. **Export MIDI** saves the arrangement as one `.mid` with a
  track per tab that is on. **Drag to DAW** drags the same file: drop it on FL Studio's playlist or a
  channel.

Settings and the arrangement are saved when the app closes, and with the project in a DAW.

## Web preview

`web/` is a browser preview of the generator: the same C++ core compiled to WebAssembly
(`web/build.sh`, needs Docker or emcc), with the app's interface and no sound. Live at
https://bfunc.cc/static/midinja/ (`web/publish.sh` copies it into the bfunc.cc repo). The preview is
the reference for the app's interface; the app adds the sound, MIDI out and drag-and-drop. Windows builds only for now.

## Building

Everything builds on GitHub Actions (see `.github/workflows/build.yml`). Locally:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

JUCE 8 is fetched by CMake. The generator, timing and MIDI-file code in `src/core` is plain
C++ and is covered by `tests/EngineTests.cpp`.

## License

AGPL-3.0 (JUCE is used under its AGPL option).
