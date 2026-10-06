/*
 * fence_config.h - The demo paddock: an L-shaped (concave) polygon near
 * Hamilton, NZ. Local metres around the first vertex are in the comments.
 *
 * tools/gen_track.py reads this file, so it is the single source of truth.
 */
#ifndef FENCE_CONFIG_H
#define FENCE_CONFIG_H

#include "geofence.h"

static const geo_point_t FENCE_VERTICES[] = {
    { -37.7870000, 175.2790000 }, /* (0, 0) m     */
    { -37.7870000, 175.2812759 }, /* (200, 0) m   */
    { -37.7862805, 175.2812759 }, /* (200, 80) m  */
    { -37.7862805, 175.2803655 }, /* (120, 80) m  */
    { -37.7856510, 175.2803655 }, /* (120, 150) m */
    { -37.7856510, 175.2790000 }, /* (0, 150) m   */
};

#define FENCE_VERTEX_COUNT ((uint8_t)(sizeof(FENCE_VERTICES) / sizeof(FENCE_VERTICES[0])))

#endif /* FENCE_CONFIG_H */
