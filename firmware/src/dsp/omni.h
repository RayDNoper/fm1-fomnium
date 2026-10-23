/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument: an OM-84 style chord harp. Three sections, as on the Omnichord:
 *
 *   HARP    16 strings, one per white key. Each plays a tone of the current chord: the tones
 *           folded into an F#..F window, root / third / fifth, five windows up, a root on top
 *           (the OM-84 / OM-108 strumplate, extended down one octave to fill 16 keys).
 *           Two voices, as the OM-84's: VOICE 1 shimmers (its tremolo), VOICE 2 is plain.
 *   CHORD   the chord itself (3 notes, F#3..F4) and its bass (C2..B2), held while a chord pad
 *           is down, or played in time by the rhythm (AUTO BASS SYNC).
 *   RHYTHM  the ten OM-84 rhythms and a reggae: bass drum, claves, hi-hat, cymbal, snare.
 *
 * The waves, envelopes and rhythm patterns follow Chordian (Jan125/pb.chordian, CC0), an OM-84
 * emulator; dsp/om_data.h and dsp/om_drums.h are generated from it (tools/import_chordian.py).
 *
 * Threads: om_render() runs in the audio interrupt. Everything else posts commands (om_cmd), which
 * the render drains at the start of a block; the state below marked (UI) is written by the render
 * and only read elsewhere. Float only, no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define OM_SR 44100.0f
#define OM_NSTR 16                     /* harp strings: the white keys */
#define OM_NCHORD 3                    /* the OM's chords are triads */

/* chord types: the OM-108's nine (all triads: a 7th drops its fifth, as on the Omnichord) */
enum { CH_MAJ, CH_MIN, CH_7, CH_MAJ7, CH_M7, CH_DIM, CH_AUG, CH_SUS4, CH_ADD9, CH_NTYPES };
extern const char *const OM_TYPE_NAME[CH_NTYPES];      /* "", "m", "7", "M7", "m7", "dim", "aug", "sus4", "add9" */
extern const char *const OM_NOTE_NAME[12];             /* "C", "C#"... */
extern const int8_t OM_TYPE_TONES[CH_NTYPES][3];

/* parameters: value ranges in om_param_info() */
enum {
    P_VOICE1, P_VOICE2, P_SUSTAIN, P_CHORD,          /* harp voice levels, harp sustain, chord + bass level */
    P_RHYTHM, P_TEMPO, P_RHYVOL, P_ABC,               /* rhythm, BPM, rhythm level, auto bass sync */
    P_REVERB, P_SPACE, P_TUNE, P_CREV,                /* the strings' reverb send, reverb size, tune (cents),
                                                       * the chord and bass's reverb send (the drums are dry) */
    P_TRANSPOSE, P_OCTAVE, P_WIDTH, P_MIDI,           /* semitones, harp octave, stereo width, MIDI out */
    P_NPARAMS
};
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} om_param_t;
const om_param_t *om_param_info(int i);
void om_param_text(int i, int v, char *buf);   /* the value as the screen shows it (10 bytes) */

/* the rhythms (dsp/om_data.h): per step, bass and chord commands (T trigger, R release, O one-shot,
 * N cut, '-' none), the bass note (R root, 5 fifth, 3 third) and the drums (bit: BD CL HH CY SD) */
typedef struct {
    const char *name;
    uint8_t len, alt, beat;                    /* beat: steps a beat (4; the Blues, in 12/8: 6) */
    const char *bass, *chord, *note;
    uint8_t drum[32];
} om_rhythm_t;
const om_rhythm_t *om_rhythm(int i);
/* after the rhythms that come with it (om_nrhythm of them), OM_NURHY of the player's own. One as the app
 * keeps it, OM_RHY_BYTES: its steps (1..32; 0: none, and it plays as silence), the steps of a beat (1..8),
 * its name (8 bytes, 0 after a shorter one), then for each of 32 steps the drums (bits) and
 * bass | chord << 3 | note << 6 (bass, chord: 0 nothing, 1 T, 2 R, 3 O, 4 N; note: 0 root, 1 fifth, 2 third).
 * om_rhythm_user() takes one in; it plays at once */
#define OM_NURHY 8
#define OM_RHY_NAME 8
#define OM_RHY_BYTES (2 + OM_RHY_NAME + 64)
int om_nrhythm(void);
int om_rhythm_ok(const uint8_t *p);            /* a rhythm as kept: every value in its range */
void om_rhythm_user(int slot, const uint8_t *p);

/* commands (main loop -> render) */
void om_init(void);
void om_set(int param, int value);
void om_chord(int root, int type);             /* the chord the harp, chord and bass play from now on */
/* and how it lies: INV_OM as the Omnichord has it (the chord's notes folded into F#3..F4, the root in the
 * bass), INV_ROOT, INV_1ST, INV_2ND with the root, the second or the third of its tones at the bottom of the
 * chord, and, for the last two, in the bass (C/E, C/G) */
enum { INV_OM, INV_ROOT, INV_1ST, INV_2ND, INV_N };
void om_chord_inv(int root, int type, int inv);
/* after the voice it comes with, for the strings (the harp) and for the chord (the organ): the OM_NDX patches of a DX7 bank, the
 * app's table (om_dx_tab, read when a note begins): each OM_DX_BYTES as a bank's SysEx holds it, six operators
 * and all. One that is empty (om_voice_ok: 0) plays as the harp, the organ. With one of them no more than
 * OM_DX_POLY strings sound: the one plucked longest ago gives way */
#define OM_NDX 32
#define OM_DX_BYTES 128
#define OM_DX_POLY 9
extern const uint8_t (*volatile om_dx_tab)[OM_DX_BYTES];
int om_voice_ok(int i);
int om_cvoice_ok(int i);
/* the strings' voice: 0 the OM's harp (Voice 1 + Voice 2), 1.. a patch of the bank (Voice 1: its level) */
#define OM_NSV 1                               /* (what it comes with: the harp) */
#define OM_NVOICES (OM_NSV + OM_NDX)
const char *om_voice_name(int i);
void om_voice(int i);
/* the chord's voice: 0 the OM's organ, 1.. a patch of the bank (the bass stays as it is) */
#define OM_NCV 1
#define OM_NCVOICES (OM_NCV + OM_NDX)
const char *om_cvoice_name(int i);
void om_cvoice(int i);
/* the strum plate: PL_TRIAD the chord's tones, or a scale (PL_*); shift: the chord's scale moved along the
 * keys by so many of its steps, -6..6 (PL_CHORD) */
void om_plate(int mode, int shift);
/* the chord sequence: OM_NTRK tracks of up to OM_NSEQ chords, each for so many beats (0: the track ends before
 * it; its top two bits: how the chord lies, INV_*); one track plays round while the rhythm runs. key: semitones added to every root. A chord from a button
 * plays until the sequence's next one. The tracks are the app's table (om_seq_tab, read as it plays): two
 * bytes a step, root | type << 4, then the beats. Another track (om_seq_track) starts when the one playing
 * comes round; at once if none plays */
#define OM_NTRK 8
#define OM_NSEQ 32
#define OM_SEQ_BEATS 16
extern const uint8_t (*volatile om_seq_tab)[OM_NSEQ][2];
/* and a melody for each step (the app's table, read as a step begins): a seed, 1..255, from which the melody
 * makes that step's tune over its chord, the same each time round; 0 none. A track with any plays them, and
 * rests on its other steps; one with none has the melody as ever (om_melody: its level for both) */
extern const uint8_t (*volatile om_seq_mel)[OM_NSEQ];
void om_seq(int on, int key);
void om_seq_track(int t);
/* the melody: while the rhythm runs and a chord sounds, a tune of its own over the chords, on the strings'
 * voice (one note at a time, MIDI channel 4). Made by rules (chord tones on the strong beats, mostly steps, a
 * leap filled in, a two-bar figure heard again and a phrase ending on the root or third), from a seed: the
 * same seed over the same chords plays the same tune. What: OM_MEL_LEVEL 0 off .. 100, OM_MEL_DENSITY 0
 * sparse .. 100 busy, OM_MEL_SEED 1..99, OM_MEL_REPEAT 0 wanders .. 100 keeps to its figure */
enum { OM_MEL_LEVEL, OM_MEL_DENSITY, OM_MEL_SEED, OM_MEL_REPEAT };
void om_melody(int what, int value);
void om_bass(int level);                       /* the bass against the chord: 0 none .. 50 as it comes .. 100 twice that */
void om_gate(int on);                          /* a chord pad held (1) / released (0): the chord section */
void om_strum(int string);                     /* pluck string 0..15 */
void om_play(int on);                          /* the rhythm: start (from its first step) / stop */
void om_panic(void);                           /* everything quiet, now */
/* MIDI clock in (the main loop passes it on): OM_CLK_TICK (F8), START, CONTINUE, STOP. While ticks keep
 * coming (the last within ~0.6 s) they drive the rhythm, 6 a step, and its own tempo stands aside;
 * otherwise the rhythm sends clock (24 a quarter note) and start / stop, with MIDI out on. */
enum { OM_CLK_TICK, OM_CLK_START, OM_CLK_CONTINUE, OM_CLK_STOP };
void om_clock(int msg);

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void om_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* MIDI out, from the render: the app sends it (USB); status, data 1, data 2 */
void om_midi_out(uint32_t st, uint32_t d1, uint32_t d2);

/* state for the screen (UI) */
extern volatile float om_str_level[OM_NSTR];   /* each string's envelope, 0..1 */
extern volatile uint8_t om_str_note[OM_NSTR];  /* the note each string plucks, for the chord now (MIDI) */
extern volatile float om_chord_level, om_bass_level;
extern volatile float om_mel_level;            /* the melody's note, as om_str_level */
extern volatile uint8_t om_mel_note;           /* and its note (MIDI) */
extern volatile uint8_t om_playing, om_step, om_steps;
extern volatile uint8_t om_drum_hit;           /* bits: drums hit since the UI last cleared it */
extern volatile uint8_t om_seq_trk, om_seq_at, om_seq_n;   /* the sequence: the track and step playing, and a
                                                             * count of the steps begun */
extern volatile uint8_t om_ext;                /* 1: following an external MIDI clock */
/* the notes a chord gives: harp strings (16, with transpose and octave), chord (3) and bass root */
/* the plate as a scale. Fixed: each white key its own note, F3..G5 in C (transpose moves the key).
 * Of the chord: its type's seven notes (OM_TYPE_SCALE) up from its root, C3..B3, two octaves and two more;
 * shift moves that along the keys by steps of the scale (-1: the first key plays the note under the root) */
enum { PL_TRIAD, PL_FIXED, PL_CHORD, PL_LINEAR, PL_NMODES };     /* (saved: new ones go last) */
extern const int8_t OM_TYPE_SCALE[CH_NTYPES][7];
void om_scale(int transpose, int octave, uint8_t harp[OM_NSTR]);
/* the chord's tones straight up the keys (PL_LINEAR): five octaves of them from its root in C2..B2, two
 * below the middle one and two above, and a root on top; no folding, so every key is higher than the last */
void om_linear(int root, int type, int transpose, int octave, uint8_t harp[OM_NSTR]);
void om_chord_scale(int root, int type, int transpose, int octave, int shift, uint8_t harp[OM_NSTR]);
void om_voicing(int root, int type, int transpose, int octave, uint8_t harp[OM_NSTR], uint8_t chord[OM_NCHORD],
                uint8_t *bass);
void om_voicing_inv(int root, int type, int inv, int transpose, int octave, uint8_t harp[OM_NSTR],
                    uint8_t chord[OM_NCHORD], uint8_t *bass);
