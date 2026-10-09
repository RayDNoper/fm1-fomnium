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

typedef struct {
    uint32_t magic, format;
    int16_t par[P_NPARAMS];              /* omni.h P_* */
    uint8_t pad_root[NSETS][NPADS], pad_type[NSETS][NPADS];
    uint8_t hold;                        /* CHORD HOLD: the chord keeps playing after its button is let go */
    uint8_t sync;                        /* SYNC START: the rhythm starts with the first chord */
    uint8_t leds;                        /* lights: 0 off, 1 keys and the buttons' glow, 2 keys only */
    uint8_t set;                         /* the chord set in use */
    uint8_t plate;                       /* the strum plate: PL_* (the chord's tones, a fixed scale, the chord's) */
    uint8_t rsv[11];                     /* room to grow: an older, shorter project loads (zeros here) */
} project_t;

extern project_t proj;

void project_defaults(void);
int project_load(void);                  /* 0 = loaded; else defaults are in place */
int project_save(void);                  /* 0 = saved and verified */
void project_apply(void);                /* every value to the engine (boot, load) */

void ui_init(void);
void ui_frame(void);
void ui_input_only(void);
int ui_dirty(void);
void ui_say(const char *a, const char *b);
