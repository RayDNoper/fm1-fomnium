/* SPDX-License-Identifier: GPL-3.0-only */
/* OMNI project: one flash object (OBJ_PROJ). A project of another format, or none, gives the
 * defaults. Part of the unity build, after app.h. */
project_t proj;
uint8_t dx_bank[OM_NDX][OM_DX_BYTES];
static uint8_t bank_dirty;
void bank_touch(void) { bank_dirty = 1; }

static void bank_load(void)
{
    uint32_t h, i;
    memset(dx_bank, 0, sizeof dx_bank);
    for (h = 0; h < 2u; h++) {
        uint8_t *p = dx_bank[h * (OM_NDX / 2)];
        if (plat_store_load(OBJ_BANK0 + h, p, BANK_HALF) != (int)BANK_HALF)
            memset(p, 0, BANK_HALF);
        for (i = 0; i < BANK_HALF; i++)
            p[i] &= 0x7Fu;
    }
    bank_dirty = 0;
}

/* the factory chord buttons, left to right, in C: the primary chords on the first group of three,
 * their relative minors on the next two, then the dominants that lead into them */
static const uint8_t PAD_ROOT[NPADS] = {5, 0, 7, 2, 9, 4, 7, 4, 2, 10, 9};
static const uint8_t PAD_TYPE[NPADS] = {CH_MAJ, CH_MAJ, CH_MAJ, CH_MIN, CH_MIN, CH_MIN, CH_7, CH_7, CH_7, CH_MAJ,
                                        CH_7};
/* the chord sets as they come: the same buttons in C, G, D, A, E, F, Bb and Eb */
static const uint8_t SET_KEY[NSETS] = {0, 7, 2, 9, 4, 5, 10, 3};

static void sets_defaults(void)
{
    int s, i;
    for (s = 0; s < NSETS; s++)
        for (i = 0; i < NPADS; i++) {
            proj.pad_root[s][i] = (uint8_t)((PAD_ROOT[i] + SET_KEY[s]) % 12);
            proj.pad_type[s][i] = PAD_TYPE[i];
        }
}

static void mel_defaults(void)
{
    proj.mel = 0;
    proj.mel_den = 50;
    proj.mel_seed = 1;
    proj.mel_rep = 70;
}

void project_defaults(void)
{
    int i;
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    for (i = 0; i < P_NPARAMS; i++)
        proj.par[i] = om_param_info(i)->def;
    sets_defaults();
    proj.leds = 1;
    mel_defaults();
}

/* the steps' own melodies: no more notes than there is room for, each step's in order, one a place */
static int notes_ok(void)
{
    int k, j, tot = 0;
    for (k = 0; k < NTRK * NSEQ; k++) {
        int n = proj.notes.n[k / NSEQ][k % NSEQ];
        if (n > OM_MEL_STEP || tot + n > OM_MEL_NEV)
            return 0;
        for (j = 0; j < n; j++) {
            const uint8_t *e = proj.notes.ev[tot + j];
            if (e[0] >= OM_MEL_STEP || e[1] > 127u || (j && e[0] <= e[-2]))
                return 0;
        }
        tot += n;
    }
    return 1;
}

static int project_valid(void)
{
    int i, s;
    if (proj.magic != PROJ_MAGIC || proj.format < 1u || proj.format > PROJ_FORMAT)
        return 0;
    if (proj.format == 1u)                       /* 0.1: that slot was Lo-fi (0 / 1) */
        proj.par[P_CREV] = om_param_info(P_CREV)->def;
    if (proj.format < 3u) {                      /* 0.1, 0.2: one set of chords, then hold, sync and leds. */
        uint8_t old[2 * NPADS + 3];              /* It becomes the first set */
        memcpy(old, proj.pad_root, sizeof old);
        sets_defaults();
        memcpy(proj.pad_root[0], old, NPADS);
        memcpy(proj.pad_type[0], old + NPADS, NPADS);
        proj.hold = old[2 * NPADS];
        proj.sync = old[2 * NPADS + 1];
        proj.leds = old[2 * NPADS + 2];
        proj.set = 0;
        proj.plate = 0;
        proj.dark = 0;
        proj.voice = 0;
        proj.cvoice = 0;
        proj.shift = 0;
        proj.seq_on = 0;
        proj.seq_trk = 0;
        proj.bass = 0;
        memset(proj.seq, 0, sizeof proj.seq);
        memset(proj.pad_inv, 0, sizeof proj.pad_inv);
        memset(proj.rhy, 0, sizeof proj.rhy);
    }
    if (proj.format == 3u) {                     /* before the bank's patches came 19 and 34 FM sounds, now gone */
        proj.voice = proj.voice >= 20u ? (uint8_t)(proj.voice - 19u) : 0u;
        proj.cvoice = proj.cvoice >= 35u ? (uint8_t)(proj.cvoice - 34u) : 0u;
    }
    if (!proj.mel_seed || proj.mel > 100u || proj.mel_den > 100u || proj.mel_seed > 99u || proj.mel_rep > 100u)
        mel_defaults();                          /* from before the melody (its bytes were zeros) */
    if (proj.mel_layer > 1u)
        proj.mel_layer = 0;
    if (!notes_ok())                             /* (the steps' own melodies only: the rest is fine) */
        memset(&proj.notes, 0, sizeof proj.notes);
    proj.format = PROJ_FORMAT;
    for (i = 0; i < P_NPARAMS; i++) {
        const om_param_t *p = om_param_info(i);
        if (proj.par[i] < p->lo || proj.par[i] > p->hi)
            return 0;
    }
    for (s = 0; s < NSETS; s++)
        for (i = 0; i < NPADS; i++)
            if (proj.pad_root[s][i] > 11u || proj.pad_type[s][i] >= CH_NTYPES)
                return 0;
    for (s = 0; s < NTRK; s++)
        for (i = 0; i < NSEQ; i++)
            if ((proj.seq[s][i][0] & 15u) > 11u || proj.seq[s][i][0] >> 4 >= CH_NTYPES ||
                (proj.seq[s][i][1] & 63u) > OM_SEQ_BEATS)
                return 0;
    for (s = 0; s < NSETS; s++)
        for (i = 0; i < NPADS; i++)
            if (proj.pad_inv[s][i] >= INV_N)
                return 0;
    for (s = 0; s < OM_NURHY; s++)
        if (!om_rhythm_ok(proj.rhy[s]))
            return 0;
    if (proj.seq_on > 1u || proj.seq_trk >= NTRK || proj.bass < -50 || proj.bass > 50)
        return 0;
    return proj.set < NSETS && proj.plate < PL_NMODES && proj.dark <= 1u && proj.voice < OM_NVOICES && proj.cvoice < OM_NCVOICES &&
           proj.shift >= -6 && proj.shift <= 6;
}

int project_load(void)
{
    int n;
    memset(&proj, 0, sizeof proj);
    bank_load();
    n = plat_store_load(OBJ_PROJ, &proj, sizeof proj);
    if (n >= 8 && project_valid())
        return 0;
    project_defaults();
    return -1;
}

int seq_key(void) { return proj.pad_root[proj.set][1]; }

int project_save(void)
{
    int rc = plat_store_save(OBJ_PROJ, &proj, sizeof proj);
    if (rc == 0 && bank_dirty) {
        rc = plat_store_save(OBJ_BANK0, dx_bank[0], BANK_HALF);
        if (rc == 0)
            rc = plat_store_save(OBJ_BANK1, dx_bank[OM_NDX / 2], BANK_HALF);
        if (rc == 0)
            bank_dirty = 0;
    }
    return rc;
}

void project_apply(void)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++)
        om_set(i, proj.par[i]);
    om_plate(proj.plate, proj.shift);
    om_dx_tab = dx_bank;
    if (!om_voice_ok(proj.voice))                /* a patch that is not there (any more): the harp, the organ */
        proj.voice = 0;
    if (!om_cvoice_ok(proj.cvoice))
        proj.cvoice = 0;
    om_voice(proj.voice);
    om_cvoice(proj.cvoice);
    om_bass(proj.bass + 50);
    om_melody(OM_MEL_LEVEL, proj.mel);
    om_melody(OM_MEL_DENSITY, proj.mel_den);
    om_melody(OM_MEL_SEED, proj.mel_seed);
    om_melody(OM_MEL_REPEAT, proj.mel_rep);
    for (i = 0; i < OM_NURHY; i++)
        om_rhythm_user(i, proj.rhy[i]);
    om_seq_tab = proj.seq;
    om_seq_mel = proj.seq_mel;
    om_mel_notes = &proj.notes;
    om_seq(proj.seq_on, seq_key());
    om_seq_track(proj.seq_trk);
}
