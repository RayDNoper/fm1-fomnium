# FoMni for the M-VAVE FM-1

FoMni turns the M-VAVE FM-1 into a chord harp inspired by the Suzuki Omnichord. The 16 white
keys are the strum plate: run a finger across them and they ring out the chord. The 11 black keys
are the chord buttons. Behind them sit an organ-like chord with its bass, and the OM-84's ten
rhythms and a reggae one, which can play the bass and chord in time (auto bass sync).

It is meant to be simple: one screen, four knobs a page, no patterns to program.

[![FoMni running on the FM-1 (watch on YouTube)](https://img.youtube.com/vi/pahaM6nzucE/maxresdefault.jpg)](https://www.youtube.com/watch?v=pahaM6nzucE)

![FoMni's screen: a C chord strummed](docs/img/screen-strum.png) ![A minor held](docs/img/screen-chord.png) ![The rhythm page](docs/img/screen-rhythm.png)

- **Try it in your browser** (no FM-1 needed): <https://raydnoper.github.io/fm1-fomnium/emu/>
- **Install it** (Chrome or Edge, with the FM-1 plugged in): <https://raydnoper.github.io/fm1-fomnium/install/>
- **Download the firmware file**: from the [site](https://raydnoper.github.io/fm1-fomnium/)

Installing is at your own risk, though it's hard to get stuck: the web installer can put M-VAVE's
firmware back, and an FM-1 that keeps crashing starts in a safe mode.

## Playing

| | |
|---|---|
| White keys | The strum plate. Each key plucks one string of the chord. As on the Omnichord, each group of three keys is root, third and fifth, folded into an F#–F window, five groups up, with a root on top. Strings on the Setup page changes this: **Fixed** makes each white key play its own note, F3 to G5 in C major, moved by Transpose and Octave; **Relative** lays out the scale of the chord that is playing, up from its root. |
| Black keys | Chord buttons (F C G, Dm Am, Em G7 E7, D7 Bb, A7 in the first chord set). Hold **ENV** for the minor, **LFO** for the 7th, both for the m7, the Omnichord's three rows. |
| PLAY | The rhythm, on and off. |
| REC | SYNC START: the rhythm waits for your first chord. |
| ARP | CHORD HOLD: the chord keeps playing after you let go (press the same button again to stop it). |
| OCT− / OCT+ | Move the strum plate an octave. |
| SELECT | Tempo. |
| ALGORITHM | Rhythm. |
| PRESETS | Chord set, 1 to 8: what the black keys play. As they come, the sets are the same buttons in C, G, D, A, E, F, Bb and Eb. |
| KNOB 1–4 | The four values on the screen (see the pages below). |
| SAVE | Saves. FoMni also saves by itself a few seconds after a change, once it's quiet. |

### The pages

Each page puts four values on KNOB 1–4. EDIT steps through the pages.

- **HOME: Play.** Voice 1, Voice 2, Sustain, Chord level.
- **SEQ: Rhythm.** Rhythm, Tempo, Drums level, Auto bass (on: the bass and chord play in time with the rhythm).
- **FX: Sound.** Reverb (the strings' send), Space (the plate's size), Width, Chord rev (the chord and bass's send). The drums stay dry.
- **SEL: Chords.** Change the chord on any black key: press the key, then turn Root (KNOB 1) and Type (KNOB 2). The change goes into the chord set in use (PRESETS), so each set can hold a song's chords. KNOB 3 and 4 are Transpose (−6 to +6) and Octave.
- **GLO: Setup.** Strings (Triads: the white keys play the chord's tones; Fixed: their own notes, a scale; Relative: the scale of the chord that is playing), Shift (with Relative: the scale moved along the keys, −6 to +6 of its steps, for a melody that sits elsewhere than from the root up), Tune, MIDI out. Press GLO again for **Setup 2**: Lights (On: the key lights, and the unlit buttons glow dimly so they can be read; Keys: the key lights only; Off), Dark (the screen in dark colours), Harp and Chord (the voice of the strings and of the chord: the Omnichord's, or an FM sound).

**Voice 1** is the Omnichord's shimmering harp, and **Voice 2** is its plain one. Mix them, and
set how long the strings ring with **Sustain**.

### MIDI

MIDI comes in over USB and the TRS jack (the FM-1's jack is an input), and goes out over USB.

- **In, channel 1:** notes pluck the nearest string.
- **In, channel 2:** play a chord on a keyboard and it becomes the chord, as if you'd pressed a black key.
- **In, clock:** the rhythm follows MIDI clock and start / stop; the screen shows EXT for the tempo.
- **Out** (on by default, Setup page): the strings on channel 1, the chord on 2, the bass on 3, the drums
  on 10, and clock with start / stop when the rhythm runs on its own tempo.

### Firmware switcher

For the stage: a web page (`web/omni_switch.html`, `switch/` on the site) keeps several FM-1 firmware packages in the browser of a phone or laptop (FoMni-1's own, and any `.fwsc` you add, such as the stock firmware) and writes any of them to the FM-1 over USB with the installer's updater. Opened once on line, it works without a network and can be installed as an app. Do not switch in the middle of a set.

### Chord set editor

A web page (`web/omni_editor.html`, on the site next to the installer) shows the eight chord sets and changes them on the FM-1 over USB, in Chrome or Edge; it saves and loads them as a file. It talks SysEx ([web/EDITOR_PROTOCOL.md](web/EDITOR_PROTOCOL.md)), and works with the browser emulator too.

## Building and testing

The toolchain is the same as X0X's (see [BUILDING.md](BUILDING.md)):

```
tests/run_tests.sh      # the instrument, the platform pieces, the app in a simulator
./build.sh              # build/omni.fwsc
python3 tools/fm1_install.py build/omni.fwsc     # install it over USB
```

`build/host/omni_host SCRIPT OUTDIR` runs the whole app on a computer from a script
(`tests/scenarios/*.omni`), with audio and screenshots coming out. `web/emu/build.sh` builds the
same code for the browser.

## Credits

FoMni-1 comes from [charlesvestal/fm1-fomni](https://github.com/charlesvestal/fm1-fomni). This is a fork of it, with a
reggae rhythm, chord sets, scales on the strum plate, a dark screen, FM voices, a chord set editor and
a firmware switcher added. The FM voices follow the DIGITAL engine of
[SLOOP](https://github.com/isod89/sloop-fm1) (from Felucca).

The rhythms, waves, drum sounds and envelope shapes come from
[Chordian](https://github.com/Jan125/pb.chordian), Jan125's OM-84 emulator (CC0). The platform
(hardware layer, USB, update loader, storage, installer) is
[Felucca](https://github.com/hugelton/Felucca)'s by Leo Kuroshita (Hügelton Instruments), by way of
[X0X](https://github.com/charlesvestal/fm1-x0x). GPL-3.0-only; see [LICENSING.md](LICENSING.md).

Omnichord is a trademark of Suzuki. FoMni isn't affiliated with or endorsed by Suzuki, M-VAVE or
Hügelton Instruments.
