/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument (omni.h). One compilation unit, -O2. Waves, envelope shapes, voicing and rhythms
 * after Chordian (Jan125/pb.chordian, CC0 1.0); its rates were written for 96 kHz and are converted
 * here where they are per sample (the filters), not where they are per second (the envelopes). */
#include "omni.h"
#include "fastmath.h"
#include "om_data.h"
#include "om_drums.h"

#ifdef OM_HOST
#define OM_POOL
#else
#define OM_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

const char *const OM_TYPE_NAME[CH_NTYPES] = {"", "m", "7", "M7", "m7", "dim", "aug", "sus4", "add9"};
const char *const OM_NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
/* the three tones (semitones over the root); the third tone of a 7th is the 7th, as on the OM */
const int8_t OM_TYPE_TONES[CH_NTYPES][3] = {
    {0, 4, 7}, {0, 3, 7}, {0, 4, 10}, {0, 4, 11}, {0, 3, 10}, {0, 3, 6}, {0, 4, 8}, {0, 5, 7}, {0, 4, 2}};

/* the scale that goes with each type: major, natural minor, Mixolydian, major, Dorian, Locrian with a
 * major 2nd, Lydian augmented, Mixolydian, major */
const int8_t OM_TYPE_SCALE[CH_NTYPES][7] = {
    {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 4, 5, 7, 9, 10}, {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 9, 10},
    {0, 2, 3, 5, 6, 8, 10}, {0, 2, 4, 6, 8, 9, 11}, {0, 2, 4, 5, 7, 9, 10}, {0, 2, 4, 5, 7, 9, 11}};

static const om_param_t PARAMS[P_NPARAMS] = {
    {"Voice 1", 0, 100, 80}, {"Voice 2", 0, 100, 45}, {"Sustain", 0, 100, 50}, {"Chord", 0, 100, 60},
    {"Rhythm", 0, OM_NRHYTHM + OM_NURHY - 1, 0}, {"Tempo", 60, 240, 116}, {"Drums", 0, 100, 70}, {"Auto bass", 0, 1, 1},
    {"Reverb", 0, 100, 30}, {"Space", 0, 100, 55}, {"Tune", -50, 50, 0}, {"Chord rev", 0, 100, 25},
    {"Transpose", -6, 6, 0}, {"Octave", -1, 1, 0}, {"Width", 0, 100, 60}, {"MIDI out", 0, 1, 1},
};

/* the player's own rhythms, as the render reads them (om_rhythm_user unpacks one; an empty one is silence) */
static char urh_txt[OM_NURHY][3][33], urh_name[OM_NURHY][OM_RHY_NAME + 1];
static om_rhythm_t urh[OM_NURHY];

int om_nrhythm(void) { return OM_NRHYTHM; }

int om_rhythm_ok(const uint8_t *p)
{
    int i;
    if (!p[0])
        return 1;
    if (p[0] > 32u || p[1] < 1u || p[1] > 8u)
        return 0;
    for (i = 0; i < OM_RHY_NAME; i++)
        if (p[2 + i] && (p[2 + i] < 32u || p[2 + i] > 126u))
            return 0;
    for (i = 0; i < 32; i++) {
        uint32_t d = p[2 + OM_RHY_NAME + 2 * i], b = p[3 + OM_RHY_NAME + 2 * i];
        if (d > 31u || (b & 7u) > 4u || (b >> 3 & 7u) > 4u || b >> 6 > 2u)
            return 0;
    }
    return 1;
}

void om_rhythm_user(int slot, const uint8_t *p)
{
    static const char CMD[8] = {'-', 'T', 'R', 'O', 'N', '-', '-', '-'}, NOTE[4] = {'R', '5', '3', 'R'};
    om_rhythm_t *r;
    int i, on;
    if ((unsigned)slot >= OM_NURHY)
        return;
    r = &urh[slot];
    on = p && p[0] && om_rhythm_ok(p);
    for (i = 0; i < 32; i++) {
        uint32_t d = on ? p[2 + OM_RHY_NAME + 2 * i] : 0u, b = on ? p[3 + OM_RHY_NAME + 2 * i] : 0u;
        r->drum[i] = (uint8_t)d;
        urh_txt[slot][0][i] = CMD[b & 7u];
        urh_txt[slot][1][i] = CMD[b >> 3 & 7u];
        urh_txt[slot][2][i] = NOTE[b >> 6];
    }
    for (i = 0; i < OM_RHY_NAME; i++)
        urh_name[slot][i] = on ? (char)p[2 + i] : "User 1"[i < 6 ? i : 6];
    if (!on || !urh_name[slot][0]) {
        for (i = 0; i < 7; i++)
            urh_name[slot][i] = "User 1"[i];
        urh_name[slot][5] = (char)('1' + slot);
    }
    r->name = urh_name[slot];
    r->bass = urh_txt[slot][0];
    r->chord = urh_txt[slot][1];
    r->note = urh_txt[slot][2];
    r->alt = 0;
    r->beat = (uint8_t)(on ? p[1] : 4);
    r->len = (uint8_t)(on ? p[0] : 16);
}

const om_rhythm_t *om_rhythm(int i)
{
    if (i >= OM_NRHYTHM && i < OM_NRHYTHM + OM_NURHY)
        return &urh[i - OM_NRHYTHM];
    return &OM_RHYTHM[(unsigned)i < OM_NRHYTHM ? i : 0];
}

const om_param_t *om_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }

static void itoa_s(int v, char *b)
{
    char t[8];
    int n = 0, neg = v < 0;
    if (neg)
        v = -v;
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 6);
    if (neg)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

void om_param_text(int i, int v, char *b)
{
    static const char *const ONOFF[2] = {"Off", "On"};
    const char *s = 0;
    if (i == P_RHYTHM)
        s = om_rhythm(v)->name;
    else if (i == P_ABC || i == P_MIDI)
        s = ONOFF[v ? 1 : 0];
    if (s) {
        int k = 0;
        while (s[k] && k < 9) {
            b[k] = s[k];
            k++;
        }
        b[k] = 0;
        return;
    }
    if ((i == P_TRANSPOSE || i == P_OCTAVE || i == P_TUNE) && v > 0) {
        *b++ = '+';
    }
    itoa_s(v, b);
}

/* ------------------------------------------------------------- voicing --- */
static int fold(int pc, int lo) { return lo + (((pc - lo) % 12) + 12) % 12; }

void om_scale(int transpose, int octave, uint8_t harp[OM_NSTR])
{
    static const uint8_t WHITE[OM_NSTR] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};   /* over F3 */
    int i;
    for (i = 0; i < OM_NSTR; i++)
        harp[i] = (uint8_t)(53 + WHITE[i] + transpose + 12 * octave);
}

void om_linear(int root, int type, int transpose, int octave, uint8_t harp[OM_NSTR])
{
    const int8_t *t = OM_TYPE_TONES[(unsigned)type < CH_NTYPES ? type : 0];
    int base = fold(root + transpose, 36 + 12 * octave), lo = t[1] < t[2] ? t[1] : t[2], hi = t[1] < t[2] ? t[2] : t[1], k;
    for (k = 0; k < 5; k++) {                     /* (add9's third tone is its 9th, under its third) */
        harp[3 * k] = (uint8_t)(base + 12 * k);
        harp[3 * k + 1] = (uint8_t)(base + 12 * k + lo);
        harp[3 * k + 2] = (uint8_t)(base + 12 * k + hi);
    }
    harp[15] = (uint8_t)(base + 60);
}

void om_chord_scale(int root, int type, int transpose, int octave, int shift, uint8_t harp[OM_NSTR])
{
    const int8_t *sc = OM_TYPE_SCALE[(unsigned)type < CH_NTYPES ? type : 0];
    int base = fold(root + transpose, 48 + 12 * octave), i;
    for (i = 0; i < OM_NSTR; i++) {
        int k = i + shift + 7;                    /* shift >= -6: never below zero */
        harp[i] = (uint8_t)(base + 12 * (k / 7 - 1) + sc[k % 7]);
    }
}

void om_voicing(int root, int type, int transpose, int octave, uint8_t harp[OM_NSTR], uint8_t chord[OM_NCHORD],
                uint8_t *bass)
{
    const int8_t *t = OM_TYPE_TONES[(unsigned)type < CH_NTYPES ? type : 0];
    int r = root + transpose, k, i;
    for (k = 0; k < 5; k++)                       /* windows F#2..F3, F#3..F4, ...: root, 2nd, 3rd tone */
        for (i = 0; i < 3; i++)
            harp[3 * k + i] = (uint8_t)fold(r + t[i], 42 + 12 * k + 12 * octave);
    harp[15] = (uint8_t)fold(r, 42 + 60 + 12 * octave);
    for (i = 0; i < 3; i++)
        chord[i] = (uint8_t)fold(r + t[i], 54);  /* F#3..F4, the OM's chord register */
    *bass = (uint8_t)fold(r, 36);                 /* C2..B2 */
}

void om_voicing_inv(int root, int type, int inv, int transpose, int octave, uint8_t harp[OM_NSTR],
                    uint8_t chord[OM_NCHORD], uint8_t *bass)
{
    const int8_t *t = OM_TYPE_TONES[(unsigned)type < CH_NTYPES ? type : 0];
    int r = root + transpose, i;
    om_voicing(root, type, transpose, octave, harp, chord, bass);
    if (inv >= INV_ROOT && inv < INV_N) {
        int b = fold(r + t[inv - INV_ROOT], 50);  /* the bottom note in D3..C#4, the others above it */
        for (i = 0; i < 3; i++)
            chord[i] = (uint8_t)fold(r + t[i], b);
        *bass = (uint8_t)fold(r + t[inv - INV_ROOT], 36);
    }
}

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_CHORD, C_GATE, C_STRUM, C_PLAY, C_PANIC, C_CLOCK, C_PLATE, C_VOICE, C_CVOICE, C_SEQ, C_SEQTRK, C_BASS,
       C_MEL, C_MELREC };
#define QN 64u
static uint32_t q[QN];
static volatile uint32_t q_w, q_r;
static void post(uint32_t c, int a, int b)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;                                   /* full: dropped (never, at the UI's rate) */
    q[w % QN] = c << 24 | ((uint32_t)a & 0xFFu) << 16 | ((uint32_t)b & 0xFFFFu);
    BARRIER();
    q_w = w + 1;
}
void om_set(int p, int v) { post(C_SET, p, v); }
void om_chord(int root, int type) { post(C_CHORD, root, type); }
void om_chord_inv(int root, int type, int inv) { post(C_CHORD, root, type | inv << 8); }
void om_plate(int mode, int shift) { post(C_PLATE, mode, shift); }
void om_voice(int i) { post(C_VOICE, i, 0); }
void om_cvoice(int i) { post(C_CVOICE, i, 0); }
void om_seq(int on, int key) { post(C_SEQ, on, key); }
void om_seq_track(int t) { post(C_SEQTRK, t, 0); }
void om_bass(int level) { post(C_BASS, level, 0); }
void om_melody(int what, int value) { post(C_MEL, what, value); }
void om_mel_rec(int on, int t, int i, int layer) { post(C_MELREC, on ? 1 + (i & 31) : 0, (t & 7) | (layer != 0) << 8); }
void om_gate(int on) { post(C_GATE, on, 0); }
void om_strum(int s) { post(C_STRUM, s, 0); }
void om_play(int on) { post(C_PLAY, on, 0); }
void om_panic(void) { post(C_PANIC, 0, 0); }
void om_clock(int msg) { post(C_CLOCK, msg, 0); }

/* -------------------------------------------------------------- state --- */
volatile float om_str_level[OM_NSTR];
volatile uint8_t om_str_note[OM_NSTR];        /* the note a pluck plays now */
volatile float om_chord_level, om_bass_level;
volatile float om_mel_level;
volatile uint8_t om_mel_note;
volatile uint8_t om_playing, om_step, om_steps = 32;
volatile uint8_t om_drum_hit;
volatile uint8_t om_ext;
volatile uint8_t om_seq_trk, om_seq_at, om_seq_n;
const uint8_t (*volatile om_seq_tab)[OM_NSEQ][2];
const uint8_t (*volatile om_seq_mel)[OM_NSEQ];
const om_notes_t *volatile om_mel_notes;
volatile uint8_t om_mel_busy;
volatile uint8_t om_rec, om_rec_takes, om_rec_ack, om_rec_n, om_rec_trk, om_rec_step;
volatile uint8_t om_rec_pos, om_rec_len, om_rec_beat;

static int16_t par[P_NPARAMS];
static float tunefac = 1.0f, v1, v2, cvol, rvol, rsend, csend, sus_rate, bvol = 1.0f;
/* the strings, and after them the melody's (MEL): a string of its own, in the middle */
#define MEL OM_NSTR
#define NV (OM_NSTR + 1)
static float pan_l[NV], pan_r[NV];
static int root, type, inv;
static uint8_t harp_n[OM_NSTR], chord_n[OM_NCHORD], bass_n;   /* bass_n: the chord's bass note (its root, or as it lies) */
static uint8_t plate;                         /* what the strum plate plays: PL_* (om_plate) */
static int8_t plate_shift;                    /* the chord's scale, moved by steps (-6..6) */
static int gate;

static float sin3[360];                       /* Chordian's shimmer shape: |sin|^0.6 */
static float shim_ph, shim_rate;
/* the shimmer's rate follows the chord's root (Chordian, Hz, C..B) */
static const float SHIM_HZ[12] = {3.761f, 2.910f, 4.275f, 4.565f, 5.050f, 6.333f, 2.843f, 3.125f, 3.162f, 3.419f,
                                  3.667f, 3.953f};

static float note_inc(int n)                   /* wave positions (100 per cycle) per sample */
{
    return 440.0f * tunefac * fm_exp2f((float)(n - 69) * (1.0f / 12.0f)) * (100.0f / OM_SR);
}

static inline float wave(const float *w, float pos)
{
    int i = (int)pos;
    float f = pos - (float)i, a = w[i], b = w[i + 1 < 100 ? i + 1 : 0];
    return a + (b - a) * f;
}
static inline float adv(float pos, float inc)
{
    pos += inc;
    while (pos >= 100.0f)
        pos -= 100.0f;
    return pos;
}

/* ---- harp strings */
typedef struct {
    float pos, inc, v, g, y1, y2, k, c, m, modamt;
    uint16_t shim_off;
    uint8_t on, note;
    float kd;                                  /* as a DX7's voice: its decay */
    uint8_t rel;                               /* let go (the melody's note ends): it dies away quickly */
} str_t;
static str_t str[NV];
static float mel_gain;                         /* the melody's level */

/* ---- the voices. The strings: the OM's harp, or a patch of the DX7 bank (om_dx.h). The chord: the OM's
 * organ, or a patch. The bass and the drums are the OM's own */
static uint8_t voice;                          /* 0 the harp, 1.. the bank's patch voice - 1 (om_voice) */
static float fm_sin[1025];                     /* a cycle, and its first point again */
static uint8_t cvoice;                         /* the chord: 0 the organ, 1.. a patch (om_cvoice) */
const uint8_t (*volatile om_dx_tab)[OM_DX_BYTES];
static const char *dx_name(int i);
static int dx_has(int i);
const char *om_voice_name(int i) { return i >= OM_NSV ? dx_name(i - OM_NSV) : "OM harp"; }
const char *om_cvoice_name(int i) { return i >= OM_NCV ? dx_name(i - OM_NCV) : "Organ"; }
int om_voice_ok(int i) { return i >= 0 && (i < OM_NSV || dx_has(i - OM_NSV)); }
int om_cvoice_ok(int i) { return i >= 0 && (i < OM_NCV || dx_has(i - OM_NCV)); }

static inline float fm_osc(uint32_t ph)
{
    uint32_t i = ph >> 22;
    float a = fm_sin[i];
    return a + (fm_sin[i + 1] - a) * ((float)(ph & 0x3FFFFFu) * (1.0f / 4194304.0f));
}
#include "om_dx.h"
static dxv_t dxs[NV], dxc[OM_NCHORD];          /* the strings and the chord's notes as a DX7's voices */
static uint32_t str_age[NV], str_clock;        /* when each string was plucked */
static int dx_has(int i)
{
    const uint8_t (*t)[OM_DX_BYTES] = om_dx_tab;
    return t && i >= 0 && i < OM_NDX && dx_ok(t[i]);
}
static const char *dx_name(int i)              /* as the patch is named (until the next call) */
{
    static char b[11];
    const uint8_t (*t)[OM_DX_BYTES] = om_dx_tab;
    int k, n = 0;
    if (!dx_has(i))
        return "-";
    for (k = 0; k < 10; k++) {
        char c = (char)t[i][118 + k];
        b[k] = c < 32 || c > 126 || c == 92 ? ' ' : c;
        if (b[k] != ' ')
            n = k + 1;
    }
    b[n] = 0;
    return n ? b : "DX";
}
#define DXS (voice >= OM_NSV)                  /* the strings, the chord play a DX7's patch */
#define DXC (cvoice >= OM_NCV)

/* a string as a DX7's voice: the patch with its key held, under the string's own decay (Sustain: 99 % gone
 * after 0.6 s .. 12 s). No more than OM_DX_POLY at once (the melody's note among them): the string plucked
 * longest ago dies away for it */
static void dx_strum(str_t *v, int s, int n)
{
    int i, live = 0, old = -1;
    for (i = 0; i < NV; i++)
        if (i != s && str[i].on && str[i].v > 0.0f) {
            live++;
            if (i != MEL && (old < 0 || str_clock - str_age[i] > str_clock - str_age[old]))
                old = i;
        }
    if (live >= OM_DX_POLY && old >= 0)
        str[old].v = 0.0f;
    str_age[s] = ++str_clock;
    dx_on(&dxs[s], om_dx_tab[voice - OM_NSV], n, !v->on);
    v->kd = fm_exp2f(-1.442695f * 4.6f / (0.6f * fm_exp2f(0.0432f * (float)par[P_SUSTAIN]) * OM_SR));
}

static void midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (par[P_MIDI])
        om_midi_out(st, d1, d2);
}

/* string s plays note n (the melody's on MIDI channel 4, the strings' on 1) */
#define STR_CH(s) ((s) == MEL ? 3u : 0u)
static void pluck(int s, int n)
{
    str_t *v = &str[s];
    float u = fm_exp2f((float)(n - 69) * (1.0f / 12.0f)) * tunefac, base;
    if (v->on && v->note != n)
        midi(0x80 | STR_CH(s), v->note, 0);
    if (v->on)
        midi(0x80 | STR_CH(s), (uint32_t)n, 0);
    midi(0x90 | STR_CH(s), (uint32_t)n, 100);
    if (!v->on) {
        v->y1 = v->y2 = 0.0f;
        v->g = 0.0f;
    }
    if (DXS)
        dx_strum(v, s, n);
    v->on = 1;
    v->rel = 0;
    v->note = (uint8_t)n;
    v->inc = note_inc(n);
    v->v = 1.0f;
    /* the voice filter: Chordian's per-note brightness, darker as the note decays */
    base = fm_maxf(((18.0f - u) / 18.0f) * 0.31f + 0.34f, 0.05f);
    v->k = fm_powf(base, 2.85f);
    {   /* the shimmer: less on the higher strings; each string its own phase (Chordian's offsets) */
        int i = s * 12 / 15;
        v->modamt = 1.0f - (float)i / 17.0f;
        v->shim_off = (uint16_t)(((i & 1) ? 27 * i : 14 * i) % 360);
    }
}
static void strum(int s) { pluck(s, harp_n[s]); }

/* ---- chord and bass */
enum { E_OFF, E_DEC, E_SUS, E_REL, E_BDEC, E_CUT };
typedef struct {
    float pos, inc, v, g;
    uint8_t st, note;
} cv_t;
static cv_t chv[OM_NCHORD], bsv;
static uint8_t bass_kind;                      /* 0 root, 1 fifth, 2 third */

static int bass_note(void)
{
    const int8_t *t = OM_TYPE_TONES[type];
    if (inv > INV_ROOT)                           /* C/E: the rhythm's other bass notes are the root */
        return bass_kind ? bass_n - t[inv - INV_ROOT] + (t[inv - INV_ROOT] > 6 ? 12 : 0) : bass_n;
    return bass_n + (bass_kind == 1 ? 7 : bass_kind == 2 ? t[1] : 0);
}

static void chord_cmd(char c)
{
    int i;
    if (c == 'T' || c == 'O') {
        for (i = 0; i < OM_NCHORD; i++) {
            if (chv[i].st != E_OFF)
                midi(0x81, chv[i].note, 0);
            if (DXC) {
                dx_on(&dxc[i], om_dx_tab[cvoice - OM_NCV], chord_n[i], chv[i].st == E_OFF);
            }
            chv[i].note = chord_n[i];
            chv[i].inc = note_inc(chord_n[i]);
            chv[i].v = 1.0f;
            chv[i].st = c == 'T' ? E_DEC : E_REL;
            midi(0x91, chord_n[i], 90);
        }
    } else if (c == 'R' || c == 'N') {
        for (i = 0; i < OM_NCHORD; i++)
            if (chv[i].st != E_OFF)
                chv[i].st = c == 'R' ? E_REL : E_CUT;
    }
}

static void bass_cmd(char c, char which)
{
    if (c == 'T' || c == 'O') {
        bass_kind = which == '5' ? 1 : which == '3' ? 2 : 0;
        if (bsv.st != E_OFF)
            midi(0x82, bsv.note, 0);
        bsv.note = (uint8_t)bass_note();
        bsv.inc = note_inc(bsv.note);
        bsv.v = 1.0f;
        bsv.st = c == 'T' ? E_SUS : E_BDEC;
        midi(0x92, bsv.note, 100);
    } else if ((c == 'R' || c == 'N') && bsv.st != E_OFF) {
        bsv.st = c == 'R' ? E_REL : E_CUT;
    }
}

/* the chord changed: what sounds moves to it (the OM's chord and bass follow the buttons) */
static void retune(void)
{
    int i;
    om_voicing_inv(root, type, inv, par[P_TRANSPOSE], par[P_OCTAVE], harp_n, chord_n, &bass_n);
    if (plate == PL_FIXED)
        om_scale(par[P_TRANSPOSE], par[P_OCTAVE], harp_n);
    else if (plate == PL_LINEAR)
        om_linear(root, type, par[P_TRANSPOSE], par[P_OCTAVE], harp_n);
    else if (plate == PL_CHORD)
        om_chord_scale(root, type, par[P_TRANSPOSE], par[P_OCTAVE], plate_shift, harp_n);
    for (i = 0; i < OM_NSTR; i++)                 /* what each string plays when plucked next */
        om_str_note[i] = harp_n[i];
    for (i = 0; i < OM_NCHORD; i++)
        if (chv[i].st != E_OFF && chv[i].note != chord_n[i]) {
            midi(0x81, chv[i].note, 0);
            chv[i].note = chord_n[i];
            chv[i].inc = note_inc(chord_n[i]);
            if (DXC)
                dx_pitch(&dxc[i], om_dx_tab[cvoice - OM_NCV], chord_n[i]);
            midi(0x91, chord_n[i], 90);
        }
    if (bsv.st != E_OFF && bsv.note != bass_note()) {
        midi(0x82, bsv.note, 0);
        bsv.note = (uint8_t)bass_note();
        bsv.inc = note_inc(bsv.note);
        midi(0x92, bsv.note, 100);
    }
    shim_rate = SHIM_HZ[((root + par[P_TRANSPOSE]) % 12 + 12) % 12] * 360.0f / OM_SR;
}

/* ---- rhythm */
typedef struct {
    uint32_t pos;
    uint8_t on;
} drum_t;
static drum_t drum[OM_NDRUM];
static int rh, rh_next, step, pulse;            /* pulse: MIDI clock, 0..5 within a step */
static float pulse_left, step_len;
static uint32_t ext_age;                         /* render calls since the last clock tick */
static const uint8_t GM_DRUM[OM_NDRUM] = {36, 75, 42, 51, 38};   /* BD, claves, closed hat, ride, snare */

static void tempo_set(void) { step_len = OM_SR * 60.0f / ((float)par[P_TEMPO] * 4.0f); }

/* a bar of the rhythm: its beats (the rhythms are mostly two bars), up to 6 */
static int bar_beats(const om_rhythm_t *r)
{
    int b = r->beat < 1 ? 4 : r->beat > 8 ? 8 : r->beat, nb = r->len / b;
    if (nb < 2)
        nb = 4;
    while (nb > 4 && !(nb & 1))
        nb /= 2;
    return nb > 6 ? 4 : nb;
}

/* ---- the chord sequence: a track's chords in turn, each for its beats, while the rhythm runs */
static uint8_t seq_on, seq_key, seq_left;       /* seq_left: beats the chord has yet to play */
static uint8_t seq_trk, seq_next;               /* the track playing, and the one to follow it */
static int8_t seq_i = -1;

static int seq_beats(int t, int i)              /* (the table is the app's: whatever it holds, a step's beats) */
{
    const uint8_t (*tab)[OM_NSEQ][2] = om_seq_tab;
    int b = tab && i < OM_NSEQ ? tab[t][i][1] & 31 : 0;
    return b > OM_SEQ_BEATS ? OM_SEQ_BEATS : b;
}
static int seq_drives(void) { return seq_on && om_playing && seq_beats(seq_trk, 0); }

/* recording a step's melody (om_mel_rec): the step (rec_i of the track playing) round and round */
static uint8_t rec_i, rec_go, rec_layer, rec_play;   /* rec_go: the count-in from the next beat; rec_play: recording
                                                      * started the rhythm */
static uint16_t sq_pos;                         /* steps of the rhythm since the sequence's step began */

static void mel_seq_step(int i);
static void rec_pass(void);
static void rec_end(int stop);

static void seq_chord(int i)                    /* step i's chord sounds */
{
    int c = om_seq_tab[seq_trk][i][0];
    seq_i = (int8_t)i;
    root = ((c & 15) + seq_key) % 12;
    type = (c >> 4) < CH_NTYPES ? c >> 4 : 0;
    inv = om_seq_tab[seq_trk][i][1] >> 6;
    retune();
    gate = 1;
    if (!par[P_ABC]) {                           /* no auto bass: the chord is held, as from a button */
        chord_cmd('T');
        bass_kind = 0;
        bass_cmd('T', 'R');
    }
    om_seq_at = (uint8_t)i;
    om_seq_n++;
    sq_pos = 0;
}

static void seq_beat(void)
{
    if (om_rec && !seq_beats(seq_trk, rec_i))   /* (the step is gone) */
        rec_end(1);
    if (rec_go) {                                /* recording: a bar's count-in, on the step's chord */
        const om_rhythm_t *r = om_rhythm(rh);
        rec_go = 0;
        om_rec = 1;
        seq_chord(rec_i);
        mel_seq_step(rec_i);
        seq_left = (uint8_t)bar_beats(r);
        om_rec_len = (uint8_t)(seq_left * r->beat);
        om_rec_beat = r->beat;
    } else if (!seq_left) {
        int i = seq_i + 1;
        if (om_rec) {                            /* the step again: a time round ends, another begins */
            i = rec_i;
            rec_pass();
        } else if (!seq_beats(seq_trk, i)) {     /* round again, or on to the track that waits */
            i = 0;
            seq_trk = seq_next;
            om_seq_trk = seq_trk;
            if (!seq_beats(seq_trk, 0)) {        /* (an empty one: the sequence rests) */
                seq_i = -1;
                return;
            }
        }
        seq_chord(i);
        seq_left = (uint8_t)seq_beats(seq_trk, i);
        if (om_rec) {
            const om_rhythm_t *r = om_rhythm(rh);
            om_rec_len = (uint8_t)(seq_left * r->beat > 255 ? 255 : seq_left * r->beat);
            om_rec_beat = r->beat;
        }
        mel_seq_step(i);
    }
    seq_left--;
}

/* ---- the melody (om_melody): a tune made as it plays, a step at a time, by rules a songwriter keeps without
 * thinking. Its notes come from the chord's scale (OM_TYPE_SCALE) around D5: on the strong beats the chord's
 * tones, between them the scale's notes too, which then move on by a step; mostly steps, some thirds, now and
 * then a leap, which the next notes turn back from; drawn back towards the middle. Its rhythm a beat at a
 * time, from a few figures (a quarter, eighths, a dotted one...), busier with Density. Four bars make a
 * phrase: a figure, the same again (a little changed), the same, then a new bar that ends on the root or the
 * third and rests. The figure keeps its notes as steps of the scale from the chord's root, so over another
 * chord it moves with it. With Repeat the next phrase keeps the figure; the seed sets the dice (from PLAY) */
static uint8_t mel_lvl, mel_den = 50, mel_seed = 1, mel_rep = 70;
static uint32_t mel_rng = 1;
static int mel_pos, mel_bar = 3, mel_nb = 4, mel_b = 4;   /* the step in the bar, the bar in the phrase, its beats, a beat's steps */
static int mel_last, mel_last2;                 /* the last note and the one before (MIDI; 0: none yet) */
static int8_t mel_leap;                         /* its move, in steps of the scale */
static uint8_t mel_pass, mel_same;              /* it was not the chord's (the next moves by a step); notes the same */
static uint8_t mel_cell, mel_fresh;             /* this beat's notes (bits: its steps); the bar is made new */
static uint8_t mel_trk, mel_sd, mel_cad;        /* the sequence's track holds melodies (they play, and only they);
                                                 * the step's (its seed, 0: none); this beat ends it (1, 2: the
                                                 * last note, then held) */
#define MEL_MAX 48                              /* a bar: up to 6 beats of 8 steps */
static uint8_t mot_n;                           /* the figure: its bar's steps (0: none) */
static int8_t mot[MEL_MAX];                     /* each step: a note (a step of the scale up from the root, +64 */
                                                /* off: 0 nothing, 1 let go, else the note + 64 */
static uint32_t mel_rand(void)
{
    uint32_t x = mel_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return mel_rng = x;
}
static int mel_dice(int pct) { return (int)(mel_rand() % 100u) < pct; }

static int mel_mid(void) { return 74 + 12 * par[P_OCTAVE]; }
/* the root the scale's steps count from: the chord's, in the octave below the middle */
static int mel_root(int r) { return fold(r + par[P_TRANSPOSE], mel_mid() - 6); }
static int mel_note(int r, int t, int d)        /* a step of the chord's scale: its note */
{
    int o = d >= 0 ? d / 7 : -((6 - d) / 7);
    return mel_root(r) + 12 * o + OM_TYPE_SCALE[t][d - 7 * o];
}
static int chord_tone(int r, int t, int n)      /* a tone of the chord (and its fifth, where the scale has one) */
{
    int pc = ((n - r - par[P_TRANSPOSE]) % 12 + 12) % 12;
    return pc == OM_TYPE_TONES[t][0] || pc == OM_TYPE_TONES[t][1] || pc == OM_TYPE_TONES[t][2] ||
           (pc == 7 && OM_TYPE_SCALE[t][4] == 7);
}
static int mel_near(int n)                      /* the step of this chord's scale nearest a note (none: the middle) */
{
    int d, best = 0, bd = 99;
    if (!n)
        n = mel_mid();
    for (d = -10; d <= 17; d++) {
        int x = mel_note(root, type, d) - n;
        x = x < 0 ? -x : x;
        if (x < bd) {
            bd = x;
            best = d;
        }
    }
    return best;
}

/* the chord after this one, if the sequence plays its last beat now: a note may lead into it */
static int mel_next(int *r, int *t)
{
    int i, c;
    if (!seq_drives() || seq_left || seq_i < 0)
        return 0;
    i = seq_beats(seq_trk, seq_i + 1) ? seq_i + 1 : 0;
    c = om_seq_tab[seq_trk][i][0];
    *r = ((c & 15) + seq_key) % 12;
    *t = (c >> 4) < CH_NTYPES ? c >> 4 : 0;
    return 1;
}

/* the next note, by the rules. weight: 2 a strong beat, 1 a beat, 0 between; end: the phrase's last note */
static int mel_pick(int weight, int end)
{
    int pd = mel_near(mel_last), pd2 = mel_last2 ? mel_near(mel_last2) : 99, mid = mel_mid(), d, tot = 0, nr = 0, nt = 0;
    int w[28], nx = mel_next(&nr, &nt) && weight == 0;
    for (d = -10; d <= 17; d++) {
        int n = mel_note(root, type, d), a = d - pd < 0 ? pd - d : d - pd, ct = chord_tone(root, type, n), x;
        x = a == 0 ? 3 : a == 1 ? 16 : a == 2 ? 7 : a == 3 ? 3 : a == 4 ? 2 : a <= 7 ? 1 : 0;
        if (mel_pass && a != 1)                   /* a note off the chord moves on by a step */
            x = 0;
        if (a == 0 && mel_same)                   /* not three the same */
            x = 0;
        if (a == 1 && d == pd2)                   /* nor a trill */
            x /= 3;
        if (weight == 2)
            x *= ct ? 8 : 0;
        else if (weight == 1)
            x *= ct ? 3 : 1;
        else
            x *= 2;
        if (mel_leap >= 3 || mel_leap <= -3) {    /* after a leap: back the other way */
            if ((d - pd) * mel_leap > 0)
                x /= 4;
            else if (d != pd)
                x *= 2;
        }
        if ((n > mid + 4 && d > pd) || (n < mid - 4 && d < pd))   /* drawn towards the middle */
            x /= 2;
        if (end) {                                /* the phrase ends on the root, or the third */
            int pc = ((n - root - par[P_TRANSPOSE]) % 12 + 12) % 12;
            x = a > 4 ? 0 : pc == 0 ? 4 * (x + 1) : pc == OM_TYPE_TONES[type][1] ? 2 * (x + 1) : 0;
        }
        if (nx && chord_tone(nr, nt, n))          /* the chord to come: lead into it */
            x *= 3;
        if (n < mid - 9 || n > mid + 10)          /* its register */
            x = 0;
        w[d + 10] = x;
        tot += x;
    }
    if (!tot) {                                   /* nothing fits: the chord's tone nearest */
        int best = pd, bd = 99;
        for (d = -10; d <= 17; d++) {
            int n = mel_note(root, type, d), a = d - pd < 0 ? pd - d : d - pd;
            if (chord_tone(root, type, n) && n >= mid - 9 && n <= mid + 10 && a < bd) {
                bd = a;
                best = d;
            }
        }
        return best;
    }
    tot = (int)(mel_rand() % (uint32_t)tot);
    for (d = -10; d < 17 && tot >= w[d + 10]; d++)
        tot -= w[d + 10];
    return d;
}

/* a beat's notes: the steps of it that start one (bits); 0 a rest; 0x80 the note before holds on */
static uint8_t mel_rhythm(int b)
{
    static const uint8_t C4[] = {0, 0x80, 0x01, 0x05, 0x09, 0x0D, 0x0F, 0x04}, C6[] = {0, 0x80, 0x01, 0x15, 0x11, 0x05};
    int d = mel_den, w[8], n, i, tot = 0;
    const uint8_t *c = b == 4 ? C4 : C6;
    w[0] = (100 - d) / 5 + 3;                     /* rest, hold, a quarter, eighths, ... */
    w[1] = (100 - d) / 3 + 2;
    w[2] = 30;
    w[3] = 10 + d / 2;
    if (b == 4) {
        w[4] = 6 + d / 8;                         /* dotted, an eighth and two sixteenths, sixteenths, off the beat */
        w[5] = d / 6;
        w[6] = d * d / 600;
        w[7] = 4;
        n = 8;
    } else if (b == 6) {                          /* 12/8: three eighths, a quarter and an eighth */
        w[4] = 8;
        n = 5;
    } else {
        n = 4;
    }
    for (i = 0; i < n; i++)
        tot += w[i];
    tot = (int)(mel_rand() % (uint32_t)tot);
    for (i = 0; i < n - 1 && tot >= w[i]; i++)
        tot -= w[i];
    if (b != 4 && b != 6 && i == 3)               /* other beats: halves */
        return (uint8_t)(1u | 1u << (b / 2));
    if (b == 6 && i == 3)
        return 0x15;
    return c[i];
}

static void mel_play(int d)                     /* the note: the melody's string plucked */
{
    int n = mel_note(root, type, d), pd = mel_near(mel_last);
    mel_leap = (int8_t)(mel_last ? d - pd : 0);
    mel_same = (uint8_t)(mel_last == n);
    mel_pass = (uint8_t)!chord_tone(root, type, n);
    mel_last2 = mel_last;
    mel_last = n;
    pluck(MEL, n);
    om_mel_note = (uint8_t)n;
}
static void mel_let_go(void)
{
    if (str[MEL].on)
        str[MEL].rel = 1;
}

static void mel_start(void)                     /* PLAY: from the seed, a new phrase */
{
    mel_rng = 0x9E3779B9u * (mel_seed + 1u);
    mel_trk = 0;
    mel_pos = 0;
    mel_bar = 3;
    mel_last = mel_last2 = 0;
    mel_pass = mel_same = 0;
    mel_leap = 0;
    mot_n = 0;
}

/* the step's own notes (om_mel_notes), as the render keeps them while the step plays; and, recording, the time
 * round's (rec_ev), in order of their places */
static uint8_t mel_ev[OM_MEL_STEP][2], rec_ev[OM_MEL_STEP][2];
const uint8_t (*const om_rec_ev)[2] = (const uint8_t (*)[2])mel_ev;
static uint8_t mel_evn, mel_ei, mel_due;        /* how many, the next to play; to be read (the app was changing them) */
static uint8_t rec_n, rec_mute, rec_early;      /* replacing: the old notes make way; a note played just before the
                                                 * time round, for its start (+1) */

static void mel_load(void)                      /* the playing step's notes, from the app's table */
{
    const om_notes_t *t = om_mel_notes;
    int k, off = 0, n;
    mel_ei = 0;
    mel_due = 0;
    if (om_rec == 2 || (om_rec_ack != om_rec_takes && seq_trk == om_rec_trk && seq_i == om_rec_step))
        return;                                   /* recording, or a take the app has yet to keep: its own */
    mel_evn = 0;
    if (!t || seq_i < 0)
        return;
    if (om_mel_busy) {                            /* (at the next step of the rhythm) */
        mel_due = 1;
        return;
    }
    for (k = 0; k < seq_trk * OM_NSEQ + seq_i; k++)
        off += t->n[k / OM_NSEQ][k % OM_NSEQ];
    n = t->n[seq_trk][seq_i];
    if (n > OM_MEL_STEP || off + n > OM_MEL_NEV)
        return;
    for (k = 0; k < n; k++) {
        mel_ev[k][0] = t->ev[off + k][0];
        mel_ev[k][1] = t->ev[off + k][1];
    }
    mel_evn = (uint8_t)n;
}

/* a step of the sequence begins (i): if its track holds melodies, this step's: its own notes, or one made anew
 * from its seed over its chord, a phrase of its own (so it is the same each time round, wherever the track was
 * before it) */
static void mel_seq_step(int i)
{
    const uint8_t (*m)[OM_NSEQ] = om_seq_mel;
    const om_notes_t *t = om_mel_notes;
    int k;
    mel_trk = om_rec != 0;
    for (k = 0; k < OM_NSEQ && seq_beats(seq_trk, k); k++)
        mel_trk |= (m && m[seq_trk][k]) || (t && t->n[seq_trk][k]);
    mel_sd = m && !om_rec ? m[seq_trk][i] : 0;
    mel_load();
    if (!mel_trk)
        return;
    mel_rng = 0x6C8E9CF5u * (mel_sd + 1u) ^ 0x2545F491u;
    mel_pos = 0;
    mel_bar = 3;
    mel_last = mel_last2 = 0;
    mel_pass = mel_same = 0;
    mel_leap = 0;
    mot_n = 0;
}

/* a step's own notes, as they fall (not in the count-in; replacing, not once a new note is played) */
static void mel_notes(void)
{
    if (mel_due && !om_mel_busy)
        mel_load();
    while (mel_ei < mel_evn && mel_ev[mel_ei][0] < sq_pos)
        mel_ei++;
    if (om_rec == 1 || mel_ei >= mel_evn || mel_ev[mel_ei][0] != sq_pos)
        return;
    if (mel_lvl && !rec_mute) {
        int n = mel_ev[mel_ei][1] + seq_key + par[P_TRANSPOSE];
        n = n < 0 ? 0 : n > 127 ? 127 : n;
        pluck(MEL, n);
        om_mel_note = (uint8_t)n;
    }
    mel_ei++;
}

/* recording: a note at place p of the time round (one a place: the last played) */
static void rec_add(int p, int v)
{
    int j, k;
    if (p < 0 || p >= OM_MEL_STEP)
        return;
    for (j = 0; j < rec_n && rec_ev[j][0] < p; j++)
        ;
    if (j >= rec_n || rec_ev[j][0] != p) {
        if (rec_n >= OM_MEL_STEP)
            return;
        for (k = rec_n; k > j; k--) {
            rec_ev[k][0] = rec_ev[k - 1][0];
            rec_ev[k][1] = rec_ev[k - 1][1];
        }
        rec_n++;
        rec_ev[j][0] = (uint8_t)p;
    }
    rec_ev[j][1] = (uint8_t)v;
    if (!rec_layer && !rec_mute) {               /* replacing: the old melody makes way */
        rec_mute = 1;
        mel_let_go();
    }
}

/* a string plucked while recording (its note n): to the nearest step of the rhythm. One in the second half of
 * the last step before the time round (or the count-in's last) is the time round's first */
static void rec_strum(int n)
{
    const om_rhythm_t *r = om_rhythm(rh);
    float left = om_ext ? 0.5f : pulse_left / (step_len * (1.0f / 6.0f)),
          f = ((float)(pulse ? pulse : 6) - fm_clampf(left, 0.0f, 1.0f)) * (1.0f / 6.0f);
    int v = n - seq_key - par[P_TRANSPOSE], up = f >= 0.5f;
    v = v < 0 ? 0 : v > 127 ? 127 : v;
    if (!om_playing || !om_rec)
        return;
    if (up && !seq_left && r->beat && step % r->beat == 0) {
        rec_early = (uint8_t)(v + 1);
        return;
    }
    if (om_rec == 2)
        rec_add(sq_pos - 1 + up, v);
}

/* a take: the time round's notes become the step's (layering: are added to them, a new note in an old one's
 * place for it), and the app hears of it */
static void rec_take(void)
{
    static uint8_t out[OM_MEL_STEP][2];
    int a = 0, b = 0, n = 0;
    while (b < rec_n || (rec_layer && a < mel_evn)) {
        if (b >= rec_n || (a < mel_evn && rec_layer && mel_ev[a][0] < rec_ev[b][0])) {
            out[n][0] = mel_ev[a][0];
            out[n++][1] = mel_ev[a++][1];
        } else {
            while (a < mel_evn && mel_ev[a][0] <= rec_ev[b][0])
                a++;
            out[n][0] = rec_ev[b][0];
            out[n++][1] = rec_ev[b++][1];
        }
    }
    for (a = 0; a < n; a++) {
        mel_ev[a][0] = out[a][0];
        mel_ev[a][1] = out[a][1];
    }
    mel_evn = (uint8_t)n;
    mel_ei = 0;
    om_rec_n = (uint8_t)n;
    om_rec_trk = seq_trk;
    om_rec_step = rec_i;
    BARRIER();
    om_rec_takes++;
}

static void rec_pass(void)                      /* a time round begins (the last one's notes: a take) */
{
    if (om_rec == 2 && rec_n)
        rec_take();
    om_rec = 2;
    rec_n = 0;
    rec_mute = 0;
    if (rec_early) {
        rec_add(0, rec_early - 1);
        rec_early = 0;
    }
}

static void play_stop(void);
static void rec_end(int stop)                   /* stop: and the rhythm, if recording started it */
{
    if (!om_rec)
        return;
    if (om_rec == 2 && rec_n)
        rec_take();
    om_rec = 0;
    rec_go = 0;
    rec_n = 0;
    rec_mute = 0;
    rec_early = 0;
    if (stop && rec_play && om_playing)
        play_stop();
    rec_play = 0;
}

/* a step of the rhythm (r): the melody's, as it falls in its bar */
static void mel_step(const om_rhythm_t *r)
{
    int k, sb, beat, weight, sq = mel_trk && seq_drives(), live = mel_lvl && (sq ? mel_sd : gate || seq_drives());
    if (sq && (mel_evn || mel_due || om_rec)) {   /* the step's own notes */
        mel_notes();
        return;
    }
    if (mel_pos == 0) {                           /* a bar: its beats as the rhythm has them, up to 6 */
        mel_b = r->beat < 1 ? 4 : r->beat > 8 ? 8 : r->beat;
        mel_nb = bar_beats(r);
        mel_bar = (mel_bar + 1) & 3;
        if (mel_bar == 0 && (!mot_n || !mel_dice(mel_rep)))
            mot_n = 0;                            /* a new figure (Repeat: the odds of keeping it) */
        if (mot_n != mel_nb * mel_b)              /* (another rhythm, another bar: anew) */
            mot_n = 0;
        mel_fresh = mel_bar == 3 || !mot_n;
        if (mel_bar == 0)
            mel_let_go();                         /* a breath between phrases */
    }
    beat = mel_pos / mel_b;
    sb = mel_pos % mel_b;
    weight = sb ? 0 : beat == 0 || (mel_nb == 4 && beat == 2) ? 2 : 1;
    if (!sb)                                      /* a step's melody of four beats or more ends on its root or third */
        mel_cad = (uint8_t)(sq && seq_beats(seq_trk, seq_i) >= 4 && seq_left <= 1 ? 2 - seq_left : 0);
    if (mel_fresh || mel_cad) {
        int end = mel_cad ? mel_cad == 1 : mel_bar == 3 && beat == mel_nb - 2;
        if (!sb)
            mel_cell = mel_cad ? (mel_cad == 1 ? 0x01 : 0x80) : mel_bar == 3 && beat == mel_nb - 2 ? 0x01
                     : mel_bar == 3 && beat == mel_nb - 1 ? 0x80 : mel_rhythm(mel_b);
        if (sb == 0 && !(mel_cell & 0x81))
            k = 1;                                /* a rest */
        else if (mel_cell >> sb & 1u && !(mel_cell & 0x80))
            k = 64 + mel_pick(weight, end);
        else
            k = 0;
        if (mel_bar == 0 && mel_fresh)
            mot[mel_pos] = (int8_t)k;
    } else {                                      /* the figure again, over this chord */
        k = mot[mel_pos];
        if (k >= 32) {
            int d = k - 64, vary = (100 - mel_rep) / 2 + (mel_bar == 1 ? 10 : 0);
            if (mel_dice(vary)) {
                d = mel_pick(weight, 0);
            } else {                              /* its step of the scale, in the octave nearest the last note */
                int p = mel_near(mel_last), mid = mel_mid();
                while (d - p > 3)
                    d -= 7;
                while (p - d > 3)
                    d += 7;
                while (mel_note(root, type, d) > mid + 10)
                    d -= 7;
                while (mel_note(root, type, d) < mid - 9)
                    d += 7;
                if (weight == 2 && !chord_tone(root, type, mel_note(root, type, d)))
                    d += chord_tone(root, type, mel_note(root, type, d - 1)) ? -1 : 1;
            }
            k = 64 + d;
        }
    }
    if (live && k == 1)
        mel_let_go();
    else if (live && k >= 32)
        mel_play(k - 64);
    else if (!live)
        mel_let_go();
    if (++mel_pos >= mel_nb * mel_b) {
        if (mel_bar == 0)
            mot_n = (uint8_t)(mel_nb * mel_b);
        mel_pos = 0;
    }
}

static void do_step(void)
{
    const om_rhythm_t *r = om_rhythm(rh);
    uint8_t d;
    if (step >= r->len)                          /* (one of the player's, made shorter as it plays) */
        step = 0;
    d = r->drum[step];
    int i;
    if (seq_drives() && step % r->beat == 0)
        seq_beat();
    for (i = 0; i < OM_NDRUM; i++)
        if (d >> i & 1u) {
            drum[i].pos = 0;
            drum[i].on = 1;
            midi(0x99, GM_DRUM[i], 100);
            midi(0x89, GM_DRUM[i], 0);
        }
    om_drum_hit |= d;
    if (gate && par[P_ABC]) {
        if (r->bass[step] != '-')
            bass_cmd(r->bass[step], r->note[step]);
        if (r->chord[step] != '-')
            chord_cmd(r->chord[step]);
    }
    mel_step(r);
    sq_pos++;
    om_rec_pos = (uint8_t)(sq_pos > 255 ? 255 : sq_pos);
    om_step = (uint8_t)step;
    if (++step >= r->len) {
        step = 0;
        if (rh_next != rh) {                     /* a new rhythm starts on the bar */
            rh = rh_next;
            om_steps = om_rhythm(rh)->len;
        }
    }
}

/* ---- reverb: Dattorro's plate (J. Dattorro, "Effect Design, Part 1", JAES 1997): a predelay, four
 * input diffusers, then a figure-eight tank of two halves, each a modulated allpass, a delay, a damping
 * lowpass, an allpass and a delay, feeding the other half. Stereo out from seven taps a side. The
 * paper's lengths are for 29761 Hz; DS() scales them to 44.1 kHz. */
#define DS(n) (((n) * 14818 + 5000) / 10000)          /* 44100 / 29761 */
#define PRE_N 442                                    /* 10 ms */
#define EXC DS(16)                                   /* the tank allpasses' modulation, samples */
enum { L_PRE, L_IN1, L_IN2, L_IN3, L_IN4, L_APL, L_D1L, L_AP2L, L_D2L, L_APR, L_D1R, L_AP2R, L_D2R, L_N };
static const int DL_LEN[L_N] = {PRE_N, DS(142), DS(107), DS(379), DS(277), DS(672) + EXC + 2, DS(4453), DS(1800),
                                DS(3720), DS(908) + EXC + 2, DS(4217), DS(2656), DS(3163)};
#define DL_TOTAL (PRE_N + DS(142) + DS(107) + DS(379) + DS(277) + DS(672) + DS(4453) + DS(1800) + DS(3720) + DS(908) + \
                  DS(4217) + DS(2656) + DS(3163) + 2 * (EXC + 2))
static float pl_buf[DL_TOTAL] OM_POOL;
typedef struct {
    float *b;
    int n, i;
} dl_t;
static dl_t dl[L_N];
static float pl_bw, pl_damp_l, pl_damp_r, pl_decay = 0.5f, pl_dd2 = 0.5f, lfo_s, lfo_c = 1.0f;

static inline float tap(const dl_t *d, int k)          /* the value written k samples ago, 1..n */
{
    int j = d->i - k;
    return d->b[j < 0 ? j + d->n : j];
}
static inline void push(dl_t *d, float x)
{
    d->b[d->i] = x;
    if (++d->i >= d->n)
        d->i = 0;
}
static inline float allpass(dl_t *d, int len, float x, float g)
{
    float z = tap(d, len), v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}
static inline float allpass_mod(dl_t *d, float len, float x, float g)   /* a fractional, moving length */
{
    int k = (int)len;
    float f = len - (float)k, z = tap(d, k) + (tap(d, k + 1) - tap(d, k)) * f, v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}

static inline void reverb(float in, float *ol, float *or_)
{
    float x, a, b, fb_l = tap(&dl[L_D2R], DS(3163)), fb_r = tap(&dl[L_D2L], DS(3720));
    push(&dl[L_PRE], in);
    x = tap(&dl[L_PRE], PRE_N);
    pl_bw += 0.7f * (x - pl_bw);                     /* input bandwidth */
    x = allpass(&dl[L_IN1], DS(142), pl_bw, 0.75f);
    x = allpass(&dl[L_IN2], DS(107), x, 0.75f);
    x = allpass(&dl[L_IN3], DS(379), x, 0.625f);
    x = allpass(&dl[L_IN4], DS(277), x, 0.625f);
    {   /* the modulation: ~0.9 Hz, sine and cosine for the two halves */
        float s = lfo_s + 0.000128f * lfo_c, c = lfo_c - 0.000128f * s;
        lfo_s = s;
        lfo_c = c;
    }
    a = allpass_mod(&dl[L_APL], (float)DS(672) + (float)EXC * lfo_s, x + pl_decay * fb_l, -0.7f);
    push(&dl[L_D1L], a);
    b = tap(&dl[L_D1L], DS(4453));
    pl_damp_l += 0.62f * (b - pl_damp_l);
    b = allpass(&dl[L_AP2L], DS(1800), pl_damp_l * pl_decay, pl_dd2);
    push(&dl[L_D2L], b);
    a = allpass_mod(&dl[L_APR], (float)DS(908) + (float)EXC * lfo_c, x + pl_decay * fb_r, -0.7f);
    push(&dl[L_D1R], a);
    b = tap(&dl[L_D1R], DS(4217));
    pl_damp_r += 0.62f * (b - pl_damp_r);
    b = allpass(&dl[L_AP2R], DS(2656), pl_damp_r * pl_decay, pl_dd2);
    push(&dl[L_D2R], b);
    *ol = 0.6f * (tap(&dl[L_D1R], DS(266)) + tap(&dl[L_D1R], DS(2974)) - tap(&dl[L_AP2R], DS(1913)) +
                  tap(&dl[L_D2R], DS(1996)) - tap(&dl[L_D1L], DS(1990)) - tap(&dl[L_AP2L], DS(187)) -
                  tap(&dl[L_D2L], DS(1066)));
    *or_ = 0.6f * (tap(&dl[L_D1L], DS(353)) + tap(&dl[L_D1L], DS(3627)) - tap(&dl[L_AP2L], DS(1228)) +
                   tap(&dl[L_D2L], DS(2673)) - tap(&dl[L_D1R], DS(2111)) - tap(&dl[L_AP2R], DS(335)) -
                   tap(&dl[L_D2R], DS(121)));
}

static void plate_init(void)
{
    float *p = pl_buf;
    int k, i;
    for (k = 0; k < L_N; k++) {
        dl[k].b = p;
        dl[k].n = DL_LEN[k];
        dl[k].i = 0;
        p += DL_LEN[k];
    }
    for (i = 0; i < DL_TOTAL; i++)
        pl_buf[i] = 0.0f;
    pl_bw = pl_damp_l = pl_damp_r = 0.0f;
    lfo_s = 0.0f;
    lfo_c = 1.0f;
}

/* ---- parameters */
static void apply(int p, int v)
{
    const om_param_t *pi = &PARAMS[p];
    int i;
    if (v < pi->lo)
        v = pi->lo;
    if (v > pi->hi)
        v = pi->hi;
    par[p] = (int16_t)v;
    switch (p) {
    case P_VOICE1: v1 = (float)v * 0.01f; break;
    case P_VOICE2: v2 = (float)v * 0.01f; break;
    case P_SUSTAIN: sus_rate = (1.0f / OM_SR) / (0.366f + 2.734f * (float)v * 0.01f); break;
    case P_CHORD: cvol = (float)v * 0.01f; break;
    case P_RHYVOL: rvol = (float)v * 0.01f; break;
    case P_REVERB: rsend = (float)v * 0.01f * (float)v * 0.01f; break;
    case P_CREV: csend = (float)v * 0.01f * (float)v * 0.01f; break;
    case P_SPACE:                                 /* the plate's decay: a small room to a long hall */
        pl_decay = 0.35f + 0.55f * (float)v * 0.01f;
        pl_dd2 = fm_clampf(pl_decay + 0.15f, 0.25f, 0.5f);
        break;
    case P_TEMPO: tempo_set(); break;
    case P_RHYTHM:
        rh_next = v;
        if (!om_playing) {
            rh = v;
            om_steps = om_rhythm(rh)->len;
        }
        break;
    case P_TUNE:
        tunefac = fm_exp2f((float)v * (1.0f / 1200.0f));
        retune();
        break;
    case P_TRANSPOSE: case P_OCTAVE: retune(); break;
    case P_WIDTH:
        for (i = 0; i < OM_NSTR; i++) {           /* low strings left, high right */
            float p0 = ((float)i / 15.0f * 2.0f - 1.0f) * (float)v * 0.01f, a = (p0 + 1.0f) * (FM_PI / 4.0f);
            pan_l[i] = fm_cosf(a);
            pan_r[i] = fm_sinf(a);
        }
        break;
    case P_MIDI:
        if (!v)
            om_midi_out(0xB0, 123, 0);            /* all notes off, as it goes quiet */
        break;
    default: break;
    }
}

static void quiet(void)
{
    int i;
    for (i = 0; i < NV; i++)
        str[i].on = 0;
    for (i = 0; i < OM_NCHORD; i++)
        chv[i].st = E_OFF;
    bsv.st = E_OFF;
    for (i = 0; i < OM_NDRUM; i++)
        drum[i].on = 0;
    for (i = 0; i < 4; i++)
        om_midi_out(0xB0 | (uint32_t)i, 123, 0);
}

void om_init(void)
{
    int i;
    for (i = 0; i < 360; i++) {
        float s = fm_sinf((float)(i + 125) * (FM_PI / 180.0f));
        sin3[i] = s == 0.0f ? 0.0f : (s > 0.0f ? 1.0f : -1.0f) * fm_powf(fm_fabsf(s), 0.6f);
    }
    for (i = 0; i <= 1024; i++)
        fm_sin[i] = fm_sinf((float)(i & 1023) * (FM_PI / 512.0f));
    plate_init();
    pan_l[MEL] = pan_r[MEL] = 0.7071f;
    for (i = 0; i < OM_NURHY; i++)
        if (!urh[i].name)
            om_rhythm_user(i, 0);
    q_r = q_w = 0;
    root = 0;
    type = CH_MAJ;
    for (i = 0; i < P_NPARAMS; i++)
        apply(i, PARAMS[i].def);
    retune();
}

/* the rhythm's clock: a step every 6 pulses. From its own tempo (pulse_left) or from MIDI clock in */
static void clock_pulse(void)
{
    if (pulse == 0)
        do_step();
    if (!om_ext)
        midi(0xF8, 0, 0);
    if (++pulse >= 6)
        pulse = 0;
}

static void play_start(int from_top)
{
    if (from_top) {
        rh = rh_next;
        om_steps = om_rhythm(rh)->len;
        step = 0;
        pulse = 0;
        seq_i = -1;
        seq_left = 0;
        mel_start();
    }
    pulse_left = 0.0f;
    om_playing = 1;
    if (!om_ext)
        midi(from_top ? 0xFA : 0xFB, 0, 0);
}

static void play_stop(void)
{
    rec_end(0);
    om_playing = 0;
    mel_let_go();
    if (!om_ext)
        midi(0xFC, 0, 0);
    if (!gate) {
        chord_cmd('R');
        bass_cmd('R', 'R');
    }
}

static void drain(void)
{
    while (q_r != q_w) {
        uint32_t c = q[q_r % QN];
        int a = (int)(c >> 16 & 0xFFu), b = (int)(int16_t)(c & 0xFFFFu);
        BARRIER();
        q_r++;
        switch (c >> 24) {
        case C_SET:
            if (a < P_NPARAMS)
                apply(a, b);
            break;
        case C_CHORD:
            root = a % 12;
            type = (b & 255) < CH_NTYPES ? b & 255 : 0;
            inv = (b >> 8 & 3);
            retune();
            break;
        case C_VOICE:                             /* the ringing strings stop: they are the other voice's */
            for (b = 0; b < NV; b++)
                if (str[b].on) {
                    str[b].on = 0;
                    midi(0x80 | STR_CH(b), str[b].note, 0);
                }
            voice = (uint8_t)(a < OM_NVOICES && om_voice_ok(a) ? a : 0);
            break;
        case C_CVOICE:                            /* the chord that sounds stops: it is the other voice's */
            for (b = 0; b < OM_NCHORD; b++)
                if (chv[b].st != E_OFF) {
                    chv[b].st = E_OFF;
                    midi(0x81, chv[b].note, 0);
                }
            cvoice = (uint8_t)(a < OM_NCVOICES && om_cvoice_ok(a) ? a : 0);
            if (gate && !(om_playing && par[P_ABC]))
                chord_cmd('T');             /* a chord held by hand goes on in the new voice */
            break;
        case C_PLATE:
            plate = (uint8_t)(a < PL_NMODES ? a : 0);
            plate_shift = (int8_t)(b < -6 ? -6 : b > 6 ? 6 : b);
            retune();
            break;
        case C_BASS:
            bvol = (float)(a > 100 ? 100 : a) * 0.02f;
            break;
        case C_MEL:
            b = b < 0 ? 0 : b > 100 ? 100 : b;
            if (a == OM_MEL_LEVEL) {
                mel_lvl = (uint8_t)b;
                mel_gain = (float)b * 0.01f;
            } else if (a == OM_MEL_DENSITY) {
                mel_den = (uint8_t)b;
            } else if (a == OM_MEL_SEED) {            /* another seed: another tune, from its next phrase */
                mel_seed = (uint8_t)b;
                mel_rng = 0x9E3779B9u * (mel_seed + 1u);
                mot_n = 0;
            } else if (a == OM_MEL_REPEAT) {
                mel_rep = (uint8_t)b;
            }
            break;
        case C_MELREC:                            /* a, the step + 1 (0: stop); b, the track | layer << 8 */
            if (!a) {
                rec_end(1);
                break;
            }
            rec_end(0);
            if (!om_playing) {
                play_start(1);
                rec_play = 1;
            }
            seq_trk = seq_next = (uint8_t)(b & 7);
            om_seq_trk = seq_trk;
            rec_i = (uint8_t)(a - 1);
            rec_layer = (uint8_t)(b >> 8 & 1);
            rec_go = 1;
            om_rec = 1;                           /* (the count-in from the next beat) */
            om_rec_pos = om_rec_len = 0;
            mel_trk = 1;
            mel_evn = 0;
            mel_let_go();
            break;
        case C_SEQTRK:                            /* when the one playing comes round; none does: now */
            seq_next = (uint8_t)(a < OM_NTRK ? a : 0);
            if (!seq_drives()) {
                seq_trk = seq_next;
                om_seq_trk = seq_trk;
                seq_i = -1;
                seq_left = 0;
            }
            break;
        case C_SEQ:
            if (!seq_on && a) {                   /* switched on: from its first chord, on the next beat */
                seq_i = -1;
                seq_left = 0;
            }
            seq_on = (uint8_t)(a != 0);
            seq_key = (uint8_t)((b % 12 + 12) % 12);
            break;
        case C_GATE:
            if (!a && seq_drives())               /* the sequence holds the chord: a button let go changes nothing */
                break;
            gate = a;
            if (!(om_playing && par[P_ABC])) {    /* by hand: the chord sounds while the pad is down */
                chord_cmd(a ? 'T' : 'R');
                bass_kind = 0;
                bass_cmd(a ? 'T' : 'R', 'R');
            } else if (!a) {
                chord_cmd('R');
                bass_cmd('R', 'R');
            }
            break;
        case C_STRUM:
            if (a < OM_NSTR) {
                strum(a);
                rec_strum(harp_n[a]);
            }
            break;
        case C_PLAY:
            if (a && !om_playing)
                play_start(1);
            else if (!a && om_playing)
                play_stop();
            break;
        case C_CLOCK:
            om_ext = 1;                             /* a clock message: someone else leads */
            ext_age = 0;
            if (a == OM_CLK_TICK) {
                if (om_playing)
                    clock_pulse();
            } else if (a == OM_CLK_START) {
                play_start(1);
            } else if (a == OM_CLK_CONTINUE) {
                if (!om_playing)
                    play_start(0);
            } else if (om_playing) {
                play_stop();
            }
            break;
        case C_PANIC:
            quiet();
            break;
        }
    }
}

/* ------------------------------------------------------------- render --- */
#define BLK 256
static float mixl[BLK], mixr[BLK], send[BLK];

/* a DX7's voice for the string or chord note being rendered, before its envelope */
static float dxbuf[BLK];

#define REL_K 0.99962f                          /* let go: gone in ~0.1 s */
static void str_level(int s, float e)
{
    if (s < OM_NSTR)
        om_str_level[s] = e;
    else
        om_mel_level = e;
}

static void render_dx(str_t *v, int s, uint32_t n)
{
    const float lvl = 0.4f * v1 * (s == MEL ? mel_gain : 1.0f), gl = pan_l[s], gr = pan_r[s],
                kd = v->rel ? v->kd * REL_K : v->kd;
    float e = v->v, g = v->g;
    uint32_t j;
    dx_render(&dxs[s], dxbuf, n);
    for (j = 0; j < n; j++) {
        float o;
        g += 0.05f * (e - g);
        o = dxbuf[j] * g * lvl;
        mixl[j] += o * gl;
        mixr[j] += o * gr;
        send[j] += o * rsend;
        e *= kd;
        if (e < 1e-4f) {
            e = 0.0f;
            if (g < 1e-4f) {
                v->on = 0;
                midi(0x80 | STR_CH(s), v->note, 0);
                break;
            }
        }
    }
    v->v = e;
    v->g = g;
    str_level(s, v->on ? e : 0.0f);
}

static void render_strings(uint32_t n)
{
    const float g1 = v1 * (1.0f - 0.5f * v2) * 1.2f,
                g2 = v2 * (1.0f - 0.5f * v1) * 0.85f * 1.2f;
    int s;
    for (s = 0; s < NV; s++) {
        str_t *v = &str[s];
        float pos = v->pos, y1 = v->y1, y2 = v->y2, e = v->v, g = v->g, c = v->c, m = v->m;
        const float inc = v->inc, gl = pan_l[s], gr = pan_r[s], sg = s == MEL ? mel_gain : 1.0f;
        uint32_t j;
        if (!v->on) {
            str_level(s, 0.0f);
            continue;
        }
        if (DXS) {
            render_dx(v, s, n);
            continue;
        }
        for (j = 0; j < n; j++) {
            float x1, x2, o;
            if (!(j & 15u)) {                       /* every 16 samples: the filter and the shimmer */
                float c96 = (0.15f + 0.85f * e) * v->k;
                int ph = (int)shim_ph + v->shim_off;
                c = 1.0f - fm_exp2f(2.1768707f * fm_log2f(1.0f - c96));   /* 96 kHz coefficient at 44.1 */
                m = (0.61f + 0.61f * sin3[ph % 360]) * v->modamt;
            }
            x1 = wave(OM_WAVE_HARP_BASE, pos) + wave(OM_WAVE_HARP_MOD, pos) * m;
            x2 = wave(OM_WAVE_HARP, pos);
            y1 += c * (x1 - y1);
            y2 += c * (x2 - y2);
            g += 0.05f * (e - g);
            o = (y1 * g1 + y2 * g2) * g * sg;
            mixl[j] += o * gl;
            mixr[j] += o * gr;
            send[j] += o * rsend;
            pos = adv(pos, inc);
            e -= sus_rate * (0.34f + 8.5f * e * e * e);
            if (v->rel)
                e *= REL_K;
            if (e <= 0.0f) {
                e = 0.0f;
                if (g < 1e-4f) {
                    v->on = 0;
                    midi(0x80 | STR_CH(s), v->note, 0);
                    break;
                }
            }
        }
        v->pos = pos;
        v->y1 = fm_flush(y1);
        v->y2 = fm_flush(y2);
        v->v = e;
        v->g = g;
        v->c = c;
        v->m = m;
        str_level(s, v->on ? e : 0.0f);
    }
    shim_ph += shim_rate * (float)n;
    while (shim_ph >= 360.0f)
        shim_ph -= 360.0f;
}

static float env_step(cv_t *v, int bass)
{
    switch (v->st) {
    case E_DEC:
        v->v -= (1.0f / OM_SR) / 0.333f;
        if (v->v <= 0.95f) {
            v->v = 0.95f;
            v->st = E_SUS;
        }
        break;
    case E_BDEC:
        v->v -= (1.0f / OM_SR) / 1.222f;
        if (v->v <= 0.95f) {
            v->v = 0.95f;
            v->st = E_REL;
        }
        break;
    case E_REL:
        v->v -= bass ? (1.0f / OM_SR) / 0.333f * (0.5f + v->v) : (1.0f / OM_SR) / 0.18f;
        break;
    case E_CUT:
        v->v -= (1.0f / OM_SR) / 0.004f;
        break;
    default:
        break;
    }
    if (v->v <= 0.0f) {
        v->v = 0.0f;
        if (v->g < 1e-4f) {
            v->st = E_OFF;
            midi(bass ? 0x82 : 0x81, v->note, 0);
        }
    }
    return v->v;
}

/* a chord note as a DX7's voice: the chord's own envelope over the patch with its key held */
static void render_dx_chord(cv_t *v, dxv_t *c, uint32_t n, float gc)
{
    uint32_t j;
    dx_render(c, dxbuf, n);
    for (j = 0; j < n && v->st != E_OFF; j++) {
        float o;
        v->g += 0.05f * (env_step(v, 0) - v->g);
        o = dxbuf[j] * v->g * gc * 0.4f;
        mixl[j] += o;
        mixr[j] += o;
        send[j] += o * csend;
    }
}

static void render_chord(uint32_t n)
{
    int i;
    uint32_t j;
    const float gc = 0.4f * cvol * 0.7071f, gb = cvol * 0.7071f * bvol;
    float lvl = 0.0f;
    for (i = 0; i < OM_NCHORD; i++) {
        cv_t *v = &chv[i];
        if (v->st == E_OFF)
            continue;
        if (DXC) {
            render_dx_chord(v, &dxc[i], n, gc);
            lvl = fm_maxf(lvl, v->v);
            continue;
        }
        for (j = 0; j < n && v->st != E_OFF; j++) {
            float e = env_step(v, 0), o;
            v->g += 0.05f * (e - v->g);
            o = wave(OM_WAVE_CHORD, v->pos) * v->g * gc;
            mixl[j] += o;
            mixr[j] += o;
            send[j] += o * csend;
            v->pos = adv(v->pos, v->inc);
        }
        lvl = fm_maxf(lvl, v->v);
    }
    om_chord_level = lvl;
    if (bsv.st != E_OFF) {
        cv_t *v = &bsv;
        for (j = 0; j < n && v->st != E_OFF; j++) {
            float e = env_step(v, 1), o;
            v->g += 0.05f * (e - v->g);
            o = (wave(OM_WAVE_BASS_BASE, v->pos) + wave(OM_WAVE_BASS_MOD, v->pos) * (1.0f - e * e)) * v->g * gb;
            mixl[j] += o;
            mixr[j] += o;
            send[j] += o * csend * 0.5f;               /* a little: a wet bass muddies */
            v->pos = adv(v->pos, v->inc);
        }
    }
    om_bass_level = bsv.st != E_OFF ? bsv.v : 0.0f;
}

static void render_drums(uint32_t n)
{
    int i;
    uint32_t j;
    const float g = rvol * (1.0f / 32768.0f) * 0.7071f;
    for (i = 0; i < OM_NDRUM; i++) {
        drum_t *d = &drum[i];
        const int16_t *w = OM_DRUM[i];
        uint32_t len = OM_DRUM_LEN[i];
        if (!d->on)
            continue;
        for (j = 0; j < n; j++) {
            float o;
            if (d->pos >= len) {
                d->on = 0;
                break;
            }
            o = (float)w[d->pos++] * g;
            mixl[j] += o;
            mixr[j] += o;
        }
    }
}

static void render_chunk(int32_t *out, uint32_t n, float gain)
{
    uint32_t j;
    for (j = 0; j < n; j++)
        mixl[j] = mixr[j] = send[j] = 0.0f;
    render_strings(n);
    render_chord(n);
    render_drums(n);
    for (j = 0; j < n; j++) {
        float wl, wr, l, r;
        reverb(send[j] * 0.5f, &wl, &wr);
        l = (mixl[j] + wl) * 0.42f * gain;
        r = (mixr[j] + wr) * 0.42f * gain;
        out[2 * j] = (int32_t)(fm_tanhf(l) * 8300000.0f);
        out[2 * j + 1] = (int32_t)(fm_tanhf(r) * 8300000.0f);
    }
}

void om_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 2.0f;
    static uint32_t smp;
    drain();
    dx_clock = (smp += n) / DX_TICK;
    if (om_ext && ++ext_age > 100u)                /* ~0.6 s without a tick: back to its own tempo */
        om_ext = 0;
    while (n) {
        uint32_t k = n > BLK ? BLK : n;
        if (om_playing && !om_ext) {
            if (pulse_left <= 0.0f) {
                clock_pulse();
                pulse_left += step_len * (1.0f / 6.0f);
            }
            if ((float)k > pulse_left)
                k = (uint32_t)pulse_left + 1u;
            if (k > n)
                k = n;
            pulse_left -= (float)k;
        }
        render_chunk(out, k, gain);
        out += 2 * k;
        n -= k;
    }
}
