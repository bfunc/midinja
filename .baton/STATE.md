# Midinja · Baton
Updated: 2026-10-20 22:38 · opus-5.5@raptor#af93 · 3f44612
What the project is and how to build it: README.md. This file is only the current state.
Sources: gh pr, gh run (CI)

## Now
Focus: the web mockup redesign (tracks: 6 riffs + Drums with own forms, one-row controls, parts grown from A / Groove), up to 15312e3. The JUCE app (v0.2, last port 18fdb7c) follows only after the user approves the mockup; the Windows zip still awaits the FL test.
Who: home builds the app (CI) and the preview; raptor relays user feedback; the user tests in FL Studio.

## Decisions (accepted, don't "fix")
Older feature decisions (roll, scales, locks, chords, tabs, MIDI out…): docs/decisions.md
- 2026-10-20 · Tracks: up to 6 riffs + 1 drum track, each its own length 1–4 bars, looping independently (polyrhythm like 3 vs 4 is fine); riffs keep parts A/A′/B/C · user decision
- 2026-10-20 · Drums: own parts independent of the riffs (groove, fills, crash-in, break, bridge…), a fill every 2 bars by default · user decision
- 2026-10-20 · Drum fills: Fill 1 / Fill 2 always end with their fill; no Fill on/off button · user: the toggle was confusing
- 2026-10-20 · Bar highlight: picking a part gives every bar using it the same soft outline, none looks selected; the strong style is only for a clicked bar (the one Play loops); an unused part lights no bar · user decision
- 2026-10-20 · Track header: no "Bar N · part" label or detail line; the part's details (chord degree, notes; drums: what the dice picked) live in the tooltips of its button and bars; Lock / Unlock stay · user agreed after review
- 2026-10-20 · Riff range: a new riff spans 2½ octaves (30 rows) from its root, e.g. E2–A#4; + still grows it to 3 octaves · user decision
- 2026-10-20 · Grid: fixed 1/16, Rate removed from the UI (the engine's rate stays, fixed at 1/16; older state with another rate loads as 16 steps) · user decision
- 2026-10-20 · Note length: up to a whole bar (16 steps, never past the next note or the bar end); a click uses the last length, drag right from an empty cell to draw, drag a note's right edge to resize; right-click no longer cycles · user decision
- 2026-10-20 · Duration: the percent of a note's own length that sounds (100% legato, low = pluck), applied in ticks (can be shorter than 1/16); the dice take it from a Duration range (default 50–100%); manual notes start at 100%; Length range stays in Advanced · user decision
- 2026-10-20 · Ratchets: up to 5 hits that split the note's length; duration does not apply to a ratcheted note; drawn as separate hits · user decision
- 2026-10-20 · Roll edit modes (Advanced): Notes · Duration · Velocity · Ratchets as bars over the dimmed roll, replacing the lanes under it; each mode's dice range sits in its header; Drums: Notes · Velocity only · user decision (overlay like Riffer)
- 2026-10-20 · Drum kit: a Kit view on the Drums track sets the note each sound sends (default GM, duplicates warned); hits stay keyed by GM inside · user decision (note mapping only, no CC)
- 2026-10-20 · Layout: controls back in one row above the roll (no right column); new tabs/track design; chords hidden for now · user decision
- 2026-10-20 · Web preview hosting: none (no bfunc.cc, no GitHub Pages); local server or StackBlitz only · user decision
- 2026-10-13 · Builds: only on GitHub Actions · user keeps the laptop free of toolchains
- 2026-10-16 · Preview: a UI mockup, no sound · user decision
- 2026-10-17 · Release: rolling "latest" pre-release from CI; a tagged release only after the user confirms it works in FL · user said "Defaults ok"
- 2026-10-18 · New UI features go to the web mockup first (served on Home at localhost), into the JUCE app only after the user approves the mockup · user: "continue to ui tasks, for this only web maket"
- 2026-10-19 · Chords per bar (option 2): the chord lane stays one continuous lane over the form; the Chords tab shows bar numbers and their chords only, no part controls; the part selector A/A′/B/C lives on Riff and Beats · the user chose it over per-part chords (option 1) the same day
- 2026-10-19 · Level 2 fixed height: the bar row and the chord lane keep the same size on every tab; the active level is shown by colour and outline only · the editor jumped ~30 px between Riff and Chords
- 2026-10-19 · One "+" on level 2: the bar-row + adds a bar (on Chords with a copy of the previous bar's chords); the chord lane has no + of its own, a chord inside a bar comes from a double-click · two + on Chords were redundant
- 2026-10-20 · New parts: a roll makes only the first part (A / Groove); the others are built from it once, on first click (riffs via core.part, drums from the Groove's choices); dice on them = build again from it; All rebuilds only parts already made · user's idea, "из A"

## Open issues
- !  VST3 MIDI bypassed FL (user, 2026-10-18): hardware on the interface played on every FL port because the plugin also opened a system MIDI port directly; VST instruments followed FL's port. Fixed in 82eb984 (host only); 2026-10-18 the picker returns, exclusive host-or-device (see docs/decisions.md), awaiting the user's FL check with the Minitaur
-    Not tried yet: host tempo/transport sync. The mac zip (app + vst3) is built by CI but never opened.
-    Not clicked through by hand: Start-on-root toggle, root marker at narrow widths (preview)

## Waiting for you
- Chord part: the prototype's lane is built in the mockup (75125a6) and the app (4d7a1dd); the user tests both (local mockup, latest zip) · since 2026-10-18
- Start-on-root: per-bar override (click the marker)? · since 2026-10-17
- Start-on-root: a middle setting "root on a strong beat"? · since 2026-10-17
- C in scales without a fifth falls back to ii (Locrian ♭II); keep it? · since 2026-10-17

## In progress
(none)

## Next
1. User reviews the redesigned mockup (tracks: 6 riffs + Drums, one-row controls, drum parts, chords hidden) on StackBlitz; then decide whether the JUCE app follows
2. User tests the Windows zip from the latest pre-release (18fdb7c) in FL: the parts selector on the tab row, Chords bars with ⧉, the bar + (copies chords on Chords), the ▾ part picker; plus the earlier Beats on channel 10, fills and the crash, the MIDI Out picker reaching the Minitaur directly; report via raptor.
3. Tag v0.2.0 after the user's FL confirmation
4. Generation: augmented-second pairs resolve into tonic/fifth (♭6–7–1, 3–♭2–1, ♭3–♯4–5); colour notes on strong beats
5. AU later
6. JUCE app: port the note length rework (fixed 1/16 grid, notes up to a bar, drag to draw/resize, last-used length) and Duration / ratchets up to 5 / edit modes / drum kit after the user approves them in the mockup; the core then needs duration and the caps raised (kMaxLength 8 → 16, kMaxRepeats 4 → 5), which the mockup works around in JS (afterCore)
