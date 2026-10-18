# Midinja · Baton
Updated: 2026-10-18 15:04 · opus-5.5@raptor#af93 · 47a2c2e
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
- 2026-10-18 · View: one chromatic roll, scale rows highlighted, out-of-scale rows dimmed (height under review: half by default, ?rows=full for full); edges step by semitone; the range keeps at least one scale note; drag snaps to scale rows, Alt-drag chromatic · user via raptor; replaces the Riff/Scale/All switch
- 2026-10-18 · Scale switch: unlocked notes keep their scale degree; colour notes move to the new scale's colour note nearest in pitch; an augmented-second pair moves as a unit to the new scale's pair (same one if it exists, else nearest), else to plain degrees; always computed from the original position, so any sequence of switches is reversible; scales of different sizes use a snapshot from before the first switch · agreed with the user
- 2026-10-18 · Locks: locked notes and hand-placed off-scale notes (locked automatically) never move on a scale switch; root and octave changes move everything, locks kept; the range cannot shrink past a locked note; imported off-scale notes are degree + alteration, not locked; an unlocked chromatic note is replaced by the next roll · agreed with the user
- 2026-10-18 · Pairs: shown as a thin teal arc between the two notes; one locked note splits the pair; a pair folds into the range as a whole · agreed with the user

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
1. Scale switch without loss (agreed 2026-10-18, see Decisions): engine + mockup + app
2. Generation: augmented-second pairs resolve into the tonic or fifth (♭6–7–1, 3–♭2–1, ♭3–♯4–5), more often at the end of a part or before a strong beat; colour notes more often on strong beats next to the root
3. Port the approved mockup to the JUCE app (root chip, range as rows with +/−, one chromatic roll, no Transpose); Windows build only
4. User: FL check of the new build; then tag v0.2.0
5. Chords (mockup first), then AU
