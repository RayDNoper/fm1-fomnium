/* SPDX-License-Identifier: GPL-3.0-only */
/* A DX7's voice: six sine operators in its 32 algorithms, each with its own envelope (four rates, four
 * levels), its level and rate scaled along the keys, its detune, a ratio or a fixed pitch; the feedback and
 * the pitch envelope, and the LFO on the pitch and on the operators' levels. Played from a patch as a DX7 bank holds it (128 bytes; om_dx_tab, omni.h).
 *
 * The envelopes, the scaling along the keys and the pitches follow msfa, the engine of Dexed
 * (music-synthesizer-for-android: Copyright 2012 Google Inc., Apache-2.0), here with the operators in float.
 * The LFO's depth on the levels (a DX7's tremolo) is by ear, not measured from one.
 * Not here: touch (every note as one played firmly), and the key let go (the strings have none,
 * and the chord has its own envelope over the sound, as with the other FM voices).
 *
 * Part of omni.c (after fm_osc). An operator is counted from 0: op 1 is [0]. */
#define DX_TICK 64u                            /* the envelopes: every so many samples (msfa's block) */
typedef struct {
    uint32_t ph[6], inc[6], inc0[6];
    float g[6], dg[6], mix[6];                 /* an operator's level now, its step a sample; 1: it is heard */
    float y[8], fbp, fbk;                      /* the operators' outputs, [6] nothing, [7] the feedback */
    float dcx, dcy;                            /* what is heard, without its DC (an operator far below hearing) */
    int32_t lv[6], tgt[6], einc[6];            /* the envelopes: log2 levels, Q24 */
    int32_t plv, ptgt, pinc;                   /* the pitch envelope: octaves, Q24 */
    int16_t outl[6];
    uint8_t R[6][4], L[6][4], ix[6], rise[6], rs[6], src[6][4];
    uint8_t pR[4], pL[4], pix, prise, pflat, fixed, fbs, loop, left;
    /* the LFO: its phase and step (a turn: 2^32), its delay (as msfa counts it), how far it moves the pitch
     * (octaves) and each operator's level (log2), its wave, its sample and hold */
    uint32_t lph, linc, ldly, ldinc, ldinc2;
    float lpm, lam[6];
    uint8_t lwave, lrand, lon, still;          /* lon: it moves something; still: the pitch does not move */
} dxv_t;
static uint32_t dx_clock;                      /* envelope steps since the start: where an LFO that runs free is */

/* the algorithms: the operators heard (bit 0: op 1), each operator's modulators, and the feedback: from
 * which operator, into which (4 and 6: a loop round the stack) */
static const struct {
    uint8_t car, mod[6], fbs, fbd;
} DX_ALG[32] = {
    {0x05, {0x02, 0, 0x08, 0x10, 0x20, 0}, 5, 5},    {0x05, {0x02, 0, 0x08, 0x10, 0x20, 0}, 1, 1},
    {0x09, {0x02, 0x04, 0, 0x10, 0x20, 0}, 5, 5},    {0x09, {0x02, 0x04, 0, 0x10, 0x20, 0}, 3, 5},
    {0x15, {0x02, 0, 0x08, 0, 0x20, 0}, 5, 5},       {0x15, {0x02, 0, 0x08, 0, 0x20, 0}, 4, 5},
    {0x05, {0x02, 0, 0x18, 0, 0x20, 0}, 5, 5},       {0x05, {0x02, 0, 0x18, 0, 0x20, 0}, 3, 3},
    {0x05, {0x02, 0, 0x18, 0, 0x20, 0}, 1, 1},       {0x09, {0x02, 0x04, 0, 0x30, 0, 0}, 2, 2},
    {0x09, {0x02, 0x04, 0, 0x30, 0, 0}, 5, 5},       {0x05, {0x02, 0, 0x38, 0, 0, 0}, 1, 1},
    {0x05, {0x02, 0, 0x38, 0, 0, 0}, 5, 5},          {0x05, {0x02, 0, 0x08, 0x30, 0, 0}, 5, 5},
    {0x05, {0x02, 0, 0x08, 0x30, 0, 0}, 1, 1},       {0x01, {0x16, 0, 0x08, 0, 0x20, 0}, 5, 5},
    {0x01, {0x16, 0, 0x08, 0, 0x20, 0}, 1, 1},       {0x01, {0x0E, 0, 0, 0x10, 0x20, 0}, 2, 2},
    {0x19, {0x02, 0x04, 0, 0x20, 0x20, 0}, 5, 5},    {0x0B, {0x04, 0x04, 0, 0x30, 0, 0}, 2, 2},
    {0x1B, {0x04, 0x04, 0, 0x20, 0x20, 0}, 2, 2},    {0x1D, {0x02, 0, 0x20, 0x20, 0x20, 0}, 5, 5},
    {0x1B, {0, 0x04, 0, 0x20, 0x20, 0}, 5, 5},       {0x1F, {0, 0, 0x20, 0x20, 0x20, 0}, 5, 5},
    {0x1F, {0, 0, 0, 0x20, 0x20, 0}, 5, 5},          {0x0B, {0, 0x04, 0, 0x30, 0, 0}, 5, 5},
    {0x0B, {0, 0x04, 0, 0x30, 0, 0}, 2, 2},          {0x25, {0x02, 0, 0x08, 0x10, 0, 0}, 4, 4},
    {0x17, {0, 0, 0x08, 0, 0x20, 0}, 5, 5},          {0x27, {0, 0, 0x08, 0x10, 0, 0}, 4, 4},
    {0x1F, {0, 0, 0, 0, 0x20, 0}, 5, 5},             {0x3F, {0, 0, 0, 0, 0, 0}, 5, 5},
};

/* an output level, 0..99, as the envelope counts it */
static int dx_outlevel(int v)
{
    static const uint8_t LUT[20] = {0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46};
    return v >= 20 ? 28 + v : LUT[v];
}

/* an envelope to its next stage */
static void dx_env_to(dxv_t *v, int i, int ix)
{
    int lvl, q;
    v->ix[i] = (uint8_t)ix;
    if (ix > 3)
        return;
    lvl = ((dx_outlevel(v->L[i][ix]) >> 1) << 6) + v->outl[i] - 4256;
    v->tgt[i] = (lvl < 16 ? 16 : lvl) << 16;
    v->rise[i] = v->tgt[i] > v->lv[i];
    q = (v->R[i][ix] * 41 >> 6) + v->rs[i];
    q = q > 63 ? 63 : q;
    v->einc[i] = (4 + (q & 3)) << (2 + 6 + (q >> 2));
}

static void dx_pitch_to(dxv_t *v, int ix)
{
    static const uint8_t RATE[100] = {
        1,  2,  3,  3,  4,  4,  5,  5,  6,  6,  7,  7,  8,  8,  9,  9,  10,  10,  11,  11,  12,  12,  13,  13,  14,
        14, 15, 16, 16, 17, 18, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,  30,  31,  33,  34,  36,  37,  38,  39,
        41, 42, 44, 46, 47, 49, 51, 53, 54, 56, 58, 60, 62, 64, 66, 68, 70,  72,  74,  76,  79,  82,  85,  88,  91,
        94, 98, 102, 106, 110, 115, 120, 125, 130, 135, 141, 147, 153, 159, 165, 171, 178, 185, 193, 202, 211, 232,
        243, 254, 255};
    static const int8_t TAB[100] = {
        -128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43, -41, -39, -37, -35, -33, -32, -31,
        -30,  -29,  -28,  -27, -26, -25, -24, -23, -22, -21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11,
        -10,  -9,   -8,   -7,  -6,  -5,  -4,  -3,  -2,  -1,  0,   1,   2,   3,   4,   5,   6,   7,   8,   9,
        10,   11,   12,   13,  14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,
        30,   31,   32,   33,  34,  35,  38,  40,  43,  46,  49,  53,  58,  65,  73,  82,  92,  103, 115, 127};
    if (ix < 0) {                                 /* a note begins: from its last level */
        v->plv = TAB[v->pL[3]] * (1 << 19);
        ix = 0;
    }
    v->pix = (uint8_t)ix;
    if (ix > 3)
        return;
    v->ptgt = TAB[v->pL[ix]] * (1 << 19);
    v->prise = v->ptgt > v->plv;
    v->pinc = RATE[v->pR[ix]] * 1143;             /* 64 samples of 2^24 / (21.3 * 44100) */
}

/* the operators' steps for note n (as the patch has it: its transposition in) */
static void dx_pitch(dxv_t *v, const uint8_t *p, int n)
{
    int i;
    float oct;
    n += p[117] > 48 ? 24 : p[117] - 24;
    n = n < 0 ? 0 : n > 127 ? 127 : n;
    oct = (float)(n - 69) * (1.0f / 12.0f);
    for (i = 0; i < 6; i++) {
        const uint8_t *o = p + 17 * (5 - i);
        int det = (o[12] >> 3 & 15) - 7, coarse = o[15] >> 1 & 31, fine = o[16] > 99 ? 99 : o[16];
        float hz;
        det = det > 7 ? 7 : det;
        if (o[15] & 1) {                          /* a fixed pitch: 1, 10, 100, 1000 Hz and the fine above it */
            hz = fm_exp2f((float)((coarse & 3) * 100 + fine) * 0.0332192809f + (det > 0 ? (float)det * 0.000802f : 0.0f));
        } else {
            float l = oct + 8.78136f;             /* log2 of the note's Hz */
            l += 0.0209f * fm_expf(-0.396f * l) * (1.0f / 7.0f) * l * (float)det;
            hz = fm_exp2f(l) * tunefac * (coarse ? (float)coarse : 0.5f) * (1.0f + 0.01f * (float)fine);
        }
        v->inc0[i] = (uint32_t)(fm_minf(hz * (1.0f / OM_SR), 0.449f) * 4294967296.0f);
        if (v->still || v->fixed >> i & 1)
            v->inc[i] = v->inc0[i];
    }
}

static int dx_curve(int group, int depth, int curve)
{
    static const uint8_t EXP[33] = {0,  1,  2,  3,  4,  5,  6,   7,   8,   9,   11,  14,  16,  19,  23,  27, 33,
                                    39, 47, 56, 66, 80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250};
    int s = curve == 0 || curve == 3 ? group * depth * 329 >> 12 : EXP[group > 32 ? 32 : group] * depth * 329 >> 15;
    return curve < 2 ? -s : s;
}

/* a note begins. fresh: from silence (else the envelopes go on from where they are) */
static void dx_on(dxv_t *v, const uint8_t *p, int n, int fresh)
{
    int i, k, a = p[110] & 31, kn = n + (p[117] > 48 ? 24 : p[117] - 24);
    kn = kn < 0 ? 0 : kn > 127 ? 127 : kn;
    v->fixed = 0;
    for (i = 0; i < 4; i++) {
        v->pR[i] = p[102 + i] > 99 ? 99 : p[102 + i];
        v->pL[i] = p[106 + i] > 99 ? 99 : p[106 + i];
    }
    v->pflat = v->pL[0] == 50 && v->pL[1] == 50 && v->pL[2] == 50 && v->pL[3] == 50;
    for (i = 0; i < 6; i++) {
        const uint8_t *o = p + 17 * (5 - i);
        int ol = o[14] > 99 ? 99 : o[14], off = kn - (o[8] > 99 ? 99 : o[8]) - 17, x = kn / 3 - 7, m = 0;
        ol = dx_outlevel(ol) + (off >= 0 ? dx_curve((off + 1) / 3, o[10] > 99 ? 99 : o[10], o[11] >> 2 & 3) :
                                           dx_curve(-(off - 1) / 3, o[9] > 99 ? 99 : o[9], o[11] & 3));
        v->outl[i] = (int16_t)((ol < 0 ? 0 : ol > 127 ? 127 : ol) << 5);
        x = x < 0 ? 0 : x > 31 ? 31 : x;
        v->rs[i] = (uint8_t)((o[12] & 7) * x >> 3);
        for (k = 0; k < 4; k++) {
            v->R[i][k] = o[k] > 99 ? 99 : o[k];
            v->L[i][k] = o[4 + k] > 99 ? 99 : o[4 + k];
        }
        if (o[15] & 1)
            v->fixed |= (uint8_t)(1u << i);
        v->mix[i] = DX_ALG[a].car >> i & 1 ? 1.0f : 0.0f;
        for (k = 0; k < 4; k++)
            v->src[i][k] = 6;
        for (k = 0; k < 6; k++)
            if (DX_ALG[a].mod[i] >> k & 1)
                v->src[i][m++] = (uint8_t)k;
        if (DX_ALG[a].fbd == i)
            v->src[i][3] = 7;
        if (fresh) {
            v->lv[i] = 0;
            v->ph[i] = 0;
            v->g[i] = 0.0f;
        }
        dx_env_to(v, i, 0);
    }
    v->fbs = DX_ALG[a].fbs;
    v->loop = DX_ALG[a].fbs != DX_ALG[a].fbd;
    v->fbk = p[111] & 7 ? fm_exp2f((float)(p[111] & 7) - 9.0f) : 0.0f;
    if (fresh) {
        for (i = 0; i < 8; i++)
            v->y[i] = 0.0f;
        v->fbp = 0.0f;
        v->dcx = v->dcy = 0.0f;
    }
    {   /* the LFO: speed, delay, depths (pitch, levels), then sync | wave << 1 | the pitch's sensitivity << 4 */
        static const uint8_t PMS[8] = {0, 10, 20, 33, 55, 92, 153, 255};
        static const float AMS[4] = {0.0f, 0.26f, 0.43f, 1.0f};
        int sr = p[112] ? 165 * (p[112] > 99 ? 99 : p[112]) >> 6 : 1, d = 99 - (p[113] > 99 ? 99 : p[113]);
        float amd = (float)(p[115] > 99 ? 99 : p[115]) * (1.0f / 99.0f);
        sr *= sr < 160 ? 11 : 11 + ((sr - 160) >> 4);
        v->linc = 36557u * (uint32_t)sr;         /* 64 samples of 2^32 / 15.5 s / 11 */
        if (d == 99) {
            v->ldinc = v->ldinc2 = 0xFFFFFFFFu;
        } else {
            d = (16 + (d & 15)) << (1 + (d >> 4));
            v->ldinc = 36557u * (uint32_t)d;
            d &= 0xFF80;
            v->ldinc2 = 36557u * (uint32_t)(d < 0x80 ? 0x80 : d);
        }
        v->lwave = p[116] >> 1 & 7;
        v->lpm = (float)((p[114] > 99 ? 99 : p[114]) * 165 >> 6) * (1.0f / 256.0f) * (float)PMS[p[116] >> 4 & 7] * (1.0f / 128.0f);
        v->lon = v->lpm != 0.0f;
        for (i = 0; i < 6; i++) {
            v->lam[i] = AMS[p[17 * (5 - i) + 13] & 3] * amd * 5.0f;
            v->lon |= v->lam[i] != 0.0f;
        }
        v->lph = p[116] & 1 ? 0x7FFFFFFFu : v->linc * dx_clock;
        v->ldly = 0;
        v->lrand = 0;
    }
    v->still = v->pflat && v->lpm == 0.0f;
    dx_pitch_to(v, -1);
    dx_pitch(v, p, n);
    v->left = 0;
}

/* the LFO, one step: its wave now, 0..1, by *w; and how much of it there is yet (its delay), 0..1 */
static float dx_lfo(dxv_t *v, float *w)
{
    uint32_t ph = v->lph += v->linc, d = v->ldly + (v->ldly < 0x80000000u ? v->ldinc : v->ldinc2), x;
    switch (v->lwave) {
    case 0: x = (ph >> 7 ^ (0u - (ph >> 31))) & 0xFFFFFFu; break;      /* triangle */
    case 1: x = (~ph ^ 0x80000000u) >> 8; break;                        /* saw down */
    case 2: x = (ph ^ 0x80000000u) >> 8; break;                         /* saw up */
    case 3: x = ~ph >> 7 & 0x1000000u; break;                           /* square */
    case 4: x = (uint32_t)(8388608.0f + 8388607.0f * fm_osc(ph)); break; /* sine */
    case 5:                                                             /* sample and hold */
        if (ph < v->linc)
            v->lrand = (uint8_t)(v->lrand * 179u + 17u);
        x = (uint32_t)((v->lrand ^ 0x80u) + 1u) << 16;
        break;
    default: x = 0x800000u; break;                                      /* (no such wave: still) */
    }
    *w = (float)x * (1.0f / 16777216.0f);
    if (d < v->ldinc)                             /* (round once: all of it, from now on) */
        return 1.0f;
    v->ldly = d;
    return d < 0x80000000u ? 0.0f : (float)(d >> 7 & 0xFFFFFFu) * (1.0f / 16777216.0f);
}

/* the envelopes, one step: the levels the operators come to over the next DX_TICK samples */
static void dx_tick(dxv_t *v)
{
    float w = 0.0f, dly = 0.0f;
    int i;
    if (v->lon)
        dly = dx_lfo(v, &w);
    for (i = 0; i < 6; i++) {
        if (v->ix[i] < 3) {                       /* (the key stays down: the third level holds) */
            if (v->rise[i]) {
                if (v->lv[i] < 1716 << 16)
                    v->lv[i] = 1716 << 16;
                v->lv[i] += (((17 << 24) - v->lv[i]) >> 24) * v->einc[i];
                if (v->lv[i] >= v->tgt[i]) {
                    v->lv[i] = v->tgt[i];
                    dx_env_to(v, i, v->ix[i] + 1);
                }
            } else {
                v->lv[i] -= v->einc[i];
                if (v->lv[i] <= v->tgt[i]) {
                    v->lv[i] = v->tgt[i];
                    dx_env_to(v, i, v->ix[i] + 1);
                }
            }
        }
        v->dg[i] = (fm_exp2f((float)v->lv[i] * (1.0f / 16777216.0f) - 15.0f - v->lam[i] * dly * w) - v->g[i]) *
                   (1.0f / (float)DX_TICK);
    }
    if (!v->still) {
        float f;
        if (!v->pflat && v->pix < 3) {
            if (v->prise) {
                v->plv += v->pinc;
                if (v->plv >= v->ptgt) {
                    v->plv = v->ptgt;
                    dx_pitch_to(v, v->pix + 1);
                }
            } else {
                v->plv -= v->pinc;
                if (v->plv <= v->ptgt) {
                    v->plv = v->ptgt;
                    dx_pitch_to(v, v->pix + 1);
                }
            }
        }
        f = fm_exp2f((float)v->plv * (1.0f / 16777216.0f) + v->lpm * dly * (w - 0.5f));
        for (i = 0; i < 6; i++)
            v->inc[i] = v->fixed >> i & 1 ? v->inc0[i] :
                        (uint32_t)(fm_minf((float)v->inc0[i] * f, 0.449f * 4294967296.0f));
    }
}

/* an operator's input, 1: two turns of the phase (what a DX7's operator at its full level gives) */
static inline uint32_t dx_pm(float m) { return (uint32_t)(int32_t)(m * 134217728.0f) << 6; }

/* k samples (no more than DX_TICK), the operators heard added up. An operator at a time, from the last: its
 * modulators come after it, so they are there when it wants them */
static void dx_block(dxv_t *v, float *out, uint32_t k)
{
    static float b[6][DX_TICK], sum[DX_TICK];     /* (not on the audio interrupt's stack) */
    uint32_t j;
    int i;
    for (j = 0; j < k; j++)
        out[j] = 0.0f;
    for (i = 5; i >= 0; i--) {
        const uint8_t *s = v->src[i];
        const float dg = v->dg[i];
        const uint32_t inc = v->inc[i];
        float g = v->g[i], *y = b[i];
        uint32_t ph = v->ph[i];
        if (s[3] == 7) {                          /* its own feedback (such an operator has no modulators) */
            float f1 = v->fbp, fb = v->y[7];
            const float fk = v->fbk;
            for (j = 0; j < k; j++) {
                float o = fm_osc(ph + dx_pm(fb)) * g;
                y[j] = o;
                fb = (o + f1) * fk;
                f1 = o;
                g += dg;
                ph += inc;
            }
            v->fbp = f1;
            v->y[7] = fb;
        } else if (s[0] == 6) {                   /* a sine */
            for (j = 0; j < k; j++) {
                y[j] = fm_osc(ph) * g;
                g += dg;
                ph += inc;
            }
        } else {
            const float *m = b[s[0]];
            if (s[1] != 6) {
                const float *m1 = b[s[1]];
                for (j = 0; j < k; j++)
                    sum[j] = m[j] + m1[j];
                if (s[2] != 6)
                    for (m1 = b[s[2]], j = 0; j < k; j++)
                        sum[j] += m1[j];
                m = sum;
            }
            for (j = 0; j < k; j++) {
                y[j] = fm_osc(ph + dx_pm(m[j])) * g;
                g += dg;
                ph += inc;
            }
        }
        v->g[i] = g;
        v->ph[i] = ph;
        if (v->mix[i] != 0.0f)
            for (j = 0; j < k; j++)
                out[j] += y[j];
    }
}

/* the same a sample at a time, for the two algorithms whose feedback goes round a stack (4, 6) */
static void dx_loop(dxv_t *v, float *out, uint32_t k)
{
    float *y = v->y;
    uint32_t j;
    for (j = 0; j < k; j++) {
        float o = 0.0f, f;
        int i;
        for (i = 5; i >= 0; i--) {
            const uint8_t *s = v->src[i];
            y[i] = fm_osc(v->ph[i] + dx_pm(y[s[0]] + y[s[1]] + y[s[2]] + y[s[3]])) * v->g[i];
            o += y[i] * v->mix[i];
            v->g[i] += v->dg[i];
            v->ph[i] += v->inc[i];
        }
        f = y[v->fbs];
        y[7] = (f + v->fbp) * v->fbk;
        v->fbp = f;
        out[j] = o;
    }
}

/* n samples of the voice */
static void dx_render(dxv_t *v, float *out, uint32_t n)
{
    while (n) {
        uint32_t k, j;
        if (!v->left) {
            dx_tick(v);
            v->left = (uint8_t)DX_TICK;
        }
        k = n < v->left ? n : v->left;
        if (v->loop)
            dx_loop(v, out, k);
        else
            dx_block(v, out, k);
        for (j = 0; j < k; j++) {                 /* from 20 Hz up, as a DX7's output is */
            float o = out[j];
            out[j] = v->dcy = o - v->dcx + 0.997f * v->dcy;
            v->dcx = o;
        }
        out += k;
        v->left = (uint8_t)(v->left - k);
        n -= k;
    }
}

/* a patch holds a sound: an operator that is heard has a level */
static int dx_ok(const uint8_t *p)
{
    int i, car = DX_ALG[p[110] & 31].car;
    for (i = 0; i < 6; i++)
        if (car >> i & 1 && p[17 * (5 - i) + 14])
            return 1;
    return 0;
}
