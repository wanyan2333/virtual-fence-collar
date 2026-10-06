"""End-to-end scenario tests: generate a GPS track, run the host simulator,
and check the CSV event log it writes.

Run:  python -m pytest tests -v
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
OUT = BUILD / "scenarios"
sys.path.insert(0, str(ROOT / "tools"))

import gen_track  # noqa: E402


# ------------------------------------------------------------ helpers ------
def _sim_path() -> Path:
    exe = "collar_sim.exe" if os.name == "nt" else "collar_sim"
    return BUILD / exe


@pytest.fixture(scope="session")
def sim() -> Path:
    """Configure (once) and build the simulator; return its path."""
    if not (BUILD / "CMakeCache.txt").exists():
        cmd = ["cmake", "-S", str(ROOT), "-B", str(BUILD)]
        if shutil.which("ninja"):
            cmd += ["-G", "Ninja"]
        subprocess.run(cmd, check=True)
    subprocess.run(["cmake", "--build", str(BUILD), "--target", "collar_sim"], check=True)
    assert _sim_path().exists()
    return _sim_path()


class Run:
    """Parsed event log of one simulator run."""

    def __init__(self, csv_path: Path):
        self.rows: list[tuple[int, str, dict[str, str]]] = []
        for line in csv_path.read_text().splitlines()[1:]:
            t, event, detail = line.split(",", 2)
            kv = dict(p.split("=", 1) for p in detail.split(";") if "=" in p)
            self.rows.append((int(t), event, kv))
        self.summary = self.only("SUMMARY")[0][2]

    def only(self, event: str):
        return [r for r in self.rows if r[1] == event]

    def transitions(self) -> list[str]:
        return [f"{d['from']}->{d['to']}" for _, _, d in self.only("STATE")]

    def cues(self) -> int:
        return len(self.only("AUDIO_CUE")) + len(self.only("VIBRATION_CUE"))


def run_scenario(sim: Path, name: str, extra: list[str] | None = None, tag: str = "") -> Run:
    OUT.mkdir(parents=True, exist_ok=True)
    track = OUT / f"{name}.nmea"
    track.write_text("\n".join(gen_track.generate(name)) + "\n")
    log = OUT / f"{name}{tag}.csv"
    args = [str(sim), str(track), str(log), "--quiet"]
    args += gen_track.SCENARIOS[name].sim_args + (extra or [])
    subprocess.run(args, check=True)
    return Run(log)


# ------------------------------------------------------------ scenarios ----
def test_grazing_inside(sim):
    r = run_scenario(sim, "grazing_inside")
    assert r.cues() == 0
    assert r.transitions() == []
    assert r.summary["final_state"] == "INSIDE"
    assert int(r.summary["gps_samples"]) == 300


def test_approach_and_breach(sim):
    r = run_scenario(sim, "approach_and_breach")
    assert r.transitions() == [
        "INSIDE->WARNING", "WARNING->BREACH", "BREACH->WARNING", "WARNING->INSIDE"]

    audio = r.only("AUDIO_CUE")
    vib = r.only("VIBRATION_CUE")
    assert len(audio) == 1
    assert [int(d["level"]) for _, _, d in vib] == [1, 2, 3]
    assert audio[0][0] < vib[0][0], "audio warning must come before vibration"
    gaps = [b[0] - a[0] for a, b in zip(vib, vib[1:])]
    assert all(g >= 10_000 for g in gaps), gaps

    # The breach is only declared after the animal is really outside.
    t_breach = next(t for t, _, d in r.only("STATE") if d["to"] == "BREACH")
    fix_at_breach = next(d for t, e, d in r.rows if e == "FIX" and t == t_breach)
    assert fix_at_breach["inside"] == "0"
    assert r.summary["final_state"] == "INSIDE"


def test_gps_noise_at_boundary_hysteresis(sim):
    r = run_scenario(sim, "gps_noise_at_boundary")
    # Make sure the test is meaningful: noise really did put fixes outside.
    outside_fixes = [d for _, _, d in r.only("FIX") if d["inside"] == "0"]
    assert len(outside_fixes) >= 2

    assert r.transitions() == ["INSIDE->WARNING"]
    assert len(r.only("VIBRATION_CUE")) == 0

    # Same track with hysteresis disabled flaps - that is what we prevent.
    no_hyst = run_scenario(sim, "gps_noise_at_boundary", ["--confirm", "1"], tag="_no_hysteresis")
    assert len(no_hyst.transitions()) > 3
    assert len(no_hyst.only("VIBRATION_CUE")) > 0


def test_bad_gps(sim):
    r = run_scenario(sim, "bad_gps")
    assert r.cues() == 0, "corrupt or missing GPS must never trigger a cue"
    assert r.transitions() == []
    assert int(r.summary["nmea_rejected"]) > 0
    reasons = {d["reason"] for _, _, d in r.only("NMEA_REJECT")}
    assert {"checksum", "format", "too_long"} <= reasons
    assert len(r.only("NO_FIX")) == 30


def test_stationary_cow_duty_cycling(sim):
    r = run_scenario(sim, "stationary_cow")
    changes = [(t, int(d["ms"])) for t, _, d in r.only("GPS_INTERVAL")]
    assert [ms for _, ms in changes] == [30_000, 1_000]
    t_slow, t_fast = changes[0][0], changes[1][0]
    assert t_slow >= 60_000, "must be still for 60 s before slowing down"
    assert t_fast > t_slow
    # Fewer GPS samples than seconds in the track = power saved.
    total_seconds = r.only("SUMMARY")[0][0] // 1000
    assert int(r.summary["gps_samples"]) < total_seconds * 0.7


def test_radio_offline_buffer(sim):
    r = run_scenario(sim, "radio_offline")
    radio = [(t, d["online"]) for t, _, d in r.only("RADIO")]
    assert radio == [(60_000, "0"), (500_000, "1")]

    drops = r.only("TELEM_DROP")
    assert len(drops) > 0
    assert int(r.summary["telem_dropped"]) == len(drops)

    # The big flush right at reconnect sends a full buffer...
    reconnect_flush = next(d for t, e, d in r.rows if e == "TELEM_FLUSH" and t == 500_000)
    assert int(reconnect_flush["sent"]) == gen_track_capacity()
    # ...starting after the dropped ones: oldest were dropped, newest kept.
    last_sent_before = max(int(d["first_seq"]) for t, e, d in r.rows
                           if e == "TELEM_FLUSH" and t < 60_000)
    assert int(reconnect_flush["first_seq"]) == last_sent_before + 1 + len(drops)
    assert r.summary["telem_queued"] == "0"


def gen_track_capacity() -> int:
    """TELEM_CAPACITY read from the C header, so the test follows the code."""
    text = (ROOT / "core" / "telemetry.h").read_text()
    return int(text.split("#define TELEM_CAPACITY")[1].split()[0].rstrip("uU"))


def test_fence_header_parsed():
    assert len(gen_track.FENCE) == 6
    x, y = gen_track.PROJ.to_local(*gen_track.FENCE[1])
    assert abs(x - 200.0) < 0.05 and abs(y) < 0.05
