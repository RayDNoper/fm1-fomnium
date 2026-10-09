/* SPDX-License-Identifier: GPL-3.0-only */
/* OMNI project: one flash object (OBJ_PROJ). A project of another format, or none, gives the
 * defaults. Part of the unity build, after app.h. */
project_t proj;

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
        memset(proj.rsv, 0, sizeof proj.rsv);
    }
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
    return proj.set < NSETS;
}

int project_load(void)
{
    int n;
    memset(&proj, 0, sizeof proj);
    n = plat_store_load(OBJ_PROJ, &proj, sizeof proj);
    if (n >= 8 && project_valid())
        return 0;
    project_defaults();
    return -1;
}

int project_save(void) { return plat_store_save(OBJ_PROJ, &proj, sizeof proj); }

void project_apply(void)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++)
        om_set(i, proj.par[i]);
}
