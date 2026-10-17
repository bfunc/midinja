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

One **part** is one bar of 4/4. There are four parts, **A A′ B C**: A is the motif on I, A′ a variation
of A (the **Similarity** knob says how close), B a contrast on IV, C sits on V and cadences to the
root. Every bar starts on the root of its chord (switch it off under Advanced).

- **Dice** roll the part shown; the selector next to them says what changes: **Both**, **♩ Rhythm**
  only, **♪ Notes** only, or **~ Mutate** a little (click the % for 15 / 33 / 50). **All** does the
  same to all four parts (A first, then A′ B C from it). Space = dice, Shift+Space = All. Locked notes
  and locked parts stay.
- **Arrangement** (left of the grid): up to 4 bars, each one a part. Click a bar to select it and edit
  its part (Play then loops only that bar; click again or Esc to deselect), Shift+click or the padlock
  locks the part, × removes the bar. The **palette** on the right holds the four parts; click one to add
  it as a bar. Keys 1–4 show a part.
- **Versions**: every roll and import is a version in the strip; hand edits change the current one.
  Click a thumbnail to go back, double-click to star it (starred versions are kept). Undo / redo
  (Ctrl+Z, Ctrl+Shift+Z) steps through everything; the buttons show with Advanced.
- **Grid**: click an empty cell to add a note, click a note to remove it, drag it to change the pitch,
  **Shift+click** to lock it, **right-click** to make it longer. **View** shows the pitches the riff
  uses, the scale, or every semitone. Teal notes are the colour notes of the scale.
- **Key**, **Rate** (1/8, 1/16 or 1/16 triplets per bar), **Density**, **Swing**, **Transpose** (scale
  steps), **Color** (how much of the scale's colour notes and signature moves the next roll uses) and
  **Surprise** (how often a rule is broken). Ctrl+click resets a knob.
- **Advanced**: pitch, length and velocity ranges, ratchets (off by default), the chord-root rule, the
  seed (type one and press Enter to roll the whole form from it), and the Vel / Len / Ratch lanes.
- **Play** loops the arrangement, or the selected bar. In a DAW the plugin follows the host's tempo and
  transport.
- **Synth**: the built-in mono synth (Saw / Square, cutoff, resonance, decay, volume), on by default;
  switch it off to use another instrument. In the standalone app pick the audio device with **Options →
  Audio/MIDI Settings** (top-left of the window).
- **MIDI Out**: the notes also go out as MIDI on the chosen **channel** (1–16). The VST3 sends them to
  the host (in FL Studio set the plugin's MIDI output port and give another instrument that input
  port); both the app and the plugin can also open a system MIDI port (hardware, loopMIDI, IAC).
- **Import MIDI** (or drop a `.mid` on the window): up to 4 bars become A A′ B C, monophonic, on a 1/16
  grid, key guessed. **Export MIDI** saves the arrangement as one `.mid`. **Drag to DAW** drags the
  same file: drop it on FL Studio's playlist or a channel.

Settings and the arrangement are saved when the app closes, and with the project in a DAW.

## Web preview

`web/` is a browser preview of the generator: the same C++ core compiled to WebAssembly
(`web/build.sh`, needs Docker or emcc), with the app's interface and no sound. Live at
https://bfunc.cc/static/midinja/ (`web/publish.sh` copies it into the bfunc.cc repo). The preview is
the reference for the app's interface; the app adds the sound, MIDI out and drag-and-drop.

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
