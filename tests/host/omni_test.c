/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument (dsp/omni.c) on its own: the strum plate's voicing against the OM-108 owner's
 * manual (p.49), the chord and bass registers, envelopes that end, the rhythm's timing, and an output
 * that stays finite and inside full scale with everything playing at once. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "omni.h"

static int bad, midi_n;
static uint8_t mel_got[512];                   /* the melody's notes (MIDI channel 4), as they came */
static int mel_n, mel_strings;                 /* how many; and notes on the strings' channel */
void om_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (st == 0x93u && d2 && mel_n < 512)
        mel_got[mel_n++] = (uint8_t)d1;
    if (st == 0x90u && d2)
        mel_strings++;
    midi_n++;
}

static void check(const char *what, int ok)
{
    printf("%-58s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static int32_t buf[2 * 256];
static float peak;
static int finite_ok = 1;
static void run(int blocks)
{
    int b, i;
    for (b = 0; b < blocks; b++) {
        om_render(buf, 256, 4096);
        for (i = 0; i < 512; i++) {
            float x = (float)buf[i] / 8388608.0f;
            if (!(x == x))
                finite_ok = 0;
            if (fabsf(x) > peak)
                peak = fabsf(x);
        }
    }
}

static void run_ms(int ms) { run(ms * 441 / 2560 + 1); }

int main(void)
{
    uint8_t h[OM_NSTR], c[OM_NCHORD], bass;
    /* the manual's C major, zones 1..13: C4 E4 G3 | C5 E5 G4 | C6 E6 G5 | C7 E7 G6 | C8. OMNI's 16 keys
     * add a group below (octave 0), so the manual's zones are OMNI's strings 3..15 */
    static const uint8_t C_OM[13] = {60, 64, 55, 72, 76, 67, 84, 88, 79, 96, 100, 91, 108};
    om_voicing(0, CH_MAJ, 0, 0, h, c, &bass);
    check("C major: strings 3..15 are the OM-108's 13 zones", !memcmp(h + 3, C_OM, 13));
    check("C major: strings 0..2 are the group below (C3 E3 G2)", h[0] == 48 && h[1] == 52 && h[2] == 43);
    check("C major: chord C4 E4 G3 (the F#3..F4 window), bass C2", c[0] == 60 && c[1] == 64 && c[2] == 55 && bass == 36);
    om_voicing(7, CH_MAJ, 0, 0, h, c, &bass);
    check("G major: zone 1 is G3 B3 D4 (manual), bass G2", c[0] == 55 && c[1] == 59 && c[2] == 62 && bass == 43);
    om_voicing(0, CH_7, 0, 0, h, c, &bass);
    check("C7 is a triad without its fifth: C E Bb", c[0] == 60 && c[1] == 64 && c[2] == 58);
    om_voicing(0, CH_MAJ, 2, 1, h, c, &bass);
    check("transpose +2, octave +1: D, a group up", h[0] == 62 && c[0] == 62 && bass == 38);
    om_scale(0, 0, h);
    check("the plate as a scale: the white keys' own notes, F3..G5", h[0] == 53 && h[3] == 59 && h[4] == 60 && h[15] == 79);
    om_scale(2, 1, h);
    check("... transpose +2, octave +1: D major, an octave up", h[4] == 74 && h[3] == 73);
    {
        static const uint8_t C_LIN[16] = {36, 40, 43, 48, 52, 55, 60, 64, 67, 72, 76, 79, 84, 88, 91, 96};
        int i, t, k, up = 1;
        om_linear(0, CH_MAJ, 0, 0, h);
        check("the chord straight up the keys: C major, C2 to C7, the middle group at C4", !memcmp(h, C_LIN, 16));
        om_linear(11, CH_ADD9, 0, 1, h);
        check("... B add9, an octave up: B3 C#4 Eb4, the top B8", h[0] == 59 && h[1] == 61 && h[2] == 63 && h[15] == 119);
        for (i = 0; i < 12; i++)
            for (t = 0; t < CH_NTYPES; t++) {
                om_linear(i, t, 0, 0, h);
                for (k = 1; k < 16; k++)
                    up &= h[k] > h[k - 1];
            }
        check("... every chord: each key higher than the last", up);
    }
    om_chord_scale(9, CH_MIN, 0, 0, 0, h);
    check("the chord's scale: A minor up from A3", h[0] == 57 && h[2] == 60 && h[7] == 69 && h[15] == 83);
    om_chord_scale(7, CH_7, 0, 0, 0, h);
    check("... G7: Mixolydian, with its F", h[0] == 55 && h[6] == 65);
    om_chord_scale(0, CH_MAJ, 0, 0, -2, h);
    check("... moved two steps down: C major from A2, its C3 on the third key", h[0] == 45 && h[1] == 47 && h[2] == 48 && h[15] == 71);
    om_chord_scale(0, CH_MAJ, 0, 0, 6, h);
    check("... six steps up: from B3", h[0] == 59 && h[1] == 60 && h[15] == 84);
    om_chord_scale(0, CH_MAJ, 0, -1, -6, h);
    check("... six down, an octave down: from D2", h[0] == 26 && h[6] == 36);
    {
        int t, k, ok = 1;
        for (t = 0; t < CH_NTYPES; t++)
            for (k = 0; k < 3; k++) {
                int j, in = 0;
                for (j = 0; j < 7; j++)
                    in |= OM_TYPE_SCALE[t][j] == OM_TYPE_TONES[t][k];
                ok &= in;
            }
        check("every type's scale holds its chord's three tones", ok);
    }
    {
        int i, ok = 1;
        for (i = 0; i < 12 && ok; i++) {
            int t;
            for (t = 0; t < CH_NTYPES; t++) {
                int k;
                om_voicing(i, t, 0, 0, h, c, &bass);
                for (k = 0; k < OM_NSTR; k++)
                    ok &= h[k] >= 42 && h[k] <= 113;
                for (k = 0; k < OM_NCHORD; k++)
                    ok &= c[k] >= 54 && c[k] <= 65;
                ok &= bass >= 36 && bass <= 47;
            }
        }
        check("every chord: strings F#2..F8, chord F#3..F4, bass C2..B2", ok);
    }

    om_init();
    om_chord(0, CH_MAJ);
    om_strum(0);
    run(2);
    check("a pluck sounds", om_str_level[0] > 0.5f);
    run(44100 / 256 * 8);
    check("a pluck ends (sustain 50: within 8 s)", om_str_level[0] == 0.0f);
    om_set(P_SUSTAIN, 0);
    om_strum(5);
    run(44100 / 256);
    check("sustain 0: gone within 1 s", om_str_level[5] == 0.0f);

    om_gate(1);
    run(10);
    check("a chord button held: the chord and its bass sound", om_chord_level > 0.9f && om_bass_level > 0.9f);
    om_gate(0);
    run(44100 / 256);
    check("let go: both end within 1 s", om_chord_level == 0.0f && om_bass_level == 0.0f);

    {   /* the rhythm: Rock 1 at 120 BPM is a 16th every 5512.5 samples; 32 steps in 4 s */
        int n = 0, last = -1, b;
        om_set(P_TEMPO, 120);
        om_set(P_RHYTHM, 0);
        om_play(1);
        for (b = 0; b < 4 * 44100 / 256; b++) {
            run(1);
            if (om_step != last) {
                n++;
                last = om_step;
            }
        }
        check("Rock 1 at 120: 32 steps (+-1) in 4 s", n >= 31 && n <= 33);
        om_play(0);
        run(2);
        check("stop", !om_playing);
    }
    {   /* how a chord lies: root position and the inversions put that tone at the bottom, and (the
         * inversions) in the bass; the Omnichord's folding is as ever */
        uint8_t h[OM_NSTR], c[OM_NCHORD], c0[OM_NCHORD], bass, bass0;
        om_voicing(0, CH_MAJ, 0, 0, h, c0, &bass0);
        om_voicing_inv(0, CH_MAJ, INV_OM, 0, 0, h, c, &bass);
        check("inversion: the Omnichord's folding unchanged", c[0] == c0[0] && c[1] == c0[1] && c[2] == c0[2] && bass == bass0);
        om_voicing_inv(0, CH_MAJ, INV_ROOT, 0, 0, h, c, &bass);
        check("inversion: root position C E G, C in the bass", c[0] % 12 == 0 && c[0] < c[1] && c[1] < c[2] && bass % 12 == 0);
        om_voicing_inv(0, CH_MAJ, INV_1ST, 0, 0, h, c, &bass);
        check("inversion: the first, E G C, E in the bass", c[1] < c[2] && c[2] < c[0] && c[0] - c[1] == 8 && bass % 12 == 4);
        om_voicing_inv(0, CH_MAJ, INV_2ND, 0, 0, h, c, &bass);
        check("inversion: the second, G C E, G in the bass", c[2] < c[0] && c[0] < c[1] && bass % 12 == 7);
    }
    {   /* the chord sequence: C for a beat, G for two, round again. A beat is four steps (0.5 s at 120),
         * in the Blues (12/8) six (0.75 s) */
        static const uint8_t tab[OM_NTRK][OM_NSEQ][2] = {{{0 | CH_MAJ << 4, 1}, {7 | CH_7 << 4, 2}},
                                                         {{0}},
                                                         {{5 | CH_MIN << 4, 1}}};
        om_seq_tab = tab;
        om_seq(1, 0);
        om_play(1);
        run(44100 / 256 / 4);
        check("sequence: its first chord from the first step", om_seq_at == 0 && om_bass_level > 0.0f);
        run(44100 / 256 / 2);
        check("sequence: the second after a beat", om_seq_at == 1 && om_seq_n == 2);
        run(44100 / 256);
        check("sequence: round to the first after its two", om_seq_at == 0 && om_seq_n == 3);
        om_gate(0);
        run(8);                                        /* (the bass's last note ends) */
        {
            int b, on = 0;
            for (b = 0; b < 44100 / 256 / 2; b++) {
                float was = om_bass_level;
                run(1);
                on |= om_bass_level > was;
            }
            check("sequence: a button let go does not stop it (the bass plays on)", on);
        }
        om_seq_track(2);                               /* another track: when this one comes round */
        run(2);
        check("sequence: another track waits", om_seq_trk == 0);
        {
            int b;
            for (b = 0; b < 2 * 44100 / 256 && om_seq_trk != 2; b++)
                run(1);
            check("sequence: and plays from the round's end (Fm)", om_seq_trk == 2 && om_seq_at == 0 &&
                                                                     om_str_note[0] % 12 == 5);
        }
        om_seq_track(1);                               /* an empty one: the sequence rests */
        run(44100 / 256);
        check("sequence: an empty track rests", om_seq_trk == 1);
        om_play(0);
        om_seq_track(0);
        om_seq(1, 2);
        om_set(P_RHYTHM, 7);
        run(2);
        om_play(1);
        run(44100 / 256 * 6 / 10);
        check("sequence, Blues: the first chord still at 0.6 s", om_seq_at == 0);
        run(44100 / 256 * 3 / 10);
        check("sequence, Blues: the second at 0.9 s", om_seq_at == 1);
        check("sequence: the key moves its chords (G7 in D: A7)", om_str_note[0] % 12 == 9);
        om_play(0);
        om_seq(0, 0);
        om_gate(0);
        om_set(P_RHYTHM, 0);
        run(2);
    }
    {   /* everything at once, levels up: finite and inside full scale */
        int i, s;
        om_set(P_VOICE1, 100);
        om_set(P_VOICE2, 100);
        om_set(P_CHORD, 100);
        om_set(P_RHYVOL, 100);
        om_set(P_REVERB, 100);
        om_set(P_SPACE, 100);
        om_set(P_SUSTAIN, 100);
        om_gate(1);
        om_play(1);
        peak = 0.0f;
        for (i = 0; i < 8; i++)
            for (s = 0; s < OM_NSTR; s++) {
                om_strum(s);
                run(1);
            }
        run(44100 / 256 * 3);
        check("all at full: finite output", finite_ok);
        check("all at full: below full scale (the soft clip)", peak < 1.0f && peak > 0.3f);
        printf("  (peak %.2f)\n", (double)peak);
    }
    {   /* the plate: a tail that ends at the longest Space, and stays finite at full send */
        int b, i;
        double e0 = 0.0, e = 0.0;
        om_play(0);
        om_gate(0);
        run(1);                                        /* (om_init empties the queue) */
        om_init();
        om_set(P_REVERB, 100);
        om_set(P_SPACE, 100);
        om_set(P_SUSTAIN, 0);
        om_strum(8);
        for (b = 0; b < 44100 * 20 / 256; b++) {
            om_render(buf, 256, 4096);
            e = 0.0;
            for (i = 0; i < 512; i++)
                e += (double)buf[i] * (double)buf[i];
            if (b == 60)
                e0 = e;
        }
        check("plate at Space 100: the tail is 60 dB down within 20 s", e < e0 * 1e-6);
    }
    check("MIDI out sent notes", midi_n > 100);
    om_set(P_MIDI, 0);
    run(1);
    midi_n = 0;
    om_strum(3);
    run(2);
    check("MIDI out off: nothing sent", midi_n == 0);
    {   /* a DX7 bank's patches as voices: one patch (all six operators heard, the first with a level), the
         * rest of the bank empty */
        static uint8_t bank[OM_NDX][OM_DX_BYTES];
        uint8_t *o = bank[0] + 17 * 5;            /* op 1: the bank has op 6 first */
        int i, live;
        memset(o, 99, 7);                         /* rates, and levels 1..3: it holds */
        o[14] = 99;
        o[15] = 2;                                /* the note's own pitch */
        memset(bank[0] + 106, 50, 4);             /* the pitch envelope: level */
        bank[0][110] = 31;
        bank[0][117] = 24;
        memcpy(bank[0] + 118, "TEST      ", 10);
        om_init();
        check("DX: no bank, no patch", !om_voice_ok(OM_NSV) && !om_cvoice_ok(OM_NCV) && om_voice_ok(OM_NSV - 1));
        om_dx_tab = (const uint8_t (*)[OM_DX_BYTES])bank;
        check("DX: the patch is there, named; the next is not",
              om_voice_ok(OM_NSV) && om_cvoice_ok(OM_NCV) && !om_voice_ok(OM_NSV + 1) &&
                  !strcmp(om_voice_name(OM_NSV), "TEST") && !strcmp(om_cvoice_name(OM_NCV), "TEST"));
        om_set(P_REVERB, 0);
        om_set(P_SUSTAIN, 100);
        om_voice(OM_NSV);
        om_chord(0, CH_MAJ);
        run(1);
        peak = 0.0f;
        om_strum(8);
        run(20);
        check("DX: a string sounds", peak > 0.02f && finite_ok);
        for (i = 0; i < OM_NSTR; i++) {
            om_strum(i);
            run(4);
        }
        run(40);
        for (i = 0, live = 0; i < OM_NSTR; i++)
            live += om_str_level[i] > 0.0f;
        check("DX: of 16 strings plucked, 9 sound", live == OM_DX_POLY);
        check("DX: the last ones plucked", om_str_level[15] > 0.0f && om_str_level[7] > 0.0f && om_str_level[6] == 0.0f);
        om_voice(OM_NSV + 1);                     /* an empty patch: the harp */
        run(2);
        peak = 0.0f;
        om_strum(8);
        run(20);
        check("DX: an empty patch plays as the harp", peak > 0.02f);
        om_voice(0);
        om_cvoice(OM_NCV);
        run(40);
        peak = 0.0f;
        om_gate(1);
        run(40);
        check("DX: the chord sounds", peak > 0.02f && om_chord_level > 0.0f && finite_ok);
        om_gate(0);
        run(200);
        check("DX: and ends when let go", om_chord_level == 0.0f);
        om_cvoice(0);
        run(1);                                   /* (heard before om_init() empties the queue) */
        om_dx_tab = 0;
    }
    {   /* the melody: over a held C, then Am, its notes from the chord's scale in its register; the same
         * seed the same tune, another another; off, or no chord, nothing */
        static uint8_t first[512];
        int i, n1 = 0, inkey = 1, inrange = 1, steps = 0, same = 1, other = 0, run;
        for (run = 0; run < 4; run++) {
            om_init();
            om_melody(OM_MEL_LEVEL, run == 3 ? 0 : 80);
            om_melody(OM_MEL_SEED, run == 2 ? 7 : 1);
            om_chord(0, CH_MAJ);
            om_gate(1);
            om_play(1);
            mel_n = mel_strings = 0;
            run_ms(8000);
            om_chord(9, CH_MIN);
            run_ms(8000);
            om_play(0);
            om_gate(0);
            run_ms(500);
            if (run == 0) {
                memcpy(first, mel_got, sizeof first);
                n1 = mel_n;
                for (i = 0; i < mel_n; i++) {
                    int pc = mel_got[i] % 12;
                    inkey &= pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
                    inrange &= mel_got[i] >= 74 - 9 && mel_got[i] <= 74 + 10;
                    if (i)
                        steps += abs(mel_got[i] - mel_got[i - 1]) <= 4;
                }
                check("melody: plays, on MIDI channel 4 (not the strings')", mel_n > 20 && !mel_strings);
                check("melody: the chord's scale (C major, A minor)", inkey);
                check("melody: in its register (around D5)", inrange);
                check("melody: mostly steps (2/3 of its moves a third or less)", steps * 3 >= (mel_n - 1) * 2);
            } else if (run == 1) {
                same = mel_n == n1 && !memcmp(first, mel_got, (size_t)mel_n);
                check("melody: the same seed, the same tune", same);
            } else if (run == 2) {
                other = mel_n != n1 || memcmp(first, mel_got, (size_t)(mel_n < n1 ? mel_n : n1));
                check("melody: another seed, another tune", other);
            } else {
                check("melody: off, nothing", mel_n == 0);
            }
        }
        om_init();
        om_melody(OM_MEL_LEVEL, 80);
        om_play(1);
        mel_n = 0;
        run_ms(4000);
        om_play(0);
        check("melody: no chord, nothing", mel_n == 0);
    }
    printf("%s\n", bad ? "OMNI TEST FAILED" : "omni test passed");
    return bad != 0;
}
