#ifndef LRRY_RETIRED_H
#define LRRY_RETIRED_H
/* L/RRY: checks of DUELL's TWO-PART features (LAYER, SPLIT, DUEL, part 2, both parts, the A/B project formats, the effects pages), retired in phase 4.1 when the firmware went to ONE part and
 * lost its MODE. A retired check is printed as RETIRED and does not count as a failure. This list is the to-delete list: when the feature it checked is gone for good the line goes too
 * (tests follow the firmware). A label is retired when it STARTS with one of these. */
#include <string.h>
static const char *const LRRY_RETIRED[] = {
    /* mod_test */ "LAYER: part 2's LFO 1", "LAYER: part 2's LFO 2", "SPLIT: part 2 stays", "LAYER: LFO 1 -> PAN", "TRS: ROUT CH1-4: ch 5", "  the MATRIX page's DEST line names every destination",
    "bus values: STEREO / LAYER", "DUEL: part 1's routes", "cost: 4 active slots add",
    /* chord_test (ui_test.c) */ "SNAP, LAYER:", "SNAP: a MIDI note follows the same path", "PLAY: the key plays C# on both parts", "DIA3 in C major stacks", "CHROMATIC: DIA3 plays", "a FIXED shape keeps its intervals",
    /* step3_test */ "FUN11 round trip: DUELL's values on the reused ids", "  restored onto the device: parts A B", "backup: settings with a MIDI map", "cost: everything active stays within",
    0};
static int lrry_retired_n;
static int lrry_retired(const char *what)
{
    int i;
    for (i = 0; LRRY_RETIRED[i]; i++)
        if (!strncmp(what, LRRY_RETIRED[i], strlen(LRRY_RETIRED[i]))) {
            lrry_retired_n++;
            return 1;
        }
    return 0;
}
#endif
