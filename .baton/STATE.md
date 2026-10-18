# Midinja · Baton
Updated: 2026-10-18 14:12 · fable-5.1@home#24cf · 2d12bd4
What the project is and how to build it: README.md. This file is only the current state.
Sources: gh pr, gh run (CI)

## Now
Focus: the **real build** v0.2: the JUCE app follows the web preview; Standalone with sound + VST3 with drag-and-drop of the arrangement into FL Studio. Built and checked on Home; waiting for the user to try it in FL.
Who: home builds the app (CI) and the preview; raptor relays user feedback; the user tests in FL Studio. No deploys to bfunc.cc.

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
- 2026-10-18 · New UI features go to the web mockup first (served on Home at localhost), into the JUCE app only after the user approves the mockup · user: "continue to ui tasks, for this only web maket"
- 2026-10-18 · Root+range: one root chip (note + octave) that the range and all four parts follow; range = the visible rows of the roll with +/− at each edge, one step = one row of the view, min 1 note, max ~3 octaves; shrinking folds notes in by octave, locked notes never change; the bottom may go below the root · agreed with the user via raptor; replaces the Octave control and the Advanced Note range of 4ed5944
- 2026-10-18 · Transpose: removed from the mockup, the root chip moves everything · user decision ("No need in transpose")

## Open issues
- !  VST3 MIDI bypassed FL (user, 2026-10-18): hardware on the interface played on every FL port because the plugin also opened a system MIDI port directly; VST instruments followed FL's port. Fixed in 82eb984 (host only), awaiting the user's confirmation in FL
-    Not tried yet: host tempo/transport sync. The mac zip (app + vst3) is built by CI but never opened.
-    Not clicked through by hand: Start-on-root toggle, root marker at narrow widths (preview)

## Waiting for you
- Start-on-root: per-bar override (click the marker)? · since 2026-10-17
- Start-on-root: a middle setting "root on a strong beat"? · since 2026-10-17
- C in scales without a fifth falls back to ii (Locrian ♭II); keep it? · since 2026-10-17

## In progress
- (none)

## Next
1. User: new Midinja-windows.zip from the latest pre-release; in FL the hardware should now play only through FL's port; report via raptor
2. User: review the mockup at http://localhost:8765/ (root chip, range = rows with +/− at the edges, no Transpose); then the port to the JUCE app
3. Tag v0.2.0 when the user confirms the FL fix
4. Chords (mockup first), then AU
