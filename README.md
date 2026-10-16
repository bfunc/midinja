# Midinja

**MIDI riff & chord generator** (pronounced *mi-DIN-ja*, MIDI + ninja).

A riff generator in the spirit of Audiomodern Riffer 3: roll a random riff in any key and
scale, lock the notes you like, re-roll the rest, play it through the built-in synth or a MIDI
port, and save it as a `.mid` file.

v0.1 is a **standalone app** for Windows and macOS. Next: a Musician mode, chords, then a VST3 / AU
plugin. The project was called bRiffer up to v0.1.0.

## Download

- Latest build from `main`: [Releases → latest](../../releases/tag/latest)
- `Midinja-windows.zip`: `Midinja.exe`, nothing to install.
- `Midinja-macos.zip`: `Midinja.app` (Apple Silicon + Intel). It is not notarized, so macOS blocks
  it the first time. After unzipping, run once in Terminal:

  ```sh
  xattr -dr com.apple.quarantine /path/to/Midinja.app
  ```

## Using it

- **DICE** rolls a new riff from the settings on the left (pitch range, length, velocity,
  repeats). Locked notes stay. **Infinity** rolls a new riff at the start of every cycle.
- **Grid**: click an empty cell to add a note, click a note to remove it, drag it to change the
  pitch, **Shift+click** to lock it, **right-click** to make it longer.
- **Lanes**: drag the bars for velocity, length and repeats (ratchets, 1-4; off by default,
  switch **Repeats** on in the left panel).
- **Transpose** moves the whole riff by scale steps. Root and scale changes move the notes into
  the new scale.
- **Synth**: the built-in mono synth plays through the audio output. Pick the audio device with
  **Options → Audio/MIDI Settings** (top-left of the window).
- **MIDI Out**: any MIDI port the system has, e.g. the MIDI OUT of your audio interface to a
  hardware synth, a USB synth, or a virtual port (loopMIDI on Windows, IAC on macOS) into a DAW.
  A port can usually be open in only one app at a time. Stop sends All Notes Off.
- **Export MIDI** saves the current pattern (one cycle) as a `.mid` file.

Settings and the pattern are saved when the app closes.

## Web preview

`web/` is a browser preview of the generator: the same C++ core compiled to WebAssembly
(`web/build.sh`, needs Docker or emcc), with the app's interface and no sound. Live at
https://bfunc.cc/static/midinja/ (`web/publish.sh` copies it into the bfunc.cc repo).

The preview is ahead of the app: Musician generation only (no Random mode, no Infinity), the
split dice, Mutate, a history of whole arrangements, and **parts A A′ B C** in a palette on the
right (one part is one bar; A′ is a variation of A with a Similarity knob, B leans on the IV
degree, C cadences to the root). Click a part to add it as a bar to the **arrangement** on the
left (up to 4 bars); a bar has a padlock (keeps its part out of rolls) and a ×. Select a bar to loop
only it. Ctrl+click resets a knob. A `.mid` dropped on the page is imported (up to 4 bars → A A′ B C,
monophonic, 1/16 grid, key guessed), and Export writes the arrangement as one `.mid`.

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
