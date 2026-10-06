#include <math.h>

#include "geofence.h"
#include "unity.h"

#define ORIGIN_LAT (-37.787)
#define ORIGIN_LON (175.279)

static geofence_t fence;

void setUp(void) {}
void tearDown(void) {}

/* Inverse of the equirectangular projection, so tests can describe
 * polygons in easy-to-read metres. */
static geo_point_t from_local(float x, float y)
{
    const double m_per_deg_lat = GEOFENCE_EARTH_RADIUS_M * 3.14159265358979323846 / 180.0;
    const double m_per_deg_lon = m_per_deg_lat * cos(ORIGIN_LAT * 3.14159265358979323846 / 180.0);
    geo_point_t g;
    g.lat_deg = ORIGIN_LAT + y / m_per_deg_lat;
    g.lon_deg = ORIGIN_LON + x / m_per_deg_lon;
    return g;
}

static local_point_t P(float x, float y)
{
    local_point_t p;
    p.x = x;
    p.y = y;
    return p;
}

static void make_square(void) /* 100 m x 100 m */
{
    geo_point_t v[4];
    v[0] = from_local(0, 0);
    v[1] = from_local(100, 0);
    v[2] = from_local(100, 100);
    v[3] = from_local(0, 100);
    TEST_ASSERT_TRUE(geofence_init(&fence, v, 4));
}

/* L-shaped (concave) paddock, same shape as the app's demo fence. */
static void make_L(void)
{
    geo_point_t v[6];
    v[0] = from_local(0, 0);
    v[1] = from_local(200, 0);
    v[2] = from_local(200, 80);
    v[3] = from_local(120, 80);
    v[4] = from_local(120, 150);
    v[5] = from_local(0, 150);
    TEST_ASSERT_TRUE(geofence_init(&fence, v, 6));
}

static void test_projection_round_trip(void)
{
    local_point_t p;
    make_square();
    p = geofence_to_local(&fence, from_local(37.5f, -12.25f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 37.5f, p.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -12.25f, p.y);
    /* 0.001 deg of latitude is ~111.2 m anywhere on Earth */
    p = geofence_to_local(&fence, (geo_point_t){ ORIGIN_LAT + 0.001, ORIGIN_LON });
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 111.2f, p.y);
}

static void test_square_inside_and_outside(void)
{
    make_square();
    TEST_ASSERT_TRUE(geofence_contains(&fence, P(50, 50)));
    TEST_ASSERT_TRUE(geofence_contains(&fence, P(1, 99)));
    TEST_ASSERT_FALSE(geofence_contains(&fence, P(150, 50)));
    TEST_ASSERT_FALSE(geofence_contains(&fence, P(-0.5f, 50)));
    TEST_ASSERT_FALSE(geofence_contains(&fence, P(50, 100.5f)));
}

static void test_on_vertex_counts_as_inside(void)
{
    make_square();
    TEST_ASSERT_TRUE(geofence_contains(&fence, P(0, 0)));
    TEST_ASSERT_TRUE(geofence_contains(&fence, P(100, 100)));
}

static void test_on_edge_counts_as_inside(void)
{
    make_square();
    TEST_ASSERT_TRUE(geofence_contains(&fence, P(50, 0)));
    TEST_ASSERT_TRUE(geofence_contains(&fence, P(100, 30)));
}

static void test_concave_polygon_notch_is_outside(void)
{
    make_L();
    TEST_ASSERT_TRUE(geofence_contains(&fence, P(60, 120)));   /* top arm    */
    TEST_ASSERT_TRUE(geofence_contains(&fence, P(180, 40)));   /* right arm  */
    TEST_ASSERT_FALSE(geofence_contains(&fence, P(160, 120))); /* the notch  */
    TEST_ASSERT_FALSE(geofence_contains(&fence, P(121, 81)));  /* just in notch */
    TEST_ASSERT_TRUE(geofence_contains(&fence, P(119, 81)));
}

static void test_ray_through_vertex_row(void)
{
    /* A horizontal ray at y = 80 runs exactly along the edge (120,80)-(200,80)
     * and through two vertices; it must still give the right answer. */
    make_L();
    TEST_ASSERT_FALSE(geofence_contains(&fence, P(-10, 80)));
    TEST_ASSERT_TRUE(geofence_contains(&fence, P(60, 80)));
    TEST_ASSERT_TRUE(geofence_contains(&fence, P(150, 80))); /* on edge */
    TEST_ASSERT_FALSE(geofence_contains(&fence, P(250, 80)));
}

static void test_distance_to_edge(void)
{
    make_square();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, geofence_distance_to_edge(&fence, P(50, 50)));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f, geofence_distance_to_edge(&fence, P(97, 40)));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, geofence_distance_to_edge(&fence, P(110, 50)));
    /* outside near a corner: distance is to the vertex, not the line */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, sqrtf(200.0f), geofence_distance_to_edge(&fence, P(110, 110)));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, geofence_distance_to_edge(&fence, P(100, 0)));
}

static void test_distance_in_concave_corner(void)
{
    make_L();
    /* (130, 90) is outside, in the notch, 10 m from both inner edges */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, geofence_distance_to_edge(&fence, P(130, 90)));
}

static void test_init_rejects_bad_polygons(void)
{
    geo_point_t v[GEOFENCE_MAX_VERTICES + 1];
    int i;
    for (i = 0; i < GEOFENCE_MAX_VERTICES + 1; i++) v[i] = from_local((float)i, (float)(i * i));
    TEST_ASSERT_FALSE(geofence_init(&fence, v, 2));
    TEST_ASSERT_FALSE(geofence_init(&fence, v, GEOFENCE_MAX_VERTICES + 1));
    TEST_ASSERT_FALSE(geofence_init(&fence, NULL, 4));
    /* three points on a line: zero area */
    v[0] = from_local(0, 0);
    v[1] = from_local(10, 10);
    v[2] = from_local(20, 20);
    TEST_ASSERT_FALSE(geofence_init(&fence, v, 3));
    TEST_ASSERT_FALSE(geofence_contains(&fence, P(10, 10)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_projection_round_trip);
    RUN_TEST(test_square_inside_and_outside);
    RUN_TEST(test_on_vertex_counts_as_inside);
    RUN_TEST(test_on_edge_counts_as_inside);
    RUN_TEST(test_concave_polygon_notch_is_outside);
    RUN_TEST(test_ray_through_vertex_row);
    RUN_TEST(test_distance_to_edge);
    RUN_TEST(test_distance_in_concave_corner);
    RUN_TEST(test_init_rejects_bad_polygons);
    return UNITY_END();
}
