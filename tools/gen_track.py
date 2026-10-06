#!/usr/bin/env python3
"""Generate NMEA GPS tracks for the collar simulator.

Each scenario is a list of "epochs" (one per second). For every epoch we
write a $GPGGA line followed by a $GPRMC line, like a real 1 Hz receiver.
Positions are planned in local metres (x east, y north) around the first
fence vertex, then converted to lat/lon with the same equirectangular
projection the firmware uses. The fence is read from app/fence_config.h.

usage:
  python tools/gen_track.py all --out-dir build/scenarios
  python tools/gen_track.py approach_and_breach --out track.nmea --noise 0.5
  python tools/gen_track.py approach_and_breach --c-array wokwi/track_data.h
"""
from __future__ import annotations

import argparse
import math
import random
import re
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FENCE_HEADER = ROOT / "app" / "fence_config.h"
EARTH_RADIUS_M = 6371000.0


# ---------------------------------------------------------------- fence ----
def load_fence(path: Path = FENCE_HEADER) -> list[tuple[float, float]]:
    """Return fence vertices [(lat, lon), ...] parsed from the C header."""
    text = path.read_text()
    body = text[text.index("FENCE_VERTICES[]"):]
    body = body[: body.index("};")]
    pts = re.findall(r"\{\s*(-?\d+\.\d+)\s*,\s*(-?\d+\.\d+)\s*\}", body)
    return [(float(a), float(b)) for a, b in pts]


class Projection:
    """Equirectangular projection around an origin (matches geofence.c)."""

    def __init__(self, lat0: float, lon0: float):
        self.lat0, self.lon0 = lat0, lon0
        self.m_per_deg_lat = EARTH_RADIUS_M * math.pi / 180.0
        self.m_per_deg_lon = self.m_per_deg_lat * math.cos(math.radians(lat0))

    def to_geo(self, x: float, y: float) -> tuple[float, float]:
        return self.lat0 + y / self.m_per_deg_lat, self.lon0 + x / self.m_per_deg_lon

    def to_local(self, lat: float, lon: float) -> tuple[float, float]:
        return (lon - self.lon0) * self.m_per_deg_lon, (lat - self.lat0) * self.m_per_deg_lat


FENCE = load_fence()
PROJ = Projection(*FENCE[0])


# ----------------------------------------------------------------- NMEA ----
def checksum(body: str) -> str:
    c = 0
    for ch in body:
        c ^= ord(ch)
    return f"{c:02X}"


def frame(body: str, bad_checksum: bool = False) -> str:
    cs = checksum(body)
    if bad_checksum:
        cs = f"{(int(cs, 16) ^ 0x5A):02X}"
    return f"${body}*{cs}"


def fmt_coord(value: float, deg_digits: int, pos: str, neg: str) -> tuple[str, str]:
    hemi = pos if value >= 0 else neg
    v = abs(value)
    deg = int(v)
    minutes = (v - deg) * 60.0
    if round(minutes, 5) >= 60.0:  # avoid printing 60.00000 minutes
        deg, minutes = deg + 1, 0.0
    return f"{deg:0{deg_digits}d}{minutes:08.5f}", hemi


def utc(t_s: int) -> str:
    return f"{t_s // 3600 % 24:02d}{t_s // 60 % 60:02d}{t_s % 60:02d}.00"


def gga(t_s: int, lat: float | None, lon: float | None) -> str:
    if lat is None:
        return f"GPGGA,{utc(t_s)},,,,,0,00,,,M,,M,,"
    la, ns = fmt_coord(lat, 2, "N", "S")
    lo, ew = fmt_coord(lon, 3, "E", "W")
    return f"GPGGA,{utc(t_s)},{la},{ns},{lo},{ew},1,08,0.9,45.0,M,25.0,M,,"


def rmc(t_s: int, lat: float | None, lon: float | None, speed_mps: float = 0.0,
        course: float = 0.0) -> str:
    if lat is None:
        return f"GPRMC,{utc(t_s)},V,,,,,,,071026,,,N"
    la, ns = fmt_coord(lat, 2, "N", "S")
    lo, ew = fmt_coord(lon, 3, "E", "W")
    knots = speed_mps / 0.514444
    return f"GPRMC,{utc(t_s)},A,{la},{ns},{lo},{ew},{knots:.2f},{course:.1f},071026,,,A"


# ------------------------------------------------------------ scenarios ----
@dataclass
class Epoch:
    x: float
    y: float
    valid: bool = True
    bad_checksum: bool = False      # corrupt this epoch's RMC checksum
    extra_lines: list[str] = field(default_factory=list)  # junk before GGA


@dataclass
class Scenario:
    name: str
    description: str
    noise_m: float
    sim_args: list[str] = field(default_factory=list)


def walk(points: list[tuple[float, float]], speed: float) -> list[tuple[float, float]]:
    """Straight-line legs between waypoints at `speed` m/s, one point per s."""
    out = [points[0]]
    for (x0, y0), (x1, y1) in zip(points, points[1:]):
        steps = max(1, round(math.hypot(x1 - x0, y1 - y0) / speed))
        out += [(x0 + (x1 - x0) * k / steps, y0 + (y1 - y0) * k / steps)
                for k in range(1, steps + 1)]
    return out


def stay(p: tuple[float, float], seconds: int) -> list[tuple[float, float]]:
    return [p] * seconds


def graze(rng: random.Random, start: tuple[float, float], seconds: int,
          box=(25.0, 95.0, 25.0, 125.0), speed=0.4) -> list[tuple[float, float]]:
    """Random walk inside a box that is >= 25 m from every fence edge."""
    x, y = start
    heading = rng.uniform(0, 2 * math.pi)
    out = []
    for _ in range(seconds):
        heading += rng.gauss(0, 0.4)
        nx, ny = x + speed * math.cos(heading), y + speed * math.sin(heading)
        if not (box[0] <= nx <= box[1] and box[2] <= ny <= box[3]):
            heading += math.pi  # bounce off the edge of the grazing box
            nx, ny = x + speed * math.cos(heading), y + speed * math.sin(heading)
        x, y = nx, ny
        out.append((x, y))
    return out


def plan(name: str, rng: random.Random) -> list[Epoch]:
    if name == "grazing_inside":
        return [Epoch(x, y) for x, y in graze(rng, (60, 60), 300)]

    if name == "approach_and_breach":
        path = (walk([(100, 40), (215, 40)], 1.0)      # through the warning band
                + stay((215, 40), 35)                   # outside: escalate
                + walk([(215, 40), (205, 25), (150, 25)], 1.0)  # walk back in
                + stay((150, 25), 20))
        return [Epoch(x, y) for x, y in path]

    if name == "gps_noise_at_boundary":
        # Stand 2.5 m inside the east edge: deep in WARNING, but GPS noise
        # pushes single fixes outside now and then.
        path = walk([(150, 40), (197.5, 40)], 1.0) + stay((197.5, 40), 180)
        return [Epoch(x, y) for x, y in path]

    if name == "bad_gps":
        epochs = [Epoch(x, y) for x, y in graze(rng, (60, 60), 200)]
        for i, e in enumerate(epochs):
            if 80 <= i < 110:
                e.valid = False                       # 30 s with no fix
            elif i % 7 == 3:
                e.bad_checksum = True                 # corrupted on the wire
        epochs[20].extra_lines.append("$GPGGA,000020.00,3747.2,S,17516.7,E,1,08*")  # no hex
        epochs[40].extra_lines.append("#$%^&*() not nmea at all")
        epochs[60].extra_lines.append("$GPGGA," + "9" * 90 + "*00")                  # too long
        epochs[120].extra_lines.append("$GPGGA,001,4807.038,N,01131.000*")           # truncated
        epochs[140].extra_lines.append(frame("GPGSV,3,1,11,03,03,111,00,04,15,270,00"))  # unsupported
        return epochs

    if name == "stationary_cow":
        path = (walk([(40, 40), (60, 40)], 0.7)
                + stay((60, 40), 200)
                + walk([(60, 40), (80, 100)], 0.8))
        return [Epoch(x, y) for x, y in path]

    if name == "radio_offline":
        return [Epoch(x, y) for x, y in graze(rng, (60, 60), 600)]

    raise KeyError(name)


SCENARIOS = {
    s.name: s for s in [
        Scenario("grazing_inside", "wandering well inside the fence", 0.8),
        Scenario("approach_and_breach", "walk to the edge, out, and back in", 0.5),
        Scenario("gps_noise_at_boundary", "standing near the edge with jittery GPS", 1.2),
        Scenario("bad_gps", "bad checksums, junk lines and a 30 s fix outage", 0.8),
        Scenario("stationary_cow", "stops moving for 200 s, then walks on", 0.3),
        Scenario("radio_offline", "radio offline from 60 s to 500 s", 0.8,
                 ["--radio-offline", "60:500"]),
    ]
}


def generate(name: str, noise_m: float | None = None, seed: int = 1) -> list[str]:
    """Return the NMEA lines for a scenario."""
    sc = SCENARIOS[name]
    rng = random.Random(seed)
    noise = sc.noise_m if noise_m is None else noise_m
    epochs = plan(name, rng)
    lines: list[str] = []
    prev = None
    for t, e in enumerate(epochs):
        lines += e.extra_lines
        mx, my = e.x + rng.gauss(0, noise), e.y + rng.gauss(0, noise)
        if not e.valid:
            lines += [frame(gga(t, None, None)), frame(rmc(t, None, None))]
            prev = None
            continue
        lat, lon = PROJ.to_geo(mx, my)
        speed, course = 0.0, 0.0
        if prev is not None:
            dx, dy = e.x - prev[0], e.y - prev[1]
            speed = math.hypot(dx, dy)
            course = math.degrees(math.atan2(dx, dy)) % 360.0
        prev = (e.x, e.y)
        lines.append(frame(gga(t, lat, lon)))
        good = rmc(t, lat, lon, speed, course)
        if e.bad_checksum:
            # Same framing, but the position was corrupted in transit (a jump
            # 150 m east - outside the fence). The checksum still belongs to
            # the original data, so the parser must reject it.
            far_lat, far_lon = PROJ.to_geo(mx + 150.0, my)
            bad_body = rmc(t, far_lat, far_lon, speed, course)
            lines.append(f"${bad_body}*{checksum(good)}")
        else:
            lines.append(frame(good))
    return lines


def write_c_array(lines: list[str], path: Path, name: str) -> None:
    out = [
        "/* Generated by tools/gen_track.py - built-in GPS track for the ESP32 demo. */",
        "#ifndef TRACK_DATA_H", "#define TRACK_DATA_H", "",
        f"/* scenario: {name}, {len(lines)} NMEA lines (GGA + RMC per second) */",
        "static const char *const TRACK_LINES[] = {",
    ]
    out += [f'    "{ln}",' for ln in lines]
    out += ["};", "", "#define TRACK_LINE_COUNT (sizeof(TRACK_LINES) / sizeof(TRACK_LINES[0]))",
            "", "#endif /* TRACK_DATA_H */", ""]
    path.write_text("\n".join(out), newline="\n")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scenario", choices=[*SCENARIOS, "all"])
    ap.add_argument("--out", type=Path, help="output .nmea file (single scenario)")
    ap.add_argument("--out-dir", type=Path, default=ROOT / "build" / "scenarios")
    ap.add_argument("--noise", type=float, default=None, help="GPS noise sigma in metres")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--c-array", type=Path, help="write a C header instead (for Wokwi)")
    args = ap.parse_args()

    names = list(SCENARIOS) if args.scenario == "all" else [args.scenario]
    for name in names:
        lines = generate(name, args.noise, args.seed)
        if args.c_array:
            write_c_array(lines, args.c_array, name)
            print(f"{name}: {len(lines)} lines -> {args.c_array}")
            continue
        out = args.out if (args.out and len(names) == 1) else args.out_dir / f"{name}.nmea"
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text("\n".join(lines) + "\n", newline="\n")
        print(f"{name}: {len(lines)} lines -> {out}")


if __name__ == "__main__":
    main()
