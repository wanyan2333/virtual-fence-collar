#!/usr/bin/env python3
"""Plot one simulator run: fence, track coloured by state, and cue events.

usage:
  python tools/plot_run.py approach_and_breach            # uses build/scenarios/<name>.csv
  python tools/plot_run.py approach_and_breach --csv path/to/events.csv --out docs/x.png

Left panel: map in local metres (x east, y north of the first fence vertex).
Right panel: signed distance to the fence edge over time (+ inside, - outside).
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import gen_track  # noqa: E402

WARN_DIST_M = 10.0

# Status palette: state is a status, so it uses status colours (+ labels).
STATE_COLOR = {"INSIDE": "#2a78d6", "WARNING": "#eda100", "BREACH": "#d03b3b"}
INK, INK_2, GRID, SURFACE = "#0b0b0b", "#52514e", "#e4e3df", "#fcfcfb"


def read_events(csv_path: Path):
    rows = []
    for line in csv_path.read_text().splitlines()[1:]:
        t, event, detail = line.split(",", 2)
        kv = dict(p.split("=", 1) for p in detail.split(";") if "=" in p)
        rows.append((int(t), event, kv))
    return rows


def fence_local() -> np.ndarray:
    pts = [gen_track.PROJ.to_local(lat, lon) for lat, lon in gen_track.FENCE]
    return np.array(pts + [pts[0]])


def warning_band_mask(poly: np.ndarray, xs: np.ndarray, ys: np.ndarray) -> np.ndarray:
    """Grid mask of points inside the fence and < WARN_DIST_M from an edge."""
    from matplotlib.path import Path as MplPath

    gx, gy = np.meshgrid(xs, ys)
    pts = np.column_stack([gx.ravel(), gy.ravel()])
    inside = MplPath(poly).contains_points(pts)
    dmin = np.full(len(pts), np.inf)
    for (ax, ay), (bx, by) in zip(poly[:-1], poly[1:]):
        abx, aby = bx - ax, by - ay
        t = np.clip(((pts[:, 0] - ax) * abx + (pts[:, 1] - ay) * aby) / (abx**2 + aby**2), 0, 1)
        d = np.hypot(pts[:, 0] - (ax + t * abx), pts[:, 1] - (ay + t * aby))
        dmin = np.minimum(dmin, d)
    return (inside & (dmin < WARN_DIST_M)).reshape(gx.shape)


def plot(name: str, csv_path: Path, out: Path) -> None:
    rows = read_events(csv_path)
    fixes = [(t, d) for t, e, d in rows if e == "FIX"]
    fix_at = {t: d for t, d in fixes}
    poly = fence_local()

    plt.rcParams.update({"font.size": 9, "axes.edgecolor": GRID, "axes.labelcolor": INK_2,
                         "xtick.color": INK_2, "ytick.color": INK_2, "text.color": INK})
    fig, (ax, ax2) = plt.subplots(1, 2, figsize=(12, 5.2), facecolor=SURFACE,
                                  gridspec_kw={"width_ratios": [1.1, 1]})
    for a in (ax, ax2):
        a.set_facecolor(SURFACE)
        a.grid(True, color=GRID, linewidth=0.6)
        a.set_axisbelow(True)
        for s in ("top", "right"):
            a.spines[s].set_visible(False)

    # --- map ---------------------------------------------------------------
    xs = np.arange(poly[:, 0].min() - 30, poly[:, 0].max() + 40, 0.5)
    ys = np.arange(poly[:, 1].min() - 30, poly[:, 1].max() + 30, 0.5)
    ax.contourf(xs, ys, warning_band_mask(poly, xs, ys).astype(float), levels=[0.5, 1.5],
                colors=["#fdf0cf"])
    ax.plot(poly[:, 0], poly[:, 1], color=INK, linewidth=2, label="fence")

    # track segments coloured by the state reported on each fix
    for (t0, a), (t1, b) in zip(fixes, fixes[1:]):
        ax.plot([float(a["x"]), float(b["x"])], [float(a["y"]), float(b["y"])],
                color=STATE_COLOR[b["state"]], linewidth=1.6, solid_capstyle="round")
    for state, color in STATE_COLOR.items():
        ax.plot([], [], color=color, linewidth=2, label=f"track: {state}")
    ax.fill([], [], color="#fdf0cf", label=f"warning band ({WARN_DIST_M:.0f} m)")

    def cue_points(event):
        pts = [(fix_at[t], d) for t, e, d in rows if e == event and t in fix_at]
        return pts

    audio = cue_points("AUDIO_CUE")
    vib = cue_points("VIBRATION_CUE")
    if audio:
        ax.scatter([float(f["x"]) for f, _ in audio], [float(f["y"]) for f, _ in audio],
                   marker="^", s=90, color=STATE_COLOR["WARNING"], edgecolor=SURFACE,
                   linewidth=1.5, zorder=5, label="audio cue")
    if vib:
        ax.scatter([float(f["x"]) for f, _ in vib], [float(f["y"]) for f, _ in vib],
                   marker="D", s=70, color=STATE_COLOR["BREACH"], edgecolor=SURFACE,
                   linewidth=1.5, zorder=5, label="vibration cue")
        for i, (f, d) in enumerate(vib):  # stagger labels so they don't overlap
            ax.annotate(f"L{d['level']}", (float(f["x"]), float(f["y"])),
                        textcoords="offset points", xytext=(8, 6 + 10 * i),
                        fontsize=8, color=INK_2)
    ax.set_aspect("equal")
    ax.set_xlabel("metres east")
    ax.set_ylabel("metres north")
    ax.legend(loc="upper right", fontsize=8, frameon=False)

    # --- distance over time -----------------------------------------------
    t_s = np.array([t / 1000 for t, _ in fixes])
    signed = np.array([float(d["dist"]) * (1 if d["inside"] == "1" else -1) for _, d in fixes])
    ax2.axhspan(0, WARN_DIST_M, color="#fdf0cf", linewidth=0)
    ax2.axhline(0, color=INK, linewidth=1)
    states = [d["state"] for _, d in fixes]
    for i in range(1, len(fixes)):
        ax2.plot(t_s[i - 1:i + 1], signed[i - 1:i + 1], color=STATE_COLOR[states[i]], linewidth=1.6)
    for event, marker, color, label in (("AUDIO_CUE", "^", STATE_COLOR["WARNING"], "audio cue"),
                                        ("VIBRATION_CUE", "D", STATE_COLOR["BREACH"], "vibration cue")):
        pts = [(t / 1000, float(fix_at[t]["dist"]) * (1 if fix_at[t]["inside"] == "1" else -1))
               for t, e, _ in rows if e == event and t in fix_at]
        if pts:
            ax2.scatter(*zip(*pts), marker=marker, s=70, color=color, edgecolor=SURFACE,
                        linewidth=1.5, zorder=5, label=label)
    ax2.text(t_s[0] if len(t_s) else 0, WARN_DIST_M + 0.5, "warning band", fontsize=8, color=INK_2)
    ax2.text(t_s[0] if len(t_s) else 0, -2.5, "outside", fontsize=8, color=INK_2)
    ax2.set_xlabel("time (s)")
    ax2.set_ylabel("signed distance to fence edge (m)")
    ax2.legend(loc="upper right", fontsize=8, frameon=False)

    summary = next((d for _, e, d in rows if e == "SUMMARY"), {})
    fig.suptitle(f"{name}: {summary.get('transitions', '?')} state changes, "
                 f"{summary.get('audio', '?')} audio / {summary.get('vibration', '?')} vibration cues",
                 fontsize=11, color=INK, x=0.06, ha="left")
    fig.tight_layout()
    out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out, dpi=130, facecolor=SURFACE)
    plt.close(fig)
    print(f"wrote {out}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scenario")
    ap.add_argument("--csv", type=Path)
    ap.add_argument("--out", type=Path)
    args = ap.parse_args()
    csv_path = args.csv or ROOT / "build" / "scenarios" / f"{args.scenario}.csv"
    out = args.out or ROOT / "docs" / f"{args.scenario}.png"
    plot(args.scenario, csv_path, out)


if __name__ == "__main__":
    main()
