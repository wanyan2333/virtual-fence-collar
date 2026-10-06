/*
 * geofence.h - Polygon virtual fence in local metres.
 *
 * The fence is given as lat/lon vertices. On init they are projected to a
 * flat local frame (x = metres east, y = metres north) around the first
 * vertex using an equirectangular projection. For a paddock a few km wide
 * the error of this projection is far below GPS noise, and it is cheap
 * (one cos() at init, multiplies afterwards).
 */
#ifndef GEOFENCE_H
#define GEOFENCE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GEOFENCE_MAX_VERTICES 16
#define GEOFENCE_EARTH_RADIUS_M 6371000.0
/* Points closer than this to an edge count as "on the boundary" (inside). */
#define GEOFENCE_EDGE_EPS_M 0.01f

typedef struct {
    double lat_deg;
    double lon_deg;
} geo_point_t;

typedef struct {
    float x; /* metres east of origin  */
    float y; /* metres north of origin */
} local_point_t;

typedef struct {
    geo_point_t origin;       /* first vertex                         */
    double m_per_deg_lat;     /* metres per degree of latitude        */
    double m_per_deg_lon;     /* metres per degree of longitude here  */
    uint8_t count;            /* number of vertices in use            */
    local_point_t v[GEOFENCE_MAX_VERTICES];
} geofence_t;

/* Returns false (and leaves the fence unusable) if count is < 3, larger
 * than GEOFENCE_MAX_VERTICES, or the polygon has ~zero area. */
bool geofence_init(geofence_t *f, const geo_point_t *vertices, uint8_t count);

/* Project a lat/lon into the fence's local metre frame. */
local_point_t geofence_to_local(const geofence_t *f, geo_point_t p);

/* Ray-casting point-in-polygon. Points on an edge or vertex are inside. */
bool geofence_contains(const geofence_t *f, local_point_t p);

/* Shortest distance (metres) from p to any fence edge (always >= 0). */
float geofence_distance_to_edge(const geofence_t *f, local_point_t p);

#ifdef __cplusplus
}
#endif

#endif /* GEOFENCE_H */
