/*
 * geofence.c - see geofence.h.
 */
#include "geofence.h"

#include <math.h>

#define DEG_TO_RAD (3.14159265358979323846 / 180.0)

bool geofence_init(geofence_t *f, const geo_point_t *vertices, uint8_t count)
{
    uint8_t i;
    float area2 = 0.0f; /* twice the signed area (shoelace formula) */

    if (f == 0 || vertices == 0) return false;
    f->count = 0;
    if (count < 3 || count > GEOFENCE_MAX_VERTICES) return false;

    f->origin = vertices[0];
    /* Equirectangular: 1 deg of latitude is constant, 1 deg of longitude
     * shrinks with cos(latitude). Use the origin latitude for the whole fence. */
    f->m_per_deg_lat = GEOFENCE_EARTH_RADIUS_M * DEG_TO_RAD;
    f->m_per_deg_lon = f->m_per_deg_lat * cos(f->origin.lat_deg * DEG_TO_RAD);

    f->count = count; /* set before calling geofence_to_local helpers */
    for (i = 0; i < count; i++) {
        f->v[i] = geofence_to_local(f, vertices[i]);
    }
    for (i = 0; i < count; i++) {
        const local_point_t a = f->v[i];
        const local_point_t b = f->v[(i + 1u) % count];
        area2 += a.x * b.y - b.x * a.y;
    }
    if (fabsf(area2) < 2.0f) { /* less than 1 m^2: degenerate fence */
        f->count = 0;
        return false;
    }
    return true;
}

local_point_t geofence_to_local(const geofence_t *f, geo_point_t p)
{
    local_point_t out;
    /* Do the subtraction in double (lat/lon need ~1e-7 deg precision),
     * then store metres as float: plenty for a paddock, and the ESP32 has
     * a single-precision FPU, so later maths on floats is fast. */
    out.x = (float)((p.lon_deg - f->origin.lon_deg) * f->m_per_deg_lon);
    out.y = (float)((p.lat_deg - f->origin.lat_deg) * f->m_per_deg_lat);
    return out;
}

/* Distance from p to the segment a-b. */
static float dist_to_segment(local_point_t p, local_point_t a, local_point_t b)
{
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float apx = p.x - a.x;
    const float apy = p.y - a.y;
    const float len2 = abx * abx + aby * aby;
    float t = 0.0f;
    float dx, dy;

    if (len2 > 0.0f) {
        /* Project p onto the line, clamp to the segment [0, 1]. */
        t = (apx * abx + apy * aby) / len2;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    }
    dx = apx - t * abx;
    dy = apy - t * aby;
    return sqrtf(dx * dx + dy * dy);
}

float geofence_distance_to_edge(const geofence_t *f, local_point_t p)
{
    uint8_t i;
    float best = INFINITY;

    for (i = 0; i < f->count; i++) {
        const float d = dist_to_segment(p, f->v[i], f->v[(i + 1u) % f->count]);
        if (d < best) best = d;
    }
    return best;
}

bool geofence_contains(const geofence_t *f, local_point_t p)
{
    uint8_t i, j;
    bool inside = false;

    if (f->count < 3) return false;

    /* Ray casting has ambiguous results exactly on the boundary, so decide
     * that case explicitly: on an edge or vertex counts as inside. */
    if (geofence_distance_to_edge(f, p) <= GEOFENCE_EDGE_EPS_M) return true;

    /* Cast a ray from p towards +x and count edge crossings; odd = inside.
     * The (a.y > p.y) != (b.y > p.y) test treats each vertex as belonging to
     * only one of its two edges, so a ray through a vertex counts once. */
    for (i = 0, j = (uint8_t)(f->count - 1u); i < f->count; j = i++) {
        const local_point_t a = f->v[i];
        const local_point_t b = f->v[j];
        if ((a.y > p.y) != (b.y > p.y)) {
            const float x_cross = a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y);
            if (p.x < x_cross) inside = !inside;
        }
    }
    return inside;
}
