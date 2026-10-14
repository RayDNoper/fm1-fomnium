/* SPDX-License-Identifier: GPL-3.0-only */
/* OMNI app: the project (everything that is saved) and the app's entry points. Included by the unity
 * build after gfx.c (drawing helpers in scope) and by the host simulator. */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "../dsp/omni.h"

#ifndef OM_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define OM_VERSION "DEV"
#endif
#define BANK_HALF (OM_NDX / 2 * OM_DX_BYTES)
#define PROJ_MAGIC 0x494E4D4Fu           /* "OMNI" */
#define PROJ_FORMAT 4u                   /* 4: the voices are the harp, the organ and the DX7 bank's; 3: the chord
                                          * sets; 2: Lo-fi's slot became Chord rev (0.2). 1 to 3 load */
#define NPADS 11                         /* the black keys: chord buttons */
#define NSETS 8                          /* chord sets: what the buttons play, picked with PRESETS */
#define NTRK OM_NTRK                     /* the chord sequence's tracks, */
#define NSEQ OM_NSEQ                     /* and the steps of each */

typedef struct {
    uint32_t magic, format;
    int16_t par[P_NPARAMS];              /* omni.h P_* */
    uint8_t pad_root[NSETS][NPADS], pad_type[NSETS][NPADS];
    uint8_t hold;                        /* CHORD HOLD: the chord keeps playing after its button is let go */
    uint8_t sync;                        /* SYNC START: the rhythm starts with the first chord */
    uint8_t leds;                        /* lights: 0 off, 1 keys and the buttons' glow, 2 keys only */
    uint8_t set;                         /* the chord set in use */
    uint8_t plate;                       /* the strum plate: PL_* (the chord's tones, a fixed scale, the chord's) */
    uint8_t dark;                        /* the screen: 0 light, 1 dark */
    uint8_t voice;                       /* the strings' voice: 0 the harp, 1.. a patch of the DX7 bank */
    uint8_t cvoice;                      /* the chord's voice: 0 the organ, 1.. a patch of the DX7 bank */
    int8_t shift;                        /* Relative strings: the scale moved along the keys, -6..6 steps */
    uint8_t seq_on;                      /* the chord sequence plays with the rhythm (REC) */
    uint8_t seq_trk;                     /* its track in use */
    int8_t bass;                         /* the bass's level against the chord, -50 (none) .. 0 (as it comes) .. 50 */
    uint8_t mel;                         /* the melody: 0 off, its level 1..100 (om_melody) */
    uint8_t mel_den, mel_seed, mel_rep;  /* its Density, Seed (1..99; 0: a project from before it) and Repeat */
    /* the chord sequence's tracks: each step a chord (root | type << 4) and its beats (| INV_* << 6), 0: the
     * track ends before it. The roots are counted from the chord set's key (seq_key), so another set (PRESETS) plays
     * the sequence in its key */
    uint8_t seq[NTRK][NSEQ][2];
    uint8_t pad_inv[NSETS][NPADS];       /* how each button's chord lies: INV_* (a step: its beats' top bits) */
    uint8_t rhy[OM_NURHY][OM_RHY_BYTES]; /* the player's own rhythms (omni.h; the editor writes them) */
} project_t;

extern project_t proj;
/* a DX7 bank's patches, as its SysEx holds them (omni.h): voices for the strings and the chord after those
 * Jambox comes with. Not in the project: two flash objects of their own, half each (the editor writes them) */
extern uint8_t dx_bank[OM_NDX][OM_DX_BYTES];
void bank_touch(void);                   /* the bank changed: project_save() saves it too */

void project_defaults(void);
int project_load(void);                  /* 0 = loaded; else defaults are in place */
int project_save(void);                  /* 0 = saved and verified */
void project_apply(void);                /* every value to the engine (boot, load) */
int seq_key(void);                       /* the chord set's key: the root of its second button (C, as it comes) */

void ui_init(void);
void ui_frame(void);
void ui_input_only(void);
int ui_dirty(void);
void ui_say(const char *a, const char *b);
void ed_service(void);                   /* the editor's requests (SysEx): answer the one that waits */
