# FoMni-1 editor protocol (SysEx over USB MIDI)

The firmware side is `firmware/src/app/editor.c`; the browser side is `web/fomni_ed.js`, used by the chord
set editor (`web/omni_editor.html`). The framing follows the editor protocol of SLOOP and Felucca (one reply
per request, 7-bit bytes), with FoMni-1's own header and commands.

## Framing

A request is `F0 7D 46 4F <cmd> <args...> F7`:

- `7D` is the non-commercial SysEx ID.
- `46 4F` is "FO".

Every request the device understands gets exactly one reply, with the same header and the same `<cmd>`.
A request it does not understand, or one with a value out of range, gets no reply and changes nothing.
Every data byte is 7 bit. The device sends nothing by itself: the editor asks for `STATE` now and then.

| Item | Encoding |
| --- | --- |
| set | 0..7: chord sets 1..8 |
| button | 0..10: the black keys, left to right |
| root | 0..11: C, C#, D, Eb, E, F, F#, G, Ab, A, Bb, B (before Transpose) |
| type | 0..8: major, m, 7, M7, m7, dim, aug, sus4, add9 |
| string | ASCII bytes, ended by a 0 byte |

## Commands (protocol 1)

| cmd | Request args | Reply args |
| --- | --- | --- |
| 1 INFO | — | protocol (1), sets (8), buttons (11), types (9), version string |
| 2 STATE | — | the set in use, then a 28-bit number in 4 bytes, low 7 bits first: it changes whenever a chord of any set, or the set in use, does (on the FM-1 or from the editor) |
| 3 SETS | — | the set in use, then for each set, for each button: root, type (176 bytes) |
| 4 PAD | set, button, root, type | set, button, root, type (as stored). A button that is sounding plays its new chord |
| 5 SET | set | the set in use (as PRESETS picks it) |
| 6 PUT | for each set, for each button: root, type (176 bytes) | 0. Every set is replaced |

A change marks the project to be saved, as a change on the FM-1 does: it saves by itself a few seconds
later, once it is quiet.

## The emulator

The browser emulator (`web/emu/`) answers the same messages: the editor page, open in another tab of the
same browser, reaches it over a `BroadcastChannel` named `fomni-editor` (`{ to: "fm1", data }` in,
`{ from: "fm1", data }` out; `data` is the bytes between `F0` and `F7`).

## Tests

`tests/scenarios/editor.omni` sends the messages to the app in the simulator;
`tests/host/editor_test.mjs` runs `web/fomni_ed.js` against the firmware built for the browser.
