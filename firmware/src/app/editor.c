/* SPDX-License-Identifier: GPL-3.0-only */
/* OMNI editor link: SysEx over USB MIDI, for the web editor (web/omni_editor.html). The protocol is in
 * web/EDITOR_PROTOCOL.md; its framing follows the editor protocol of SLOOP and Felucca (one reply per
 * request, 7-bit bytes), with Jambox's own header and commands. Part of the unity build, after ui.c.
 *
 *   F0 7D 46 4F <cmd> <args...> F7        ("FO"; 7D: the non-commercial id)
 *
 * A request the device does not understand, or with a value out of range, gets no reply. */
enum { ED_INFO = 1, ED_STATE, ED_SETS, ED_PAD, ED_SET, ED_PUT, ED_SETS2, ED_RHY, ED_RHYPUT, ED_RHYSEL, ED_RSTATE,
       ED_PLAY, ED_DX, ED_DXPUT, ED_DXUSE };
#define ED_PROTO 4u                              /* 2: how a chord lies (INV_*), in its type byte's bits 4, 5; */
                                                 /* 3: the rhythms; 4: the DX7 bank */
#define ED_RHY_N (2u + OM_RHY_NAME + 96u)        /* a rhythm on the wire: steps, beat, name, 32 x (drums, bass, chord) */
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
            h = (h ^ (proj.pad_root[s][i] | (uint32_t)proj.pad_type[s][i] << 4 | (uint32_t)proj.pad_inv[s][i] << 8)) *
                16777619u;
    return (h ^ proj.set) * 16777619u;
}

/* the chord that sounds from a button follows what its button now holds */
static void ed_follow(void)
{
    if (ui.pad >= 0) {
        ui.root = proj.pad_root[proj.set][ui.pad];
        ui.type = proj.pad_type[proj.set][ui.pad];
        ui.inv = proj.pad_inv[proj.set][ui.pad];
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
        ed_b((uint32_t)om_nrhythm());            /* (protocol 3: the rhythms it comes with, and the player's) */
        ed_b(OM_NURHY);
        ed_b(OM_NDX);                            /* (protocol 4: the DX7 bank's patches) */
        break;
    case ED_DX:                                  /* a patch of the bank, as a bank's SysEx holds it */
        if (n != 1u || a[0] >= OM_NDX)
            return;
        ed_begin(cmd);
        ed_b(a[0]);
        for (i = 0; i < OM_DX_BYTES; i++)
            ed_b(dx_bank[a[0]][i]);
        break;
    case ED_DXPUT:                               /* patch, then its bytes (all 0: none there) */
        if (n != 1u + OM_DX_BYTES || a[0] >= OM_NDX)
            return;
        memcpy(dx_bank[a[0]], a + 1, OM_DX_BYTES);
        if (!om_voice_ok(proj.voice)) {          /* the voice in use went: the harp, the organ */
            proj.voice = 0;
            om_voice(0);
        }
        if (!om_cvoice_ok(proj.cvoice)) {
            proj.cvoice = 0;
            om_cvoice(0);
        }
        bank_touch();
        mark_dirty();
        ed_begin(cmd);
        ed_b(a[0]);
        ed_b(0);
        break;
    case ED_DXUSE:                               /* a patch as the voice of the strings (0) or the chord (1) */
        if (n != 2u || a[0] > 1u || a[1] >= OM_NDX || !om_voice_ok(OM_NSV + (int)a[1]))
            return;
        if (a[0]) {
            proj.cvoice = (uint8_t)(OM_NCV + a[1]);
            om_cvoice(proj.cvoice);
        } else {
            proj.voice = (uint8_t)(OM_NSV + a[1]);
            om_voice(proj.voice);
        }
        mark_dirty();
        ed_begin(cmd);
        ed_b(a[0]);
        ed_b(a[1]);
        break;
    case ED_RHY: {                               /* a rhythm, by its number: as it plays */
        const om_rhythm_t *r;
        int own = n == 1u ? (int)a[0] - om_nrhythm() : -1;
        if (n != 1u || a[0] >= (uint32_t)om_nrhythm() + OM_NURHY)
            return;
        r = om_rhythm(a[0]);
        ed_begin(cmd);
        ed_b(a[0]);
        ed_b(own >= 0 && !proj.rhy[own][0] ? 0u : r->len);
        ed_b(r->beat);
        for (i = 0, s = 0; i < OM_RHY_NAME; i++) {
            s = s || !r->name[i];
            ed_b(s ? 0u : (uint8_t)r->name[i]);
        }
        for (i = 0; i < 32u; i++) {
            static const char C[] = "-TRON";
            uint32_t b = 0, c = 0, k;
            if (i < r->len)
                for (k = 0; k < 5u; k++) {
                    if (r->bass[i] == C[k])
                        b = k;
                    if (r->chord[i] == C[k])
                        c = k;
                }
            ed_b(i < r->len ? r->drum[i] : 0u);
            ed_b(b | (i < r->len ? (r->note[i] == '5' ? 1u : r->note[i] == '3' ? 2u : 0u) : 0u) << 3);
            ed_b(c);
        }
        break;
    }
    case ED_RHYPUT: {                            /* one of the player's: slot, then the rhythm (no steps: none) */
        uint8_t *q;
        if (n != 1u + ED_RHY_N || a[0] >= OM_NURHY)
            return;
        {
            uint8_t t[OM_RHY_BYTES];
            for (i = 0; i < 2u + OM_RHY_NAME; i++)
                t[i] = a[1 + i];
            for (i = 0; i < 32u; i++) {
                const uint8_t *st = a + 1 + 2 + OM_RHY_NAME + 3u * i;
                if (st[1] > 23u || st[2] > 4u)
                    return;
                t[2 + OM_RHY_NAME + 2u * i] = st[0];
                t[3 + OM_RHY_NAME + 2u * i] = (uint8_t)((st[1] & 7u) | st[2] << 3 | (st[1] >> 3) << 6);
            }
            if (!om_rhythm_ok(t))
                return;
            q = proj.rhy[a[0]];
            if (!t[0])
                memset(t, 0, sizeof t);
            memcpy(q, t, sizeof t);
        }
        om_rhythm_user((int)a[0], q);
        ui.rhy_n++;
        mark_dirty();
        ed_begin(cmd);
        ed_b(a[0]);
        ed_b(0);
        break;
    }
    case ED_RHYSEL:                              /* the rhythm in use, as ALGORITHM picks it (not an empty one) */
        if (n != 1u || a[0] >= (uint32_t)om_nrhythm() + OM_NURHY || rhy_empty(a[0]))
            return;
        if (proj.par[P_RHYTHM] != a[0]) {
            proj.par[P_RHYTHM] = a[0];
            om_set(P_RHYTHM, a[0]);
            mark_dirty();
        }
        ed_begin(cmd);
        ed_b(a[0]);
        break;
    case ED_RSTATE:                              /* the rhythm in use, whether it plays, and the rhythms' number */
        if (n)
            return;
        ed_begin(cmd);
        ed_b((uint32_t)proj.par[P_RHYTHM]);
        ed_b(om_playing);
        s = 2166136261u;
        for (i = 0; i < sizeof proj.rhy; i++)
            s = (s ^ ((const uint8_t *)proj.rhy)[i]) * 16777619u;
        for (i = 0; i < 4u; i++)
            ed_b(s >> (7u * i));
        break;
    case ED_PLAY:                                /* the rhythm on, off */
        if (n != 1u || a[0] > 1u)
            return;
        ui.armed = 0;
        om_play(a[0]);
        ed_begin(cmd);
        ed_b(a[0]);
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
    case ED_SETS:                                /* (SETS: as protocol 1 had it, the types alone) */
    case ED_SETS2:
        if (n)
            return;
        ed_begin(cmd);
        ed_b(proj.set);
        for (s = 0; s < NSETS; s++)
            for (i = 0; i < NPADS; i++) {
                ed_b(proj.pad_root[s][i]);
                ed_b(proj.pad_type[s][i] | (cmd == ED_SETS2 ? (uint32_t)proj.pad_inv[s][i] << 4 : 0u));
            }
        break;
    case ED_PAD:                                 /* set, button, root, type | how it lies << 4 */
        if (n != 4u || a[0] >= NSETS || a[1] >= NPADS || a[2] > 11u || (a[3] & 15u) >= CH_NTYPES || a[3] >> 4 >= INV_N)
            return;
        if (proj.pad_root[a[0]][a[1]] != a[2] || proj.pad_type[a[0]][a[1]] != (a[3] & 15u) ||
            proj.pad_inv[a[0]][a[1]] != a[3] >> 4) {
            proj.pad_root[a[0]][a[1]] = a[2];
            proj.pad_type[a[0]][a[1]] = a[3] & 15u;
            proj.pad_inv[a[0]][a[1]] = a[3] >> 4;
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
            if (a[i] > 11u || (a[i + 1u] & 15u) >= CH_NTYPES || a[i + 1u] >> 4 >= INV_N)
                return;
        for (s = 0; s < NSETS; s++)
            for (i = 0; i < NPADS; i++) {
                proj.pad_root[s][i] = a[2u * (s * NPADS + i)];
                proj.pad_type[s][i] = a[2u * (s * NPADS + i) + 1u] & 15u;
                proj.pad_inv[s][i] = a[2u * (s * NPADS + i) + 1u] >> 4;
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
