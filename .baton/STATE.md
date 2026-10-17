# Midinja · Baton
Updated: 2026-10-17 21:08 · opus-5.5@raptor#af93 · f5047cf
What the project is and how to build it: README.md. This file is only the current state.
Sources: gh pr, gh run (CI)

## Now
Focus: the **form** feature in the web preview (A A′ B C, A → A′ variations), designed in the browser first, then ported to JUCE and the VST3.
Who: home builds the preview on localhost; raptor relays user feedback. No deploys to bfunc.cc.

## Decisions (accepted, don't "fix")
- 2026-10-13 · Builds: only on GitHub Actions · user keeps the laptop free of toolchains
- 2026-10-16 · Preview: a UI mockup, no sound · user decision
- 2026-10-16 · Form: ≤4 bars, 1 part = 1 bar 4/4, 16 steps; import mono (chords → lowest note), 1/16 grid, cut at bar lines, velocity kept, key guessed · agreed in discussion
- 2026-10-16 · Modes: Musician only, no Infinity; Random stays in the engine for JUCE · user decision
- 2026-10-17 · Chord roots: each bar starts on its chord root (A, A′ = I; B = IV; C = V), imports untouched · user decision
- 2026-10-17 · Start-on-root toggle: stays under Advanced (not the main screen); the grid shows a root marker on step 1 of each bar that the rule holds · user decision after discussion
- 2026-10-17 · Mutate amounts: 15 / 33 / 50 %, default 15 · user corrected this several times; final
- 2026-10-17 · Versions: dice, import, Rhythm, Notes and Mutate each create a new version; manual edits change the current one; undo is separate · user decision, confirmed again 2026-10-17
- 2026-10-17 · Deploys: none to bfunc.cc until the user asks · user decision
- 2026-10-17 · UI: the JUCE app (Standalone and VST3) follows web/index.html, layout and behaviour, colours unchanged · approved by the user ("Use web html reference to build. It is approved")
- 2026-10-17 · Plugin sound: built-in preview synth with an on/off toggle, and the VST3 also sends MIDI to the host · user decision
- 2026-10-17 · MIDI channel: selectable 1-16 in Standalone and VST3, saved with settings / plugin state, used for live MIDI out and the exported or dragged .mid · user request
- 2026-10-17 · Release: rolling "latest" pre-release from CI; a tagged release only after the user confirms it works in FL · user said "Defaults ok"

## Open issues
- !  JUCE app is behind the preview: no form, arrangement, versions, colour sets, chord roots (?)
-    Not clicked through by hand: Start-on-root toggle, root marker at narrow widths

## Waiting for you
- Start-on-root: per-bar override (click the marker)? · since 2026-10-17
- Start-on-root: a middle setting "root on a strong beat"? · since 2026-10-17
- C in scales without a fifth falls back to ii (Locrian ♭II); keep it? · since 2026-10-17

## In progress
- fable-5.1@home#24cf · Real build: Standalone with sound + VST3 (drag-and-drop .mid of the arrangement into FL Studio, MIDI channel 1-16 selectable in both, Export .mid kept) · since 2026-10-17 22:07

## Next
1. More preview iterations on user feedback (home)
2. Port form + new UI to JUCE, then VST3
