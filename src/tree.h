/* Window classifier: a decision tree trained by tools/train_tree.py and
 * exported to src/tree.c (integer comparisons only). */
#ifndef TREE_H
#define TREE_H
#include "mcu.h"
#include "config.h"
#include "fusion.h"

/* Integer features of one window, for the zone z that felt it first.
 * Field order matches FEATURES in tools/train_tree.py. */
typedef struct {
    uint16_t hits_z;      /* hits on zone z */
    uint16_t hits_max;    /* most hits on any zone */
    uint16_t zones_hit;   /* zones with at least one hit */
    uint16_t peak_z;      /* peak |deviation| on z */
    uint16_t snr_z;       /* peak x 4 / (background + 1), max 255 */
    uint16_t ms_per_hit;  /* time inside events per hit on z */
    uint16_t gap_min;     /* shortest / longest gap between steps on z, ms (0 if none) */
    uint16_t gap_max;
    uint16_t irregular;   /* gap_max x 16 / gap_min (16 = steady), 0 below 3 hits */
    uint16_t spread_ms;   /* first onset on the last zone minus on the first, max 2000 */
} tree_feat_t;

extern __idata tree_feat_t tree_f;        /* the features tree_predict reads */

void    tree_features(const window_t __idata *w, uint8_t z);   /* fills tree_f */
uint8_t tree_predict(void);                /* classifies tree_f; returns an EV_* code */

#endif
