# Jambox FM-1 editor protocol (SysEx over USB MIDI)

The firmware side is `firmware/src/app/editor.c`; the browser side is `web/fomni_ed.js`, used by the
editor of chord sets and rhythms (`web/omni_editor.html`). The framing follows the editor protocol of SLOOP and Felucca (one reply
per request, 7-bit bytes), with Jambox FM-1's own header and commands.

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
| lies | 0..3: how the chord lies. 0 as the Omnichord folds it, 1 root position, 2 and 3 the first and second inversion (that tone at the bottom of the chord and in the bass) |
| type+ | type, with lies in bits 4 and 5: `type \| lies << 4` |
| string | ASCII bytes, ended by a 0 byte |

## Commands (protocol 3)

Protocol 3 added the rhythms (commands 8 to 12, and two more bytes in INFO). Protocol 2 added how a chord lies. Protocol 1's messages still work: `SETS` answers as it did, and a plain
type in `PAD` or `PUT` is a chord that lies as the Omnichord folds it.

| cmd | Request args | Reply args |
| --- | --- | --- |
| 1 INFO | — | protocol (3), sets (8), buttons (11), types (9), version string, then (protocol 3) the rhythms it comes with (11) and the places for the player's own (8) |
| 2 STATE | — | the set in use, then a 28-bit number in 4 bytes, low 7 bits first: it changes whenever a chord of any set, or the set in use, does (on the FM-1 or from the editor) |
| 3 SETS | — | the set in use, then for each set, for each button: root, type (176 bytes) |
| 4 PAD | set, button, root, type+ | set, button, root, type+ (as stored). A button that is sounding plays its new chord |
| 5 SET | set | the set in use (as PRESETS picks it) |
| 6 PUT | for each set, for each button: root, type+ (176 bytes) | 0. Every set is replaced |
| 7 SETS2 | — | as SETS, with type+ for type (protocol 2) |
| 8 RHY | rhythm | rhythm, then the rhythm (106 bytes, below). Any rhythm can be read |
| 9 RHYPUT | place 0..7, then the rhythm (106 bytes) | place, 0. One of the player's own is written; with no steps it is taken away. It plays at once |
| 10 RHYSEL | rhythm | the rhythm in use (as ALGORITHM picks it). An empty place gets no reply |
| 11 RSTATE | — | the rhythm in use, 1 while it plays, then a 28-bit number as STATE's, which changes with any of the player's rhythms |
| 12 PLAY | 1 or 0 | the same: the rhythm starts, stops |

A rhythm's number is 0..10 for the ones Jambox FM-1 comes with, then 11..18 for the player's eight places.

A rhythm on the wire is 106 bytes:

| Bytes | |
| --- | --- |
| 1 | its steps, 1..32 (sixteenth notes); 0: an empty place |
| 1 | the steps of a beat, 1..8 |
| 8 | its name, ASCII, 0 after a shorter one |
| 32 x 3 | for each step: the drums (bit 0 bass drum, 1 claves, 2 hi-hat, 3 cymbal, 4 snare); the bass (0 nothing, 1 play and hold, 2 let go, 3 play once, 4 cut) with its note in bits 3 and 4 (0 the chord's root, 1 fifth, 2 third); the chord (0..4, as the bass) |

A change marks the project to be saved, as a change on the FM-1 does: it saves by itself a few seconds
later, once it is quiet.

## The emulator

The browser emulator (`web/emu/`) answers the same messages: the editor page, open in another tab of the
same browser, reaches it over a `BroadcastChannel` named `fomni-editor` (`{ to: "fm1", data }` in,
`{ from: "fm1", data }` out; `data` is the bytes between `F0` and `F7`).

## Tests

`tests/scenarios/editor.omni` sends the messages to the app in the simulator;
`tests/host/editor_test.mjs` runs `web/fomni_ed.js` against the firmware built for the browser.
