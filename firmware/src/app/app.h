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
#define PROJ_MAGIC 0x494E4D4Fu           /* "OMNI" */
#define PROJ_FORMAT 3u                   /* 3: the chord sets; 2: Lo-fi's slot became Chord rev (0.2). 1 and 2 load */
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
    uint8_t voice;                       /* the strings' voice: 0 the harp, 1.. an FM sound (om_voice_name) */
    uint8_t cvoice;                      /* the chord's voice: 0 the organ, 1.. an FM sound (om_cvoice_name) */
    int8_t shift;                        /* Relative strings: the scale moved along the keys, -6..6 steps */
    uint8_t seq_on;                      /* the chord sequence plays with the rhythm (REC) */
    uint8_t seq_trk;                     /* its track in use */
    uint8_t rsv[5];                      /* room to grow: an older, shorter project loads (zeros here) */
    /* the chord sequence's tracks: each step a chord (root | type << 4) and its beats, 0: the track ends
     * before it. The roots are counted from the chord set's key (seq_key), so another set (PRESETS) plays
     * the sequence in its key */
    uint8_t seq[NTRK][NSEQ][2];
} project_t;

extern project_t proj;

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
