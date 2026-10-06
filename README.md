# OMNI for the M-VAVE FM-1

OMNI turns the M-VAVE FM-1 into a chord harp in the spirit of the Suzuki Omnichord. The 16 white
keys are the strum plate: run a finger across them and they ring out the chord. The 11 black keys
are the chord buttons. Behind them sit an organ-like chord with its bass, and the OM-84's ten
rhythms, which can play the bass and chord in time (auto bass sync).

It is meant to be simple: one screen, four knobs a page, no patterns to program.

## Playing

| | |
|---|---|
| White keys | The strum plate. Each key plucks one string of the chord. As on the Omnichord, each group of three keys is root, third and fifth, folded into an F#–F window, five groups up, with a root on top. |
| Black keys | Chord buttons (F C G, Dm Am, Em G7 E7, D7 Bb, A7 by default). Hold **ENV** for the minor, **LFO** for the 7th, both for the m7, the Omnichord's three rows. |
| PLAY | The rhythm, on and off. |
| REC | SYNC START: the rhythm waits for your first chord. |
| ARP | CHORD HOLD: the chord keeps playing after you let go (press the same button again to stop it). |
| OCT− / OCT+ | Move the strum plate an octave. |
| SELECT / ALGORITHM / PRESETS | Tempo / rhythm / transpose (−6 to +6). |
| KNOB 1–4 | The four values on the screen. |
| HOME, SEQ, FX, SEL, GLO | The pages: Play (voice 1, voice 2, sustain, chord level), Rhythm (rhythm, tempo, drums, auto bass), Sound (reverb, space, width, lo-fi), Chords (change the chord on any button: press it, then turn Root and Type), Setup (tune, MIDI out, key lights). EDIT steps through them. |
| SAVE | Saves. OMNI also saves by itself a few seconds after a change, once it's quiet. |

**Voice 1** is the Omnichord's shimmering harp, and **Voice 2** is its plain one. Mix them, and
set how long the strings ring with **Sustain**.

**MIDI out** (on by default): the strings on channel 1, the chord on 2, the bass on 3 and the
drums on 10. **MIDI in**: notes on channel 1 pluck the nearest string.

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

The rhythms, waves, drum sounds and envelope shapes come from
[Chordian](https://github.com/Jan125/pb.chordian), Jan125's OM-84 emulator (CC0). The platform
(hardware layer, USB, update loader, storage, installer) is
[Felucca](https://github.com/hugelton/Felucca)'s by Leo Kuroshita (Hügelton Instruments), by way of
[X0X](https://github.com/charlesvestal/fm1-x0x). GPL-3.0-only; see [LICENSING.md](LICENSING.md).

Omnichord is a trademark of Suzuki. OMNI isn't affiliated with or endorsed by Suzuki, M-VAVE or
Hügelton Instruments.
