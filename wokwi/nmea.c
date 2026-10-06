/*
 * nmea.c - see nmea.h.
 *
 * Steps for every line:
 *   1. measure length (bounded, never walk past NMEA_MAX_LINE_LEN + 2)
 *   2. check framing "$<body>*HH" and the XOR checksum
 *   3. copy <body> into a local fixed-size buffer and split it on ','
 *   4. decode the fields for GGA or RMC
 */
#include "nmea.h"

#include <stddef.h>

#define KNOTS_TO_MPS 0.514444f

/* Internal result of the framing check. */
typedef struct {
    nmea_result_t result;
    size_t body_len; /* number of chars between '$' and '*' */
} frame_t;

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Length of the line without trailing CR/LF. Stops early if the line is
 * too long so we never read far past the end of a bad buffer. */
static size_t bounded_len(const char *line, bool *too_long)
{
    size_t n = 0;
    *too_long = false;
    while (line[n] != '\0' && line[n] != '\r' && line[n] != '\n') {
        n++;
        if (n > NMEA_MAX_LINE_LEN) {
            *too_long = true;
            return n;
        }
    }
    return n;
}

static frame_t check_frame(const char *line)
{
    frame_t f = { NMEA_ERR_FORMAT, 0 };
    bool too_long;
    size_t len;
    size_t i;
    uint8_t sum = 0;
    int hi, lo;

    if (line == NULL) return f;
    len = bounded_len(line, &too_long);
    if (too_long) {
        f.result = NMEA_ERR_TOO_LONG;
        return f;
    }
    /* Shortest useful frame is "$X*HH" (5 chars). */
    if (len < 5 || line[0] != '$') return f;
    /* The '*' must be exactly 3 chars from the end: "...*HH". */
    if (line[len - 3] != '*') return f;

    for (i = 1; i < len - 3; i++) {
        if (line[i] == '*' || line[i] == '$') return f; /* stray delimiter */
        sum ^= (uint8_t)line[i];
    }
    hi = hex_value(line[len - 2]);
    lo = hex_value(line[len - 1]);
    if (hi < 0 || lo < 0) return f;

    f.body_len = len - 4; /* minus '$', '*', and two hex digits */
    f.result = (sum == (uint8_t)((hi << 4) | lo)) ? NMEA_OK : NMEA_ERR_CHECKSUM;
    return f;
}

bool nmea_checksum_ok(const char *line)
{
    return check_frame(line).result == NMEA_OK;
}

/*
 * Parse an unsigned decimal like "4807.038" without strtod (no locale,
 * no "inf"/"nan"/hex surprises). Rejects empty strings and any other char.
 */
static bool parse_decimal(const char *s, double *out)
{
    double value = 0.0;
    double scale = 1.0;
    bool seen_dot = false;
    bool seen_digit = false;

    for (; *s != '\0'; s++) {
        if (*s >= '0' && *s <= '9') {
            seen_digit = true;
            if (seen_dot) {
                scale /= 10.0;
                value += (double)(*s - '0') * scale;
            } else {
                value = value * 10.0 + (double)(*s - '0');
            }
        } else if (*s == '.' && !seen_dot) {
            seen_dot = true;
        } else {
            return false;
        }
    }
    if (!seen_digit) return false;
    *out = value;
    return true;
}

/*
 * NMEA coordinates are "dddmm.mmmm": degrees * 100 + minutes.
 * pos/neg are the hemisphere letters ('N'/'S' or 'E'/'W').
 */
static bool parse_coord(const char *value, const char *hemi, char pos, char neg,
                        double max_deg, double *out_deg)
{
    double raw, minutes;
    int degrees;

    if (!parse_decimal(value, &raw)) return false;
    if (hemi[0] == '\0' || hemi[1] != '\0') return false;

    degrees = (int)(raw / 100.0);
    minutes = raw - (double)degrees * 100.0;
    if (minutes >= 60.0 || (double)degrees > max_deg) return false;

    *out_deg = (double)degrees + minutes / 60.0;
    if (*out_deg > max_deg) return false;

    if (hemi[0] == neg) {
        *out_deg = -*out_deg;
    } else if (hemi[0] != pos) {
        return false;
    }
    return true;
}

static bool parse_uint(const char *s, uint32_t max, uint32_t *out)
{
    double v;
    if (!parse_decimal(s, &v) || v > (double)max) return false;
    *out = (uint32_t)v;
    return true;
}

/* Optional UTC field: empty is allowed (receiver has no time yet). */
static bool parse_utc(const char *s, uint32_t *out)
{
    *out = 0;
    if (s[0] == '\0') return true;
    return parse_uint(s, 235960u, out);
}

/* $GPGGA,time,lat,N,lon,E,quality,sats,hdop,alt,M,geoid,M,age,station */
static nmea_result_t parse_gga(char **f, int n, nmea_fix_t *fix)
{
    uint32_t quality, sats = 0;

    if (n < 10) return NMEA_ERR_FORMAT;
    if (!parse_utc(f[1], &fix->utc_hhmmss)) return NMEA_ERR_FORMAT;
    if (!parse_uint(f[6], 9u, &quality)) return NMEA_ERR_FORMAT;
    if (f[7][0] != '\0' && !parse_uint(f[7], 99u, &sats)) return NMEA_ERR_FORMAT;

    fix->type = NMEA_SENTENCE_GGA;
    fix->satellites = (uint8_t)sats;
    fix->fix_valid = (quality > 0);
    if (fix->fix_valid) {
        if (!parse_coord(f[2], f[3], 'N', 'S', 90.0, &fix->lat_deg)) return NMEA_ERR_FORMAT;
        if (!parse_coord(f[4], f[5], 'E', 'W', 180.0, &fix->lon_deg)) return NMEA_ERR_FORMAT;
    }
    return NMEA_OK;
}

/* $GPRMC,time,status,lat,N,lon,E,speed_kn,course,date,magvar,E,mode */
static nmea_result_t parse_rmc(char **f, int n, nmea_fix_t *fix)
{
    double knots = 0.0;

    if (n < 10) return NMEA_ERR_FORMAT;
    if (!parse_utc(f[1], &fix->utc_hhmmss)) return NMEA_ERR_FORMAT;
    if ((f[2][0] != 'A' && f[2][0] != 'V') || f[2][1] != '\0') return NMEA_ERR_FORMAT;

    fix->type = NMEA_SENTENCE_RMC;
    fix->fix_valid = (f[2][0] == 'A');
    if (fix->fix_valid) {
        if (!parse_coord(f[3], f[4], 'N', 'S', 90.0, &fix->lat_deg)) return NMEA_ERR_FORMAT;
        if (!parse_coord(f[5], f[6], 'E', 'W', 180.0, &fix->lon_deg)) return NMEA_ERR_FORMAT;
        if (f[7][0] != '\0' && !parse_decimal(f[7], &knots)) return NMEA_ERR_FORMAT;
    }
    fix->speed_mps = (float)knots * KNOTS_TO_MPS;
    return NMEA_OK;
}

static bool type_is(const char *field0, const char *type3)
{
    /* field0 is "TTSSS": 2-char talker (GP, GN, GL...) + 3-char type. */
    int i;
    for (i = 0; i < 5; i++) {
        if (field0[i] == '\0') return false;
    }
    return field0[5] == '\0' && field0[2] == type3[0] && field0[3] == type3[1] &&
           field0[4] == type3[2];
}

static nmea_result_t parse_internal(const char *line, nmea_fix_t *fix)
{
    char buf[NMEA_MAX_LINE_LEN + 1]; /* fixed-size copy we can modify */
    char *fields[NMEA_MAX_FIELDS];
    int n = 0;
    size_t i;
    frame_t frame;
    const nmea_fix_t empty = { NMEA_SENTENCE_NONE, false, 0.0, 0.0, 0.0f, 0, 0 };

    if (fix == NULL) return NMEA_ERR_FORMAT;
    *fix = empty;

    frame = check_frame(line);
    if (frame.result != NMEA_OK) return frame.result;

    /* Copy the body (between '$' and '*') and split on ','. Each ',' becomes
     * '\0' so every fields[k] is a normal C string pointing into buf. */
    fields[n++] = buf;
    for (i = 0; i < frame.body_len; i++) {
        char c = line[i + 1];
        if (c == ',') {
            buf[i] = '\0';
            if (n >= NMEA_MAX_FIELDS) return NMEA_ERR_FORMAT;
            fields[n++] = &buf[i + 1];
        } else {
            buf[i] = c;
        }
    }
    buf[frame.body_len] = '\0';

    if (type_is(fields[0], "GGA")) return parse_gga(fields, n, fix);
    if (type_is(fields[0], "RMC")) return parse_rmc(fields, n, fix);
    return NMEA_ERR_UNSUPPORTED;
}

nmea_result_t nmea_parse(const char *line, nmea_fix_t *fix, nmea_stats_t *stats)
{
    nmea_result_t r = parse_internal(line, fix);

    if (stats != NULL) {
        if (r == NMEA_OK) {
            stats->accepted++;
        } else if (r == NMEA_ERR_UNSUPPORTED) {
            stats->unsupported++;
        } else {
            stats->rejected++;
        }
    }
    return r;
}

const char *nmea_result_name(nmea_result_t r)
{
    switch (r) {
    case NMEA_OK: return "ok";
    case NMEA_ERR_TOO_LONG: return "too_long";
    case NMEA_ERR_FORMAT: return "format";
    case NMEA_ERR_CHECKSUM: return "checksum";
    case NMEA_ERR_UNSUPPORTED: return "unsupported";
    default: return "unknown";
    }
}
