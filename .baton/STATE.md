# Midinja · Baton
Updated: 2026-10-18 21:40 · fable-5.1#24cf · 670c3f4
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
- 2026-10-18 · Chord part (approved prototype): two tabs Riff/Chords with on/off dots; Rhythm/Notes toggles + Mutate mode; equal roll rows; ghost notes of the other tab; a chord lane of blocks {start, length in beats, degree, type, inversion, lock, rhythm} with drag/resize/snap, + repeats the last chord; Progression, moods (Bright/Colour/Fable/Drama/Dark, diatonic only), styles (Jazz/R&B/Funk/House/D&B), typed progression; Chords roll crops a key-anchored voicing with named ghosts; the riff plays a chord tone on every chord start · user, via raptor 2026-10-18
- 2026-10-18 · App: built-in synths OFF by default; in the VST the host's Play/Stop, position and tempo drive playback, the plugin's Play is hidden there; Unlock bar clears every lock of the bar shown; a square + after the arrangement repeats the last bar · user
- 2026-10-18 · View: one chromatic roll, scale rows highlighted, out-of-scale rows dimmed (height under review: half by default, ?rows=full for full); edges step by semitone; the range keeps at least one scale note; drag snaps to scale rows, Alt-drag chromatic · user via raptor; replaces the Riff/Scale/All switch
- 2026-10-18 · Scale switch: unlocked notes keep their scale degree; colour notes move to the new scale's colour note nearest in pitch; an augmented-second pair moves as a unit to the new scale's pair (same one if it exists, else nearest), else to plain degrees; always computed from the original position, so any sequence of switches is reversible; scales of different sizes use a snapshot from before the first switch · agreed with the user
- 2026-10-18 · Locks: locked notes and hand-placed off-scale notes (locked automatically) never move on a scale switch; root and octave changes move everything, locks kept; the range cannot shrink past a locked note; imported off-scale notes are degree + alteration, not locked; an unlocked chromatic note is replaced by the next roll · agreed with the user
- 2026-10-18 · Pairs: shown as a thin teal arc between the two notes; one locked note splits the pair; a pair folds into the range as a whole · agreed with the user
- 2026-10-18 · Tabs: 3 like Riffer, tab 1 mono ch 1 on, tab 2 chords ch 2 off, tab 3 mono ch 3 off; parts/arrangement shared; key, root note and scale shared; octave and range per tab; an off tab doesn't play or export; export = one .mid, a track per tab · user decision
- 2026-10-18 · Chords: per part type a degree (I–VII), a type (triad, 7th, sus2, sus4; quality from the scale) and an inversion; default A,A′ = I (A root, A′ 1st inv), B = IV, C = V, 7th; one held chord per bar, dice vary only the rhythm; riffs start on the part's degree; strings pad in the app's synth · user decision
- 2026-10-18 · Progression dice: sets A A′ B C and the degrees (A = A′, B, C) from idiomatic lists per mode (Dorian i–IV, Mixolydian I–♭VII–IV, Lydian I–II, Phrygian i–♭II, minor i–VI–III–VII, Andalusian for harmonic minor / Phrygian dominant); C leads home; locked parts keep their degree; 7-note scales only · user decision
- 2026-10-18 · Extension: new empty bars are a copy of A moved by scale steps to their degree (A′ untransposed); whole bar shifted an octave before folding notes; C's last note goes to root or fifth; locks not copied; existing bars never overwritten · user decision

- 2026-10-18 · Highlights: four levels: primary (what the inspector edits) = orange frame + tint, exactly one on screen; context (the bar or part the primary belongs to) = thin muted outline; state (lock = icon + grey frame, tab on/off = the dot, the open tab = underline) never orange; chosen options share one teal segment style · four orange things at once were unreadable
- 2026-10-18 · Layout by levels (mockup first): song bar (Play+BPM, Key, Rate, Preset, Versions, undo, Import/Export, ⚙ for synth and MIDI out) → form (bar chips with lock and a part picker, + repeats the last bar; harmony under it: read-only strip on Riff, the lane on Chords) → track tabs → roll with Generator, Inspector, Advanced and ? on the right; palette and Unlock bar removed · the levels were mixed; JUCE port after the user approves

- 2026-10-18 · Beats tab: a third tab on channel 10, on by default; nine GM drums (Kick 36 … Crash 49) as rows; groove = kick on every beat, snare/clap on 2 and 4, a hats pattern, one open hat; A has no fill, A′ B C end with a fill (C over two beats), a crash opens the bar after a fill; built in the mockup AND the app at once ("build the VST for Windows right away") · overrides "mockup first" for this feature only, by the user

## Open issues
- !  VST3 MIDI bypassed FL (user, 2026-10-18): hardware on the interface played on every FL port because the plugin also opened a system MIDI port directly; VST instruments followed FL's port. Fixed in 82eb984 (host only), awaiting the user's confirmation in FL
-    Not tried yet: host tempo/transport sync. The mac zip (app + vst3) is built by CI but never opened.
-    Not clicked through by hand: Start-on-root toggle, root marker at narrow widths (preview)

## Waiting for you
- Chord part: the prototype's lane is built in the mockup (75125a6) and the app (4d7a1dd); the user tests both (bfunc.cc, latest zip) · since 2026-10-18
- Start-on-root: per-bar override (click the marker)? · since 2026-10-17
- Start-on-root: a middle setting "root on a strong beat"? · since 2026-10-17
- C in scales without a fifth falls back to ii (Locrian ♭II); keep it? · since 2026-10-17

## In progress
- (none)

## Next
1. Raptor deploys the mockup (75125a6); the user tests the mockup and the new Windows zip (4d7a1dd); report via raptor
2. User: the new Midinja-windows.zip from the latest pre-release (the app follows the mockup: tabs, chords, range edges, scale switch); report via raptor
3. User: FL check; then tag v0.2.0
4. Generation: augmented-second pairs resolve into tonic/fifth (♭6–7–1, 3–♭2–1, ♭3–♯4–5); colour notes on strong beats
5. AU later
