/*
 * nmea.h - Minimal NMEA 0183 parser for $xxGGA and $xxRMC sentences.
 *
 * Pure C, no I/O, no malloc. The caller passes one complete line
 * (e.g. "$GPRMC,...*6A\r\n") and gets back a filled nmea_fix_t.
 */
#ifndef NMEA_H
#define NMEA_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NMEA 0183 allows 82 chars including CR/LF; we count chars before CR/LF. */
#define NMEA_MAX_LINE_LEN 82
/* RMC has 13 fields, GGA has 15; anything with more is rejected. */
#define NMEA_MAX_FIELDS 20

typedef enum {
    NMEA_OK = 0,
    NMEA_ERR_TOO_LONG,    /* line longer than NMEA_MAX_LINE_LEN            */
    NMEA_ERR_FORMAT,      /* missing '$'/'*', bad number, missing field     */
    NMEA_ERR_CHECKSUM,    /* framing OK but XOR checksum does not match     */
    NMEA_ERR_UNSUPPORTED  /* valid sentence, just not GGA or RMC (ignored)  */
} nmea_result_t;

typedef enum {
    NMEA_SENTENCE_NONE = 0,
    NMEA_SENTENCE_GGA,
    NMEA_SENTENCE_RMC
} nmea_sentence_t;

typedef struct {
    nmea_sentence_t type;
    bool fix_valid;       /* GGA: quality > 0, RMC: status 'A'          */
    double lat_deg;       /* + north, - south (only meaningful if valid) */
    double lon_deg;       /* + east,  - west                             */
    float speed_mps;      /* RMC only (converted from knots), else 0     */
    uint8_t satellites;   /* GGA only, else 0                            */
    uint32_t utc_hhmmss;  /* integer part of the UTC time field          */
} nmea_fix_t;

typedef struct {
    uint32_t accepted;    /* parsed OK                                   */
    uint32_t rejected;    /* checksum, format or length errors           */
    uint32_t unsupported; /* well-formed but not GGA/RMC                 */
} nmea_stats_t;

/* True if the line has "$...*HH" framing and HH matches the XOR of the body. */
bool nmea_checksum_ok(const char *line);

/*
 * Parse one line. On NMEA_OK, *fix is filled. If stats is not NULL the
 * matching counter is incremented. A line with fix_valid == false is still
 * NMEA_OK (the receiver told us "no fix"), it is not a rejected sentence.
 */
nmea_result_t nmea_parse(const char *line, nmea_fix_t *fix, nmea_stats_t *stats);

/* Short name for logs, e.g. "checksum". */
const char *nmea_result_name(nmea_result_t r);

#ifdef __cplusplus
}
#endif

#endif /* NMEA_H */
