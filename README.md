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

One **part** is one bar of 4/4. There are four parts, **A A′ B C** (the melody form): A is the
motif, A′ a variation of A (the **Similarity** knob says how close), B a contrast, C leads home.
The harmony lives in the **chord lane**: every bar starts on the root of the chord it starts in
(switch it off under Advanced), and wherever a chord starts the riff plays one of its tones.

- **Tabs**: **Riff** (channel 1, on), **Chords** (channel 2, off) and **Beats** (channel 10, on). Click the name to show a tab,
  the dot to turn it on or off; an off tab does not play or export. Both share the key, the rate, the
  arrangement and the chord lane; each has its own octave, range and channel (the Riff tab its
  parts). The shown tab's channel and range sit on the right of the tab bar.
- **Dice**: **♩ Rhythm** and **♪ Notes** are toggles (both on = a new riff, one on = only that);
  **~ Mutate** is a mode that switches them off and cycles 15 / 33 / 50 % on a second click. The
  dice roll the part shown; **All** does the same to all four parts (A first, then A′ B C from it).
  On the Chords tab the dice pick a new rhythm for the selected chord, All for every unlocked one.
  Space = dice, Shift+Space = All. Locked notes, parts and chords stay.
- **Arrangement** (left): up to 4 bars, each one a part; a new riff starts with one bar A. The
  square **+** after the last bar repeats it (A → A A); clicking a letter in the **palette** on the
  right adds that part (an empty part comes in as a copy of A moved to its bar's chord). Click a bar to
  select it and edit its part (Play then loops only that bar; Esc deselects), Shift+click or the
  padlock locks the part, × removes the bar and its chords. **Unlock bar** removes every lock of the
  bar shown. Keys 1–4 show a part.
- **Chord lane**: blocks with a start and a length in beats. Drag a block to move it, drag its edge
  to resize it (one-beat snap, blocks never overlap, a gap = no chord). On each block: lock, ↕ next
  inversion, ↻ another chord, ×. The square **+** on the left repeats the last chord (and adds a bar
  below four); double-click the empty lane to add a chord there; Del removes the selected one.
- **Generators** under the lane: **⚄ Progression** picks one typical for the scale; the moods
  **Bright, Colour, Fable, Drama, Dark** use only moves that exist in the current scale (a mood with
  none is disabled); the styles **Jazz, R&B, Funk, House, Drum & Bass** set chord types and a rhythm;
  **type a progression** (`i IV i VII`; more chords than bars share the bars) and Apply. Locked
  chords are kept.
- **Selected chord**: degree, type (triad, 7th, 9th, sus2, sus4, add9; the quality comes from the
  scale), inversion (3rd only for four-note chords), rhythm (Whole … Funk 16ths), rhythm to all, lock.
- **Chords roll**: the range (+/− at the edges, one to three octaves) only crops the view; the voicing
  sits around a fixed point of the key, so cut notes show as named ghosts above and below the roll.
  The other tab's notes show faintly underneath on either tab.
- **Beats**: a drum grid of nine GM drums (Kick 36, Snare 38, Clap 39, Closed hat 42, Low tom 45, Open hat 46,
  Mid tom 47, Crash 49, High tom 50). Each part is a groove: kick on every beat, snare (sometimes a clap)
  on 2 and 4, a hats pattern, one open hat; **A′, B and C end with a fill** on the last beat (C on the
  last two) with snare and toms going down, and the bar after a fill starts with a crash (shown dashed).
  Click = hit / remove, Shift+click = lock, right-click = soft / normal / accent. The dice pick other hats
  and another fill for the part shown, All for every unlocked part; **Fill on / off** per part (A has
  none). The key, the octave and the range do not apply here.
- **Riff roll**: every semitone of the range is a row, out-of-scale rows dimmed. Click an empty cell to
  add a note (an off-scale one is locked at once), click a note to remove it, drag it to change the
  pitch (it snaps to scale rows, Alt for every row), **Shift+click** to lock it, **right-click** to
  make it longer. Teal notes are the colour notes of the scale; a thin arc joins an augmented-second
  pair. The **[+] [−]** edges grow or shrink the range one semitone at a time; notes on a removed row
  fold back in by octave.
- **Key**: the root with its octave (the note moves both tabs, **−** / **+** move the tab shown an
  octave), the scale (a switch keeps every note's degree, moves colour notes and augmented-second
  pairs, leaves locked notes alone; chords keep their degree and take the new quality), **Rate**,
  **Density**, **Swing**, **Color** and **Surprise**. Ctrl+click resets a knob.
- **Advanced**: length and velocity ranges, ratchets (off by default), the chord-root rule, the seed,
  and the Vel / Len / Ratch lanes.
- **Play** (standalone) loops the arrangement, or the selected bar. In a DAW the plugin follows the
  host's Play / Stop, position and tempo; its own Play button is hidden there.
- **Synth**: the built-in preview synths are **off by default** (a mono saw / square for the riff, a
  strings pad for the chords); switch them on to hear the app without another instrument. In the
  standalone app pick the audio device with **Options → Audio/MIDI Settings**.
- **MIDI Out**: every tab that is on goes out as MIDI on its channel. The picker in the footer says
  where: in the VST3 **Host** (default; the host routes it, in FL Studio: wrapper settings → MIDI →
  **Output port**, and the same **Input port** on the instrument that should play) or one system
  device (a hardware synth, loopMIDI, IAC), which then gets everything and the host nothing. The
  choice is saved with the project; a missing device falls back to Host. The Standalone app picks a
  device or None.
- **Import MIDI** (or drop a `.mid` on the window): up to 4 bars become A A′ B C of the Riff tab,
  monophonic, on a 1/16 grid, key guessed; the lane gets one chord per bar. **Export MIDI** saves the
  arrangement as one `.mid` with a track per tab that is on. **Drag to DAW** drags the same file.

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
