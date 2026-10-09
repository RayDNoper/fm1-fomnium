/* SPDX-License-Identifier: GPL-3.0-only */
/* OMNI editor link: SysEx over USB MIDI, for the web editor (web/omni_editor.html). The protocol is in
 * web/EDITOR_PROTOCOL.md; its framing follows the editor protocol of SLOOP and Felucca (one reply per
 * request, 7-bit bytes), with FoMni's own header and commands. Part of the unity build, after ui.c.
 *
 *   F0 7D 46 4F <cmd> <args...> F7        ("FO"; 7D: the non-commercial id)
 *
 * A request the device does not understand, or with a value out of range, gets no reply. */
enum { ED_INFO = 1, ED_STATE, ED_SETS, ED_PAD, ED_SET, ED_PUT };
#define ED_PROTO 1u
#define ED_NPAIR (2u * NSETS * NPADS)

static uint8_t ed_out[16 + ED_NPAIR];
static uint32_t ed_n;

static void ed_begin(uint32_t cmd)
{
    ed_out[0] = 0xF0;
    ed_out[1] = 0x7D;
    ed_out[2] = 0x46;
    ed_out[3] = 0x4F;
    ed_out[4] = (uint8_t)cmd;
    ed_n = 5;
}
static void ed_b(uint32_t v)
{
    if (ed_n < sizeof ed_out - 1u)
        ed_out[ed_n++] = (uint8_t)(v & 0x7Fu);
}
static void ed_send(void)
{
    ed_out[ed_n++] = 0xF7;
    plat_sysex_send(ed_out, ed_n);
}

/* what the editor shows, as one number: it asks for this now and then, and reads the sets again when
 * it has changed (a chord changed on the FM-1 itself, another set picked) */
static uint32_t ed_sig(void)
{
    uint32_t h = 2166136261u, s, i;
    for (s = 0; s < NSETS; s++)
        for (i = 0; i < NPADS; i++)
            h = (h ^ (proj.pad_root[s][i] | (uint32_t)proj.pad_type[s][i] << 4)) * 16777619u;
    return (h ^ proj.set) * 16777619u;
}

/* the chord that sounds from a button follows what its button now holds */
static void ed_follow(void)
{
    if (ui.pad >= 0) {
        ui.root = proj.pad_root[proj.set][ui.pad];
        ui.type = proj.pad_type[proj.set][ui.pad];
        sound_chord();
    }
}

void ed_service(void)
{
    static uint8_t a[ED_NPAIR];
    const uint8_t *p;
    uint32_t n, cmd, i, s;
    if (!plat_sysex_get(&p, &n))
        return;
    if (n < 4u || p[0] != 0x7Du || p[1] != 0x46u || p[2] != 0x4Fu)
        return;                                  /* not ours: left for the updater (ota_service) */
    cmd = p[3];
    n = n - 4u > sizeof a ? sizeof a + 1u : n - 4u;      /* too long: no command takes it */
    for (i = 0; i < n && i < sizeof a; i++)
        a[i] = p[4 + i];
    plat_sysex_done();
    switch (cmd) {
    case ED_INFO:
        if (n)
            return;
        ed_begin(cmd);
        ed_b(ED_PROTO);
        ed_b(NSETS);
        ed_b(NPADS);
        ed_b(CH_NTYPES);
        for (i = 0; OM_VERSION[i]; i++)
            ed_b((uint8_t)OM_VERSION[i]);
        ed_b(0);
        break;
    case ED_STATE:
        if (n)
            return;
        ed_begin(cmd);
        ed_b(proj.set);
        s = ed_sig();
        for (i = 0; i < 4u; i++)
            ed_b(s >> (7u * i));
        break;
    case ED_SETS:
        if (n)
            return;
        ed_begin(cmd);
        ed_b(proj.set);
        for (s = 0; s < NSETS; s++)
            for (i = 0; i < NPADS; i++) {
                ed_b(proj.pad_root[s][i]);
                ed_b(proj.pad_type[s][i]);
            }
        break;
    case ED_PAD:                                 /* set, button, root, type */
        if (n != 4u || a[0] >= NSETS || a[1] >= NPADS || a[2] > 11u || a[3] >= CH_NTYPES)
            return;
        if (proj.pad_root[a[0]][a[1]] != a[2] || proj.pad_type[a[0]][a[1]] != a[3]) {
            proj.pad_root[a[0]][a[1]] = a[2];
            proj.pad_type[a[0]][a[1]] = a[3];
            if (a[0] == proj.set && ui.pad == (int8_t)a[1])
                ed_follow();
            mark_dirty();
        }
        ed_begin(cmd);
        for (i = 0; i < 4u; i++)
            ed_b(a[i]);
        break;
    case ED_SET:                                 /* the set in use, as PRESETS picks it */
        if (n != 1u || a[0] >= NSETS)
            return;
        set_select(a[0]);
        ed_begin(cmd);
        ed_b(proj.set);
        break;
    case ED_PUT:                                 /* every set at once (a file loaded in the editor) */
        if (n != ED_NPAIR)
            return;
        for (i = 0; i < ED_NPAIR; i += 2u)
            if (a[i] > 11u || a[i + 1u] >= CH_NTYPES)
                return;
        for (s = 0; s < NSETS; s++)
            for (i = 0; i < NPADS; i++) {
                proj.pad_root[s][i] = a[2u * (s * NPADS + i)];
                proj.pad_type[s][i] = a[2u * (s * NPADS + i) + 1u];
            }
        ed_follow();
        mark_dirty();
        ed_begin(cmd);
        ed_b(0);
        break;
    default:
        return;
    }
    ed_send();
}
