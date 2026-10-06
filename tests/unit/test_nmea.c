#include <stdio.h>
#include <string.h>

#include "nmea.h"
#include "unity.h"

/* Classic example sentences from the NMEA reference (checksums are real). */
static const char *GGA_OK = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
static const char *RMC_OK = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";

static nmea_fix_t fix;
static nmea_stats_t stats;

void setUp(void)
{
    memset(&fix, 0, sizeof fix);
    memset(&stats, 0, sizeof stats);
}

void tearDown(void) {}

/* Build "$<body>*HH" with a correct checksum, so tests can focus on fields. */
static const char *sentence(const char *body)
{
    static char line[128];
    unsigned sum = 0;
    const char *p;
    for (p = body; *p; p++) sum ^= (unsigned char)*p;
    snprintf(line, sizeof line, "$%s*%02X", body, sum);
    return line;
}

static void test_checksum_valid_examples(void)
{
    TEST_ASSERT_TRUE(nmea_checksum_ok(GGA_OK));
    TEST_ASSERT_TRUE(nmea_checksum_ok(RMC_OK));
}

static void test_checksum_detects_single_char_change(void)
{
    char line[100];
    strcpy(line, GGA_OK);
    line[20] = '9'; /* corrupt one digit of the latitude */
    TEST_ASSERT_FALSE(nmea_checksum_ok(line));
    TEST_ASSERT_EQUAL(NMEA_ERR_CHECKSUM, nmea_parse(line, &fix, &stats));
    TEST_ASSERT_EQUAL_UINT32(1, stats.rejected);
    TEST_ASSERT_EQUAL_UINT32(0, stats.accepted);
}

static void test_checksum_lowercase_hex_accepted(void)
{
    TEST_ASSERT_TRUE(nmea_checksum_ok("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6a"));
}

static void test_parse_gga(void)
{
    TEST_ASSERT_EQUAL(NMEA_OK, nmea_parse(GGA_OK, &fix, &stats));
    TEST_ASSERT_EQUAL(NMEA_SENTENCE_GGA, fix.type);
    TEST_ASSERT_TRUE(fix.fix_valid);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 48.0 + 7.038 / 60.0, fix.lat_deg);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 11.0 + 31.0 / 60.0, fix.lon_deg);
    TEST_ASSERT_EQUAL_UINT8(8, fix.satellites);
    TEST_ASSERT_EQUAL_UINT32(123519, fix.utc_hhmmss);
    TEST_ASSERT_EQUAL_UINT32(1, stats.accepted);
}

static void test_parse_rmc_speed_in_mps(void)
{
    TEST_ASSERT_EQUAL(NMEA_OK, nmea_parse(RMC_OK, &fix, &stats));
    TEST_ASSERT_EQUAL(NMEA_SENTENCE_RMC, fix.type);
    TEST_ASSERT_TRUE(fix.fix_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.4f * 0.514444f, fix.speed_mps);
}

static void test_south_and_west_are_negative(void)
{
    const char *l = sentence("GPRMC,010203,A,3747.22000,S,17516.74000,W,0.0,0.0,071026,,,A");
    TEST_ASSERT_EQUAL(NMEA_OK, nmea_parse(l, &fix, NULL));
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -(37.0 + 47.22 / 60.0), fix.lat_deg);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -(175.0 + 16.74 / 60.0), fix.lon_deg);
}

static void test_gn_talker_and_crlf_accepted(void)
{
    char line[100];
    snprintf(line, sizeof line, "%s\r\n",
             sentence("GNRMC,010203,A,3747.22000,S,17516.74000,E,0.5,0.0,071026,,,A"));
    TEST_ASSERT_EQUAL(NMEA_OK, nmea_parse(line, &fix, NULL));
    TEST_ASSERT_TRUE(fix.fix_valid);
}

static void test_rmc_status_void_is_ok_but_no_fix(void)
{
    TEST_ASSERT_EQUAL(NMEA_OK, nmea_parse(sentence("GPRMC,010203,V,,,,,,,071026,,,N"), &fix, &stats));
    TEST_ASSERT_FALSE(fix.fix_valid);
    TEST_ASSERT_EQUAL_UINT32(0, stats.rejected);
}

static void test_gga_quality_zero_is_no_fix(void)
{
    TEST_ASSERT_EQUAL(NMEA_OK, nmea_parse(sentence("GPGGA,010203,,,,,0,00,,,M,,M,,"), &fix, NULL));
    TEST_ASSERT_FALSE(fix.fix_valid);
}

static void test_malformed_lines_rejected(void)
{
    /* framing problems */
    TEST_ASSERT_EQUAL(NMEA_ERR_FORMAT, nmea_parse("", &fix, &stats));
    TEST_ASSERT_EQUAL(NMEA_ERR_FORMAT, nmea_parse("GPGGA,123519*47", &fix, &stats));      /* no '$' */
    TEST_ASSERT_EQUAL(NMEA_ERR_FORMAT, nmea_parse("$GPGGA,123519,4807.038", &fix, &stats)); /* no '*' */
    TEST_ASSERT_EQUAL(NMEA_ERR_FORMAT, nmea_parse("$GPGGA,123519*4", &fix, &stats));      /* 1 hex */
    TEST_ASSERT_EQUAL(NMEA_ERR_FORMAT, nmea_parse("$GPGGA,123519*G7", &fix, &stats));     /* not hex */
    TEST_ASSERT_EQUAL(NMEA_ERR_FORMAT, nmea_parse(NULL, &fix, &stats));
    TEST_ASSERT_EQUAL_UINT32(6, stats.rejected);
}

static void test_bad_fields_rejected(void)
{
    /* valid checksums, but broken content */
    TEST_ASSERT_EQUAL(NMEA_ERR_FORMAT, nmea_parse(sentence("GPRMC,010203,A,,,,,,,071026"), &fix, NULL));
    TEST_ASSERT_EQUAL(NMEA_ERR_FORMAT,
                      nmea_parse(sentence("GPRMC,010203,A,37x7.2,S,17516.74,E,0,0,071026"), &fix, NULL));
    TEST_ASSERT_EQUAL(NMEA_ERR_FORMAT,
                      nmea_parse(sentence("GPRMC,010203,A,3767.2,S,17516.74,E,0,0,071026"), &fix, NULL)); /* 67 min */
    TEST_ASSERT_EQUAL(NMEA_ERR_FORMAT,
                      nmea_parse(sentence("GPRMC,010203,A,3747.2,X,17516.74,E,0,0,071026"), &fix, NULL)); /* hemi */
    TEST_ASSERT_EQUAL(NMEA_ERR_FORMAT,
                      nmea_parse(sentence("GPRMC,010203,A,3747.2,E,17516.74,E,0,0,071026"), &fix, NULL)); /* E on lat */
    TEST_ASSERT_EQUAL(NMEA_ERR_FORMAT, nmea_parse(sentence("GPRMC,010203,Q,,,,,,,071026"), &fix, NULL));
    TEST_ASSERT_EQUAL(NMEA_ERR_FORMAT, nmea_parse(sentence("GPRMC,010203,A"), &fix, NULL)); /* too few */
    TEST_ASSERT_EQUAL(NMEA_ERR_FORMAT, nmea_parse(sentence("GPGGA,010203,3747.2,S,17516.74,E,,08"), &fix, NULL));
}

static void test_too_many_fields_rejected(void)
{
    TEST_ASSERT_EQUAL(NMEA_ERR_FORMAT, nmea_parse(sentence("GPRMC,,,,,,,,,,,,,,,,,,,,,,,,"), &fix, NULL));
}

static void test_too_long_rejected(void)
{
    char body[120];
    memset(body, '1', sizeof body - 1);
    memcpy(body, "GPRMC,", 6);
    body[sizeof body - 1] = '\0';
    TEST_ASSERT_EQUAL(NMEA_ERR_TOO_LONG, nmea_parse(sentence(body), &fix, &stats));
    TEST_ASSERT_EQUAL_UINT32(1, stats.rejected);
}

static void test_unsupported_sentence_counted_separately(void)
{
    TEST_ASSERT_EQUAL(NMEA_ERR_UNSUPPORTED, nmea_parse(sentence("GPGSV,1,1,00"), &fix, &stats));
    TEST_ASSERT_EQUAL_UINT32(1, stats.unsupported);
    TEST_ASSERT_EQUAL_UINT32(0, stats.rejected);
}

static void test_result_names(void)
{
    TEST_ASSERT_EQUAL_STRING("checksum", nmea_result_name(NMEA_ERR_CHECKSUM));
    TEST_ASSERT_EQUAL_STRING("ok", nmea_result_name(NMEA_OK));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_checksum_valid_examples);
    RUN_TEST(test_checksum_detects_single_char_change);
    RUN_TEST(test_checksum_lowercase_hex_accepted);
    RUN_TEST(test_parse_gga);
    RUN_TEST(test_parse_rmc_speed_in_mps);
    RUN_TEST(test_south_and_west_are_negative);
    RUN_TEST(test_gn_talker_and_crlf_accepted);
    RUN_TEST(test_rmc_status_void_is_ok_but_no_fix);
    RUN_TEST(test_gga_quality_zero_is_no_fix);
    RUN_TEST(test_malformed_lines_rejected);
    RUN_TEST(test_bad_fields_rejected);
    RUN_TEST(test_too_many_fields_rejected);
    RUN_TEST(test_too_long_rejected);
    RUN_TEST(test_unsupported_sentence_counted_separately);
    RUN_TEST(test_result_names);
    return UNITY_END();
}
