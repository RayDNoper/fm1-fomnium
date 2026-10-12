/* SPDX-License-Identifier: GPL-3.0-only */
/* OMNI UI: the panel and the screen. Part of the unity build (after gfx.c and project.c); the host
 * simulator includes it too.
 *
 *   white keys     the strum plate: each one plucks a string of the chord
 *   black keys     the chord buttons; hold ENV for the minor, LFO for the 7th, both for the m7
 *   PLAY           the rhythm on / off (with SYNC START: it waits for the first chord)
 *   REC            the chord sequence: on, and its page (there, with the rhythm stopped, a chord button
 *                  writes a step); again on its page: off. Held, the first eight chord buttons pick its track
 *   ARP            CHORD HOLD          OCT- / OCT+   the plate's octave
 *   HOME SEQ FX SEL GLO   the pages: PLAY, RHYTHM, SOUND, CHORDS, SETUP (GLO again: its second and third
 *                         page; EDIT steps through them all)
 *   SELECT tempo   ALGORITHM rhythm   PRESETS chord set   KNOB 1-4 the page's four values
 *   SAVE           saves (it also saves by itself, a few seconds after a change, when quiet) */

enum { V_PLAY, V_RHYTHM, V_SOUND, V_PADS, V_SETUP, V_SETUP2, V_SETUP3, V_SEQ, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Play", "Rhythm", "Sound", "Chords", "Setup", "Setup 2", "Setup 3",
                                              "Sequence"};
/* the four knobs of each page: a parameter, or one of these */
enum { K_PADROOT = 100, K_PADTYPE, K_LEDS, K_PLATE, K_DARK, K_VOICE, K_CVOICE, K_SHIFT, K_SYNC,
       K_SQSTEP, K_SQROOT, K_SQTYPE, K_SQBEATS, K_NONE };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_VOICE1, P_VOICE2, P_SUSTAIN, P_CHORD},
    {P_RHYTHM, P_TEMPO, P_RHYVOL, P_ABC},
    {P_REVERB, P_SPACE, P_WIDTH, P_CREV},
    {K_PADROOT, K_PADTYPE, P_TRANSPOSE, P_OCTAVE},
    {K_PLATE, K_SHIFT, P_TUNE, P_MIDI},
    {K_LEDS, K_DARK, K_VOICE, K_CVOICE},
    {K_SYNC, K_NONE, K_NONE, K_NONE},
    {K_SQSTEP, K_SQROOT, K_SQTYPE, K_SQBEATS},
};

/* Strings, in the knob's order (the saved numbers are PL_*) */
static const uint8_t PLATE_ORDER[PL_NMODES] = {PL_TRIAD, PL_LINEAR, PL_FIXED, PL_CHORD};
static const char *const PLATE_NAME[PL_NMODES] = {"Triads", "Linear", "Fixed", "Relative"};

static const uint8_t WHITE_K[OM_NSTR] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_K[NPADS] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};

/* the screen's colours: light and friendly. Cream paper, warm brown ink, one soft colour per section
 * (harp coral, chords teal, rhythm pink), each with a pale tint for the shapes at rest. And the same
 * at night (Setup 2, Dark): cream ink on a warm black, the tints deep */
enum { KI_BG, KI_PANEL, KI_LINE, KI_DIM, KI_TEXT, KI_WHITE, KI_HARP, KI_HARP_T, KI_CHORD, KI_CHORD_T, KI_RHY, KI_RHY_T,
       KI_N };
static const uint16_t PAL[2][KI_N] = {
    {RGB(250, 244, 232), RGB(236, 228, 212), RGB(216, 205, 186), RGB(150, 136, 118), RGB(72, 58, 48), RGB(255, 255, 255),
     RGB(242, 120, 72), RGB(248, 208, 184), RGB(38, 166, 154), RGB(196, 230, 222), RGB(232, 88, 128), RGB(246, 206, 218)},
    {RGB(28, 24, 22), RGB(46, 40, 36), RGB(74, 65, 58), RGB(144, 132, 118), RGB(240, 230, 214), RGB(255, 255, 255),
     RGB(246, 134, 86), RGB(96, 58, 44), RGB(64, 190, 176), RGB(30, 76, 72), RGB(240, 104, 142), RGB(92, 46, 60)},
};
#define K_(i) (PAL[proj.dark & 1u][i])
#define K_BG K_(KI_BG)
#define K_PANEL K_(KI_PANEL)
#define K_LINE K_(KI_LINE)
#define K_DIM K_(KI_DIM)
#define K_TEXT K_(KI_TEXT)
#define K_WHITE K_(KI_WHITE)
#define K_HARP K_(KI_HARP)
#define K_HARP_T K_(KI_HARP_T)
#define K_CHORD K_(KI_CHORD)
#define K_CHORD_T K_(KI_CHORD_T)
#define K_RHY K_(KI_RHY)
#define K_RHY_T K_(KI_RHY_T)

static struct {
    uint8_t view;
    uint32_t btn, keys, btn_used;
    uint32_t enc_t[NE];
    int8_t pad;                        /* the chord button sounding (or held), -1 none, -2 a chord from MIDI, */
                                       /* -3 one from the sequence */
    uint8_t pad_sel;                   /* the chord button the CHORDS page edits */
    uint8_t seq_sel;                   /* the step the SEQUENCE page edits, and a chord button writes */
    uint8_t seq_n, seq_drv;            /* om_seq_n as last seen; the sequence was playing */
    uint8_t seq_on, seq_key, seq_trk;  /* what the engine was told last */
    uint32_t seq_look;                 /* until then the page shows the cursor's half, though another plays */
    uint8_t npads_down;
    uint8_t root, type;                /* the chord (before transpose) */
    uint8_t armed;                     /* SYNC START waiting for a chord */
    uint8_t dirty;                     /* the project changed since the last save */
    uint32_t act_t, saved_t;
    char msg[2][24];
    uint32_t msg_until;
    int8_t touched;                    /* the knob turned last (0..3), shown bright for a moment */
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[3];                   /* what each band shows: redrawn when it changes */
} ui;

static void say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
}
void ui_say(const char *a, const char *b) { say(a, b); }

static void itoa_u(uint32_t v, char *b)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static int str_len_(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static void mark_dirty(void) { ui.dirty = 1; }

static uint32_t kb_held[4];                    /* MIDI in channel 2: the notes held (128) */
static int kb_any(void) { return (kb_held[0] | kb_held[1] | kb_held[2] | kb_held[3]) != 0; }
int ui_dirty(void) { return ui.dirty != 0; }

static void chord_name(int root, int type, char *b)
{
    const char *n = OM_NOTE_NAME[((root + proj.par[P_TRANSPOSE]) % 12 + 12) % 12], *t = OM_TYPE_NAME[type];
    while (*n)
        *b++ = *n++;
    while (*t)
        *b++ = *t++;
    *b = 0;
}

/* the sequence: the track in use (the one the page shows and edits), its steps (up to the first of no beats),
 * and whether a track plays */
#define SQ (proj.seq[proj.seq_trk])
#define SQ_ROOT(i) (SQ[i][0] & 15u)
#define SQ_TYPE(i) (SQ[i][0] >> 4)
#define SQ_BEATS(i) (SQ[i][1])
static int seq_len(void)
{
    int n = 0;
    while (n < NSEQ && SQ_BEATS(n))
        n++;
    return n;
}
static int seq_plays(void) { return proj.seq_on && om_playing && proj.seq[om_seq_trk % NTRK][0][1]; }

/* ------------------------------------------------------------ values --- */
static int knob_get(int k)
{
    if (k == K_SYNC)
        return proj.sync;
    if (k == K_SQSTEP)
        return ui.seq_sel;
    if (k == K_SQROOT)                           /* as it sounds in this chord set's key */
        return (int)(SQ_ROOT(ui.seq_sel) + (uint32_t)seq_key()) % 12;
    if (k == K_SQTYPE)
        return SQ_TYPE(ui.seq_sel);
    if (k == K_SQBEATS)
        return SQ_BEATS(ui.seq_sel);
    if (k < P_NPARAMS)
        return proj.par[k];
    if (k == K_PADROOT)
        return proj.pad_root[proj.set][ui.pad_sel];
    if (k == K_PADTYPE)
        return proj.pad_type[proj.set][ui.pad_sel];
    if (k == K_LEDS)
        return proj.leds;
    if (k == K_PLATE) {
        int i = 0;
        while (i < PL_NMODES - 1 && PLATE_ORDER[i] != proj.plate)
            i++;
        return i;
    }
    if (k == K_SHIFT)                            /* the Relative scale's place on the keys: only then */
        return proj.plate == PL_CHORD ? proj.shift : 0;
    if (k == K_DARK)
        return proj.dark;
    if (k == K_VOICE)
        return proj.voice;
    if (k == K_CVOICE)
        return proj.cvoice;
    return 0;
}
static void knob_range(int k, int *lo, int *hi)
{
    if (k < P_NPARAMS) {
        *lo = om_param_info(k)->lo;
        *hi = om_param_info(k)->hi;
    } else if (k == K_SHIFT) {
        *lo = proj.plate == PL_CHORD ? -6 : 0;
        *hi = proj.plate == PL_CHORD ? 6 : 0;
    } else if (k == K_SQSTEP) {                  /* the steps there are, and the one after them */
        *lo = 0;
        *hi = seq_len() < NSEQ ? seq_len() : NSEQ - 1;
    } else {
        *lo = 0;
        *hi = k == K_SYNC ? 1 : k == K_SQROOT ? 11 : k == K_SQTYPE ? CH_NTYPES - 1 : k == K_SQBEATS ? OM_SEQ_BEATS :
              k == K_PADROOT ? 11 : k == K_PADTYPE ? CH_NTYPES - 1 : k == K_LEDS ? 2 : k == K_PLATE ? PL_NMODES - 1 : k == K_DARK ? 1 : k == K_VOICE ? OM_NVOICES - 1 :
              k == K_CVOICE ? OM_NCVOICES - 1 : 0;
    }
}
static const char *knob_name(int k)
{
    if (k < P_NPARAMS)
        return om_param_info(k)->name;
    return k == K_PADROOT ? "Root" : k == K_PADTYPE ? "Type" : k == K_LEDS ? "Lights" :
           k == K_PLATE ? "Strings" : k == K_DARK ? "Dark" : k == K_VOICE ? "Harp" : k == K_CVOICE ? "Chord" :
           k == K_SHIFT ? "Shift" : k == K_SYNC ? "Sync start" : k == K_SQSTEP ? "Step" : k == K_SQROOT ? "Root" :
           k == K_SQTYPE ? "Type" : k == K_SQBEATS ? "Beats" : "";
}
static void knob_text(int k, char *b)
{
    int v = knob_get(k);
    if (k < P_NPARAMS) {
        om_param_text(k, v, b);
    } else if (k == K_SQSTEP) {
        itoa_u((uint32_t)v + 1u, b);
    } else if (k == K_SQBEATS) {                 /* none: the sequence ends before this step */
        if (v) {
            itoa_u((uint32_t)v, b);
        } else {
            b[0] = 'E';
            b[1] = 'n';
            b[2] = 'd';
            b[3] = 0;
        }
    } else if (k == K_PADROOT || k == K_SQROOT) {
        const char *n = OM_NOTE_NAME[v];
        while ((*b++ = *n++))
            ;
    } else if (k == K_PADTYPE || k == K_SQTYPE) {
        const char *n = v ? OM_TYPE_NAME[v] : "maj";
        while ((*b++ = *n++))
            ;
    } else if (k == K_LEDS) {                    /* On: the keys, and the unlit buttons glow; Keys: no glow */
        const char *n = v == 2 ? "Keys" : v ? "On" : "Off";
        while ((*b++ = *n++))
            ;
    } else if (k == K_SHIFT) {                   /* steps of the scale; "-" unless Strings is Relative */
        if (proj.plate != PL_CHORD) {
            b[0] = '-';
            b[1] = 0;
        } else {
            if (v > 0)
                *b++ = '+';
            if (v < 0)
                *b++ = '-';
            itoa_u((uint32_t)(v < 0 ? -v : v), b);
        }
    } else if (k == K_VOICE || k == K_CVOICE) {
        const char *n = k == K_VOICE ? om_voice_name(v) : om_cvoice_name(v);
        while ((*b++ = *n++))
            ;
    } else if (k == K_DARK || k == K_SYNC) {
        const char *n = v ? "On" : "Off";
        while ((*b++ = *n++))
            ;
    } else if (k == K_PLATE) {                   /* the chord's tones (folded, the OM's plate; or straight up), */
        const char *n = PLATE_NAME[v];           /* the keys' own notes, the chord's scale */
        while ((*b++ = *n++))
            ;
    } else {
        b[0] = 0;
    }
}

static void sound_chord(void);
static void knob_set(int k, int v)
{
    int lo, hi;
    knob_range(k, &lo, &hi);
    v = v < lo ? lo : v > hi ? hi : v;
    if (v == knob_get(k))
        return;
    if (k < P_NPARAMS) {
        proj.par[k] = (int16_t)v;
        om_set(k, v);
    } else if (k == K_PADROOT || k == K_PADTYPE) {
        if (k == K_PADROOT)
            proj.pad_root[proj.set][ui.pad_sel] = (uint8_t)v;
        else
            proj.pad_type[proj.set][ui.pad_sel] = (uint8_t)v;
        if (ui.pad == (int8_t)ui.pad_sel) {        /* the button being edited is the one sounding */
            ui.root = proj.pad_root[proj.set][ui.pad_sel];
            ui.type = proj.pad_type[proj.set][ui.pad_sel];
            om_chord(ui.root, ui.type);
        }
    } else if (k == K_LEDS) {
        proj.leds = (uint8_t)v;
    } else if (k == K_PLATE) {
        proj.plate = PLATE_ORDER[v];
        om_plate(proj.plate, proj.shift);
    } else if (k == K_SHIFT) {
        proj.shift = (int8_t)v;
        om_plate(proj.plate, v);
    } else if (k == K_DARK) {                    /* the screen's colours: every band is drawn again */
        proj.dark = (uint8_t)v;
    } else if (k == K_VOICE) {
        proj.voice = (uint8_t)v;
        om_voice(v);
    } else if (k == K_CVOICE) {
        proj.cvoice = (uint8_t)v;
        om_cvoice(v);
    } else if (k == K_SYNC) {                    /* SYNC START: PLAY waits for the first chord */
        proj.sync = (uint8_t)v;
        if (!v)
            ui.armed = 0;
    } else if (k == K_SQSTEP) {                  /* (the page's cursor: nothing to save) */
        ui.seq_sel = (uint8_t)v;
        ui.seq_look = plat_ms() + 4000u;
        return;
    } else if (k == K_SQROOT || k == K_SQTYPE || k == K_SQBEATS) {
        uint8_t *st = SQ[ui.seq_sel];
        if (k == K_SQROOT)
            st[0] = (uint8_t)((st[0] & 0xF0u) | (uint32_t)((v - seq_key() + 12) % 12));
        else if (k == K_SQTYPE)
            st[0] = (uint8_t)((st[0] & 15u) | (uint32_t)v << 4);
        else
            st[1] = (uint8_t)v;
        ui.seq_look = plat_ms() + 4000u;
    }
    mark_dirty();
}

/* knob acceleration by speed: a slow turn is one step a detent, a quick one up to 6 */
static uint8_t accel_off;              /* the host simulator's "spin": exact steps */
static int32_t accel(int role, int32_t s, int range)
{
    uint32_t now = plat_ms(), dt = now - ui.enc_t[role], a = (uint32_t)(s < 0 ? -s : s), m;
    ui.enc_t[role] = now;
    if (range <= 24 || !a || accel_off)
        return s;
    if (a > 1)
        dt /= a;
    m = dt < 15u ? 6u : dt < 30u ? 4u : dt < 60u ? 2u : 1u;
    return s * (int32_t)m;
}

static void turn(int role, int k, int32_t e)
{
    int lo, hi;
    if (!e || k == K_NONE)
        return;
    knob_range(k, &lo, &hi);
    knob_set(k, knob_get(k) + accel(role, e, hi - lo));
}

/* ------------------------------------------------------------- chords --- */
static void sound_chord(void) { om_chord(ui.root, ui.type); }

/* the SEQUENCE page, the rhythm stopped: a chord button writes its chord at the cursor, which moves on. A
 * new step takes the beats of the one before it (the first: four) */
static void seq_write(int root, int type)
{
    int i = ui.seq_sel;
    SQ[i][0] = (uint8_t)((root - seq_key() + 12) % 12 | type << 4);
    if (!SQ_BEATS(i))
        SQ[i][1] = i ? SQ_BEATS(i - 1) : 4;
    if (i < NSEQ - 1)
        ui.seq_sel++;
    mark_dirty();
}

/* REC held and one of the first eight chord buttons: that track. It plays when the one playing comes round */
static void seq_track(int t)
{
    char n[4];
    if (t != proj.seq_trk) {
        proj.seq_trk = (uint8_t)t;
        ui.seq_sel = 0;
        mark_dirty();
    }
    itoa_u((uint32_t)t + 1u, n);
    say("TRACK", n);
}

/* the engine hears of the sequence being on, of its track, and of the chord set's key (PRESETS, or its second button
 * changed: here or from the editor). While it plays, the screen follows its chords; when it stops, its
 * chord is let go */
static void seq_follow(void)
{
    int drv = seq_plays(), key = seq_key();
    if (proj.seq_on != ui.seq_on || key != ui.seq_key) {
        ui.seq_on = proj.seq_on;
        ui.seq_key = (uint8_t)key;
        om_seq(proj.seq_on, key);
    }
    if (proj.seq_trk != ui.seq_trk) {
        ui.seq_trk = proj.seq_trk;
        om_seq_track(proj.seq_trk);
    }
    if (ui.seq_sel > seq_len())
        ui.seq_sel = (uint8_t)seq_len();
    if (drv && om_seq_n != ui.seq_n) {
        const uint8_t *st = proj.seq[om_seq_trk % NTRK][om_seq_at % NSEQ];
        ui.root = (uint8_t)(((st[0] & 15) + key) % 12);
        ui.type = (uint8_t)(st[0] >> 4);
        ui.pad = -3;
    }
    ui.seq_n = om_seq_n;
    if (!drv && ui.seq_drv && !ui.npads_down && !(ui.pad == -2 && kb_any())) {
        om_gate(0);
        ui.pad = -1;
    }
    ui.seq_drv = (uint8_t)drv;
}

static void pad_down(int p)
{
    uint32_t b = ui.btn;
    int type = proj.pad_type[proj.set][p];
    int mods = (b >> B_ENV & 1u) | (b >> B_LFO & 1u) << 1;
    if (b >> B_REC & 1u) {                       /* REC held: the sequence's track */
        ui.btn_used |= 1u << B_REC;
        ui.npads_down++;
        if (p < NTRK)
            seq_track(p);
        return;
    }
    if (mods)                                    /* the OM's rows: minor, 7th, both: m7 */
        type = mods == 1 ? CH_MIN : mods == 2 ? CH_7 : CH_M7;
    if (mods & 1)
        ui.btn_used |= 1u << B_ENV;
    if (mods & 2)
        ui.btn_used |= 1u << B_LFO;
    if (ui.view == V_PADS)
        ui.pad_sel = (uint8_t)p;
    if (ui.view == V_SEQ && proj.seq_on && !om_playing)
        seq_write(proj.pad_root[proj.set][p], type);
    if (proj.hold && ui.pad == p && !ui.npads_down && ui.root == proj.pad_root[proj.set][p] && ui.type == type) {
        ui.pad = -1;                             /* HOLD: the sounding button again lets it go */
        om_gate(0);
        ui.npads_down++;
        return;
    }
    ui.npads_down++;
    ui.pad = (int8_t)p;
    ui.root = proj.pad_root[proj.set][p];
    ui.type = (uint8_t)type;
    sound_chord();
    om_gate(1);
    if (ui.armed) {                              /* SYNC START: the rhythm starts with the chord */
        ui.armed = 0;
        om_play(1);
    }
}

static void pad_up(int p)
{
    if (ui.npads_down)
        ui.npads_down--;
    if (ui.npads_down || proj.hold)
        return;
    if (ui.pad == p || ui.pad >= 0) {
        om_gate(0);
        ui.pad = -1;
    }
}

/* PRESETS: another chord set under the buttons. A button that is sounding goes to its chord there */
static void set_select(int s)
{
    char t[8] = "SET ";
    s = s < 0 ? 0 : s >= NSETS ? NSETS - 1 : s;
    if (s != proj.set) {
        proj.set = (uint8_t)s;
        if (ui.pad >= 0) {
            ui.root = proj.pad_root[s][ui.pad];
            ui.type = proj.pad_type[s][ui.pad];
            sound_chord();
        }
        mark_dirty();
    }
    itoa_u((uint32_t)s + 1u, t + 4);
    say("CHORDS", t);
}

/* ------------------------------------------------------------ buttons --- */
static void set_view(int v)
{
    ui.view = (uint8_t)v;
    ui.touched = -1;
}

static void button(int b)
{
    switch (b) {
    case B_HOME: set_view(V_PLAY); break;
    case B_SEQ: set_view(V_RHYTHM); break;
    case B_FX: set_view(V_SOUND); break;
    case B_SEL: set_view(V_PADS); break;
    case B_GLO: set_view(ui.view == V_SETUP ? V_SETUP2 : ui.view == V_SETUP2 ? V_SETUP3 : V_SETUP); break;
    case B_EDIT: set_view((ui.view + 1) % NVIEWS); break;
    case B_PLAY:
        if (om_playing) {
            om_play(0);
            ui.armed = 0;
        } else if (ui.armed) {
            ui.armed = 0;
            say("SYNC START", "CANCELLED");
        } else if (proj.sync && ui.pad == -1 && !(proj.seq_on && SQ_BEATS(0))) {
            ui.armed = 1;
            say("WAITING FOR", "A CHORD");
        } else {
            om_play(1);
        }
        break;
    case B_REC:                                  /* the sequence: on, to its page, and from there off */
        if (proj.seq_on && ui.view != V_SEQ) {
            set_view(V_SEQ);
            break;
        }
        proj.seq_on = (uint8_t)!proj.seq_on;
        ui.armed = 0;
        set_view(proj.seq_on ? V_SEQ : V_PLAY);
        say("SEQUENCE", proj.seq_on ? "ON" : "OFF");
        seq_follow();
        mark_dirty();
        break;
    case B_ARP:
        proj.hold = (uint8_t)!proj.hold;
        if (!proj.hold && !ui.npads_down && ui.pad != -1 && !(ui.pad == -2 && kb_any())) {
            om_gate(0);
            ui.pad = -1;
        }
        say("CHORD HOLD", proj.hold ? "ON" : "OFF");
        mark_dirty();
        break;
    case B_OCTDN: case B_OCTUP:
        knob_set(P_OCTAVE, proj.par[P_OCTAVE] + (b == B_OCTUP ? 1 : -1));
        {
            char t[8];
            om_param_text(P_OCTAVE, proj.par[P_OCTAVE], t);
            say("OCTAVE", t);
        }
        break;
    case B_SAVE:
        if (project_save() == 0) {
            ui.dirty = 0;
            say("SAVED", 0);
        } else {
            say("NOT SAVED", "FLASH ERROR");
        }
        break;
    default: break;
    }
}

/* MIDI in, channel 2: the chord held on a keyboard. The best fit of the nine types: a root among the
 * notes (the lowest preferred), as many of its tones held as possible and nothing left over but its
 * fifth (a held C E G Bb is C7, which the OM plays without the G). Under two notes: no chord. */
static int kb_chord(int *root, int *type)
{
    uint32_t pcs = 0, n;
    int low = -1, r, t, best = -1;
    for (n = 0; n < 128u; n++)
        if (kb_held[n >> 5] >> (n & 31u) & 1u) {
            pcs |= 1u << (n % 12u);
            if (low < 0)
                low = (int)(n % 12u);
        }
    if (low < 0 || !(pcs & (pcs - 1u)))
        return 0;
    for (r = 0; r < 12; r++) {
        if (!(pcs >> r & 1u))
            continue;
        for (t = 0; t < CH_NTYPES; t++) {
            uint32_t tones = 0, k;
            int score, hit = 0, extra = 0;
            for (k = 0; k < 3u; k++)
                tones |= 1u << ((r + OM_TYPE_TONES[t][k]) % 12);
            for (k = 0; k < 12u; k++) {
                hit += (pcs & tones) >> k & 1u;
                extra += (pcs & ~tones & ~(1u << ((r + 7) % 12))) >> k & 1u;
            }
            score = hit * 10 - extra * 3 + (r == low) * 2 - t / 4;   /* the common types first, on a tie */
            if (score > best) {
                best = score;
                *root = r;
                *type = t;
            }
        }
    }
    return best >= 20;
}

static void kb_update(void)
{
    int root, type;
    if (kb_chord(&root, &type)) {
        if (ui.pad == -2 && ui.root == root && ui.type == type)
            return;
        ui.root = (uint8_t)root;
        ui.type = (uint8_t)type;
        ui.pad = -2;                             /* a chord from MIDI: no button lit */
        sound_chord();
        om_gate(1);
        if (ui.armed) {
            ui.armed = 0;
            om_play(1);
        }
    } else if (ui.pad == -2 && !kb_any() && !proj.hold) {
        ui.pad = -1;                             /* all let go */
        om_gate(0);
    }
}

/* MIDI in (USB and the TRS jack): channel 1 notes pluck the nearest string, channel 2 notes pick the
 * chord, clock and start / stop drive the rhythm */
static void midi_in(void)
{
    uint32_t pkt;
    int chords = 0;
    while (plat_midi_in(&pkt)) {
        uint32_t st = (pkt >> 8) & 0xFFu, n = (pkt >> 16) & 0x7Fu, v = pkt >> 24;
        if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu) {
            om_clock(st == 0xF8u ? OM_CLK_TICK : st == 0xFAu ? OM_CLK_START : st == 0xFBu ? OM_CLK_CONTINUE : OM_CLK_STOP);
            if (st == 0xFAu)
                ui.armed = 0;
        } else if (st == 0x90u && v) {
            int s, best = 0, bd = 999;
            for (s = 0; s < OM_NSTR; s++) {
                int d = om_str_note[s] - (int)n;
                d = d < 0 ? -d : d;
                if (d < bd) {
                    bd = d;
                    best = s;
                }
            }
            om_strum(best);
        } else if (st == 0x91u || st == 0x81u) {
            if (st == 0x91u && v)
                kb_held[n >> 5] |= 1u << (n & 31u);
            else
                kb_held[n >> 5] &= ~(1u << (n & 31u));
            chords = 1;
        }
    }
    if (chords)
        kb_update();
}

static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), ch, i;
    int32_t e;
    ch = btn ^ ui.btn;
    ui.btn = btn;
    for (i = 0; i < NB; i++) {
        uint32_t m = 1u << i;
        if (!(ch & m))
            continue;
        if (btn & m) {
            ui.btn_used &= ~m;
            if (i == B_PLAY)                   /* transport on the press */
                button((int)i);
        } else if (!(ui.btn_used & m) && i != B_PLAY && i != B_ENV && i != B_LFO) {
            button((int)i);
        }
    }
    ch = keys ^ ui.keys;
    ui.keys = keys;
    for (i = 0; i < OM_NSTR; i++)              /* strings first: a strum is the fastest thing here */
        if ((ch & keys) >> WHITE_K[i] & 1u)
            om_strum((int)i);
    for (i = 0; i < NPADS; i++)
        if (ch >> BLACK_K[i] & 1u) {
            if (keys >> BLACK_K[i] & 1u)
                pad_down((int)i);
            else
                pad_up((int)i);
        }
    if (btn || keys || ch)
        ui.act_t = plat_ms();
    midi_in();
    seq_follow();
    if ((e = plat_enc(EN_SELECT)) != 0)
        turn(EN_SELECT, P_TEMPO, e);
    if ((e = plat_enc(EN_ALGO)) != 0)
        knob_set(P_RHYTHM, proj.par[P_RHYTHM] + (e > 0 ? 1 : -1));
    if ((e = plat_enc(EN_PRESET)) != 0)
        set_select(proj.set + (e > 0 ? 1 : -1));
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0) {
            turn(EN_K1 + (int)i, VIEW_KNOB[ui.view][i], e);
            ui.touched = (int8_t)i;
            ui.touch_until = plat_ms() + 1200u;
            ui.act_t = plat_ms();
        }
}

/* ------------------------------------------------------------- screen --- */
/* three bands: the header (28 px), the instrument (144), the knobs (68) */
#define HDR_H 28
#define MAIN_Y 28
#define MAIN_H 144
#define KNB_Y 172
#define KNB_H 68

static uint32_t hash(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

/* a rectangle with rounded corners (radius r, up to 6) */
static void cv_round(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    static const uint8_t IN[7][6] = {{0}, {1}, {1, 0}, {2, 1, 0}, {2, 1, 0, 0}, {3, 2, 1, 0, 0}, {4, 2, 1, 1, 0, 0}};
    int32_t j;
    if (r > 6)
        r = 6;
    if (r > h / 2)
        r = h / 2;
    cv_rect(x, y + r, w, h - 2 * r, c);
    for (j = 0; j < r; j++) {
        int32_t d = IN[r][j];
        cv_rect(x + d, y + j, w - 2 * d, 1, c);
        cv_rect(x + d, y + h - 1 - j, w - 2 * d, 1, c);
    }
}

static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}

static void draw_header(void)
{
    char t[24];
    int msg = plat_ms() < ui.msg_until;
    uint32_t h = hash(hash(hash(2166136261u, ui.view | proj.dark << 8), om_playing | ui.armed << 1 | proj.hold << 2 | proj.sync << 3 |
                                                       ui.dirty << 4 | (uint32_t)msg << 5 | proj.seq_on << 6 | proj.seq_trk << 8 |
                                                       om_seq_trk << 12),
                      (uint32_t)proj.par[P_RHYTHM] << 8 | (uint32_t)proj.par[P_TEMPO] | (uint32_t)om_ext << 20);
    if (msg)
        h = hash(hash(h, (uint32_t)ui.msg[0][0] << 8 | ui.msg[0][1]), ui.msg_until);
    if (om_playing)
        h = hash(h, om_step / 4u);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    if (msg) {
        int32_t x = cv_text(10, 6, &FONT_B, ui.msg[0], K_TEXT);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], K_HARP);
    } else {
        int32_t x = 10;
        cv_text(x, 6, &FONT_B, VIEW_NAME[ui.view], K_TEXT);
        x = 232;
        {   /* right: tempo, the rhythm, its state */
            if (om_ext) {                         /* following MIDI clock */
                t[0] = 'E';
                t[1] = 'X';
                t[2] = 'T';
                t[3] = 0;
            } else {
                itoa_u((uint32_t)proj.par[P_TEMPO], t);
            }
            x -= text_w(&FONT_S, t);
            cv_text(x, 6, &FONT_S, t, om_playing ? K_TEXT : K_DIM);
            x -= 8 + text_w(&FONT_S, om_rhythm(proj.par[P_RHYTHM])->name);
            cv_text(x, 6, &FONT_S, om_rhythm(proj.par[P_RHYTHM])->name, om_playing ? K_RHY : K_DIM);
            x -= 16;
            if (om_playing) {                     /* a beat light */
                int on = (om_step & 3u) < 2u;
                cv_round(x, 8, 10, 10, 5, on ? K_RHY : K_RHY_T);
            } else if (ui.armed) {
                cv_round(x, 8, 10, 10, 5, (ui.frame / 8u) & 1u ? K_RHY : K_RHY_T);
            } else {
                int32_t j;                        /* stopped: a play triangle */
                for (j = 0; j < 5; j++)
                    cv_rect(x + j, 9 + j, 1, 11 - 2 * j, K_DIM);
            }
        }
        x = 18 + text_w(&FONT_B, VIEW_NAME[ui.view]);
        if (proj.hold) {                          /* little tags */
            int32_t w = text_w(&FONT_XS, "HOLD") + 10;
            cv_round(x, 6, w, 16, 6, K_CHORD_T);
            cv_text(x + 5, 7, &FONT_XS, "HOLD", K_CHORD);
            x += w + 4;
        }
        if (proj.sync) {
            int32_t w = text_w(&FONT_XS, "SYNC") + 10;
            cv_round(x, 6, w, 16, 6, K_RHY_T);
            cv_text(x + 5, 7, &FONT_XS, "SYNC", K_RHY);
            x += w + 4;
        }
        if (proj.seq_on || ui.view == V_SEQ) {
            char n[8] = "OFF";                  /* SEQ and its track; another waiting to play: 1>3 */
            int32_t w;
            if (proj.seq_on) {
                char *q = n + 4;
                n[0] = 'S';
                n[1] = 'E';
                n[2] = 'Q';
                n[3] = ' ';
                if (seq_plays() && om_seq_trk != proj.seq_trk) {
                    *q++ = (char)('1' + om_seq_trk);
                    *q++ = '>';
                }
                *q++ = (char)('1' + proj.seq_trk);
                *q = 0;
            }
            w = text_w(&FONT_XS, n) + 10;
            cv_round(x, 6, w, 16, 6, proj.seq_on ? K_HARP_T : K_PANEL);
            cv_text(x + 5, 7, &FONT_XS, n, proj.seq_on ? K_HARP : K_DIM);
            x += w + 4;
        }
        if (ui.dirty)
            cv_round(x, 12, 5, 5, 2, K_DIM);
    }
    cv_blit(0, 0);
}

static uint16_t mix(uint16_t a, uint16_t b, int t)   /* t 0..16: a -> b */
{
    int r = ((a >> 11) * (16 - t) + (b >> 11) * t) / 16, g = (((a >> 5) & 63) * (16 - t) + ((b >> 5) & 63) * t) / 16,
        bl = ((a & 31) * (16 - t) + (b & 31) * t) / 16;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

static void draw_strings(int32_t y0, int32_t h)
{
    int s;
    for (s = 0; s < OM_NSTR; s++) {
        float lv = om_str_level[s];
        int t = (int)(lv * 16.0f + 0.5f), x = 9 + s * 14, w = 1 + (int)(lv * 4.0f + 0.3f);
        uint16_t c = mix(K_HARP_T, K_HARP, t > 16 ? 16 : t);
        int amp = (int)(lv * 3.0f), y;
        if (amp && (ui.frame & 1u))
            amp = -amp;
        for (y = 0; y < h; y++) {                  /* a string: bowed out in the middle while it rings */
            int d = (y < h / 2 ? y : h - 1 - y) * amp * 2 / h;
            cv_rect(x + 6 - w / 2 + d, y0 + y, w, 1, c);
        }
        if ((om_str_note[s] % 12) == (uint8_t)(((ui.root + proj.par[P_TRANSPOSE]) % 12 + 12) % 12))
            cv_round(x + 4, y0 + h + 3, 5, 5, 2, t > 2 ? K_HARP : K_LINE);   /* the roots: the plate's dots */
    }
}

static void draw_rhythm(int32_t y0, int32_t h)
{
    const om_rhythm_t *r = om_rhythm(proj.par[P_RHYTHM]);
    int len = r->len, s, d, cw = 224 / len;
    int32_t x0 = 120 - cw * len / 2, rowh = h / 6;
    static const char *const DN[5] = {"BD", "CL", "HH", "CY", "SD"};
    for (d = 0; d < 6; d++) {
        for (s = 0; s < len; s++) {
            int on, cur = om_playing && om_step == s;
            uint16_t c;
            if (d < 5) {
                on = r->drum[s] >> d & 1u;
                c = on ? (cur ? K_TEXT : K_RHY) : (s % 4 ? K_PANEL : K_RHY_T);
            } else {                               /* the bass line */
                on = r->bass[s] == 'T' || r->bass[s] == 'O';
                c = on ? (cur ? K_TEXT : K_CHORD) : (s % 4 ? K_PANEL : K_CHORD_T);
            }
            cv_round(x0 + s * cw, y0 + d * rowh, cw - 1, rowh - 2, 2, c);
        }
        (void)DN;
    }
}

/* the half of the track the page shows: the playing step's, if this track plays; the cursor's if not, and
 * for a moment after a knob changed something there */
static int seq_half(void)
{
    if (seq_plays() && om_seq_trk == proj.seq_trk && plat_ms() >= ui.seq_look)
        return om_seq_at >= 16;
    return ui.seq_sel >= 16;
}

/* the sequence: two rows of eight steps (half the track), each its chord and, small, its beats; the cursor
 * framed, the one playing filled */
static void draw_seq(int32_t y0)
{
    int len = seq_len(), i0 = seq_half() * 16, i, k;
    for (k = 0; k < 16; k++) {
        char t[12];
        int32_t x = 8 + (k % 8) * 28, y = y0 + (k / 8) * 32;
        int in = (i = i0 + k) < len, cur = in && seq_plays() && om_seq_trk == proj.seq_trk && om_seq_at == i;
        if (ui.seq_sel == i)
            cv_round(x - 1, y - 1, 29, 32, 6, K_TEXT);
        cv_round(x, y, 27, 30, 5, cur ? K_CHORD : in ? K_CHORD_T : K_PANEL);
        if (!in)
            continue;
        chord_name((int)(SQ_ROOT(i) + (uint32_t)seq_key()) % 12, 0, t);
        cv_text(x + 3, y + 2, &FONT_XS, t, cur ? K_WHITE : K_TEXT);
        itoa_u(SQ_BEATS(i), t);
        cv_text(x + 24 - text_w(&FONT_XS, t), y + 2, &FONT_XS, t, cur ? K_WHITE : K_DIM);
        text_c(x + 13, y + 15, &FONT_XS, OM_TYPE_NAME[SQ_TYPE(i)], cur ? K_WHITE : K_CHORD);
    }
}

static void draw_pads(int32_t y0)
{
    int p;
    for (p = 0; p < NPADS; p++) {
        char t[12];
        int32_t x = 4 + p * 20 + ((p >= 3) + (p >= 5) + (p >= 8) + (p >= 10)) * 3;   /* grouped as the keys are */
        int on = ui.pad == p, sel = ui.view == V_PADS && ui.pad_sel == p;
        if (sel)
            cv_round(x - 1, y0 - 1, 20, 30, 6, K_TEXT);
        cv_round(x, y0, 18, 28, 5, on ? K_CHORD : K_CHORD_T);
        chord_name(proj.pad_root[proj.set][p], 0, t);
        text_c(x + 9, y0 + 1, &FONT_XS, t, on ? K_WHITE : K_TEXT);
        text_c(x + 9, y0 + 13, &FONT_XS, OM_TYPE_NAME[proj.pad_type[proj.set][p]], on ? K_WHITE : K_CHORD);
    }
}

static void draw_main(void)
{
    char name[16], notes[24];
    uint32_t h = hash(2166136261u, ui.view | proj.dark << 8), s;
    for (s = 0; s < OM_NSTR; s++)
        h = hash(h, (uint32_t)(om_str_level[s] * 24.0f) | (uint32_t)om_str_note[s] << 8);
    h = hash(hash(h, (uint32_t)ui.pad | ui.pad_sel << 8 | ui.root << 16 | ui.type << 24),
             (uint32_t)proj.par[P_TRANSPOSE] << 8 | (uint32_t)(om_chord_level * 8.0f));
    for (s = 0; s < NPADS; s++)
        h = hash(h, proj.pad_root[proj.set][s] | proj.pad_type[proj.set][s] << 4);
    h = hash(h, proj.set | proj.plate << 8);
    if (ui.view == V_RHYTHM)
        h = hash(hash(h, om_playing ? om_step : 255u), (uint32_t)proj.par[P_RHYTHM]);
    if (ui.view == V_SEQ) {
        for (s = 0; s < NSEQ; s++)
            h = hash(h, SQ[s][0] | SQ[s][1] << 8);
        h = hash(h, ui.seq_sel | (seq_plays() ? om_seq_at : 255u) << 8 | proj.seq_trk << 16 | om_seq_trk << 20 |
                        (uint32_t)seq_half() << 24);
    }
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    chord_name(ui.root, ui.type, name);
    {   /* the chord: its name, big, and its notes */
        uint8_t hp[OM_NSTR], cn[OM_NCHORD], bn;
        int i, k = 0;
        uint16_t c = mix(K_TEXT, K_CHORD, (int)(om_chord_level * 16.0f));
        int32_t x = 12;
        const char *tn = OM_TYPE_NAME[ui.type];
        name[str_len_(name) - str_len_(tn)] = 0;        /* the root, big; the type beside it */
        x = cv_text(x, 4, &FONT_L, name, c);
        cv_text(x + 2, 16, &FONT_B, tn, c);
        om_voicing(ui.root, ui.type, proj.par[P_TRANSPOSE], proj.par[P_OCTAVE], hp, cn, &bn);
        for (i = 0; i < 3; i++) {
            const char *n = OM_NOTE_NAME[cn[i] % 12];
            if (i)
                notes[k++] = ' ';
            while (*n)
                notes[k++] = *n++;
        }
        notes[k] = 0;
        cv_text(240 - 12 - text_w(&FONT_S, notes), 14, &FONT_S, notes, K_DIM);
        notes[0] = 'S';                                 /* the chord set, small, above them */
        notes[1] = 'E';
        notes[2] = 'T';
        notes[3] = ' ';
        itoa_u(proj.set + 1u, notes + 4);
        x = 240 - 12 - text_w(&FONT_XS, notes);
        cv_text(x, 1, &FONT_XS, notes, K_CHORD);
        if (ui.view == V_SEQ) {                         /* the track, and which of its steps show */
            char m[16] = "TRACK 1  1-16";
            m[6] = (char)('1' + proj.seq_trk);
            if (seq_half()) {
                m[9] = '1';
                m[10] = '7';
                m[11] = '-';
                m[12] = '3';
                m[13] = '2';
                m[14] = 0;
            }
            cv_text(x - 6 - text_w(&FONT_XS, m), 1, &FONT_XS, m, K_HARP);
        } else if (proj.plate) {
            const char *m = proj.plate == PL_CHORD ? "REL SCALE" : proj.plate == PL_LINEAR ? "LINEAR" : "SCALE";
            cv_text(x - 6 - text_w(&FONT_XS, m), 1, &FONT_XS, m, K_HARP);
        }
    }
    if (ui.view == V_RHYTHM)
        draw_rhythm(44, 62);
    else if (ui.view == V_SEQ)
        draw_seq(45);
    else
        draw_strings(44, 54);
    draw_pads(112);
    cv_blit(0, MAIN_Y);
}

static void draw_knobs(void)
{
    uint32_t h = hash(2166136261u, ui.view | (uint32_t)ui.touched << 8 | ui.pad_sel << 16 | (uint32_t)proj.dark << 24),
             i;
    h = hash(h, ui.seq_sel | (uint32_t)seq_len() << 8);
    int now_touch = plat_ms() < ui.touch_until;
    for (i = 0; i < 4; i++)
        h = hash(h, (uint32_t)knob_get(VIEW_KNOB[ui.view][i]));
    h = hash(h, (uint32_t)now_touch);
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, KNB_H, K_BG);
    cv_round(4, 2, 232, KNB_H - 6, 6, K_PANEL);
    for (i = 0; i < 4; i++) {
        int k = VIEW_KNOB[ui.view][i], lo, hi, v;
        int32_t x = (int32_t)i * 60, cx = x + 30, bw;
        int hot = now_touch && ui.touched == (int)i;
        char t[16];
        if (k == K_NONE)
            continue;
        if (i)
            cv_rect(x, 14, 1, KNB_H - 30, K_LINE);
        knob_range(k, &lo, &hi);
        v = knob_get(k);
        text_c(cx, 6, &FONT_XS, knob_name(k), hot ? K_TEXT : K_DIM);
        knob_text(k, t);
        {   /* the value: big, unless it has lower case (the big face has none) or is long */
            const char *q = t;
            int lower = 0;
            for (; *q; q++)
                lower |= *q >= 'a' && *q <= 'z';
            if (lower || text_w(&FONT_M, t) > 56)
                text_c(cx, 26, &FONT_B, t, hot ? K_HARP : K_TEXT);
            else
                text_c(cx, 22, &FONT_M, t, hot ? K_HARP : K_TEXT);
        }
        bw = hi > lo ? (int32_t)(44 * (v - lo) / (hi - lo)) : 0;
        cv_round(cx - 22, 50, 44, 6, 3, K_LINE);
        if (lo < 0) {                                /* centred values: a bar from the middle */
            int32_t m = cx - 22 + 44 * (0 - lo) / (hi - lo), e = cx - 22 + bw;
            cv_round(e < m ? e : m, 50, (e < m ? m - e : e - m) + 6, 6, 3, hot ? K_HARP : K_CHORD);
        } else if (bw) {
            cv_round(cx - 22, 50, bw < 6 ? 6 : bw, 6, 3, hot ? K_HARP : K_CHORD);
        }
    }
    cv_blit(0, KNB_Y);
}

static void leds(void)
{
    uint32_t b = 0, k = 0, i;
    static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_SEQ, B_FX, B_SEL, B_GLO, B_GLO, B_GLO, B_REC};
    b |= 1u << VIEW_BTN[ui.view];
    if (om_playing)
        b |= (om_step & 3u) < 2u ? 1u << B_PLAY : 0u;
    else if (ui.armed && (ui.frame / 8u) & 1u)
        b |= 1u << B_PLAY;
    if (proj.seq_on)
        b |= 1u << B_REC;
    if (proj.hold)
        b |= 1u << B_ARP;
    if (proj.leds) {
        if (ui.pad >= 0)
            k |= 1u << BLACK_K[ui.pad];
        for (i = 0; i < OM_NSTR; i++)
            if (om_str_level[i] > 0.2f)
                k |= 1u << WHITE_K[i];
    }
    plat_glow(proj.leds == 1);
    plat_leds(b, k);
}

/* AUTOSAVE: a few seconds after a change, when nothing sounds and nothing is touched (a flash erase
 * silences the audio for a moment) */
#define AUTOSAVE_QUIET 4000u
static void autosave(void)
{
    uint32_t now = plat_ms(), i;
    if (!ui.dirty || om_playing || ui.btn || ui.keys || now - ui.act_t < AUTOSAVE_QUIET || om_chord_level > 0.0f ||
        om_bass_level > 0.0f)
        return;
    for (i = 0; i < OM_NSTR; i++)
        if (om_str_level[i] > 0.0f)
            return;
    if (project_save() == 0) {
        ui.dirty = 0;
        ui.saved_t = now;
    }
}

void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.pad = -1;
    ui.touched = -1;
    ui.root = proj.pad_root[proj.set][1];        /* the second button: C, as it comes */
    ui.type = proj.pad_type[proj.set][1];
    sound_chord();
    lcd_fill(0, 0, 240, 240, K_BG);
}

void ui_frame(void)
{
    input();
    autosave();
    draw_header();
    draw_main();
    draw_knobs();
    leds();
    ui.frame++;
}

void ui_input_only(void) { input(); }
