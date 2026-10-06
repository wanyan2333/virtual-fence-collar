#!/usr/bin/env python3
"""Copy the shared firmware sources into wokwi/ (Wokwi wants one flat folder)
and generate wokwi/track_data.h (the built-in GPS track).

    python tools/sync_wokwi.py          # update wokwi/
    python tools/sync_wokwi.py --check  # exit 1 if wokwi/ is out of date (CI)
"""
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WOKWI = ROOT / "wokwi"
sys.path.insert(0, str(ROOT / "tools"))
import gen_track  # noqa: E402

DEMO_SCENARIO = "approach_and_breach"

SOURCES = (
    sorted((ROOT / "core").glob("*.[ch]"))
    + [ROOT / "hal" / "hal.h",
       ROOT / "hal" / "esp32" / "hal_esp32.h",
       ROOT / "hal" / "esp32" / "hal_esp32.cpp",
       ROOT / "app" / "collar_app.h",
       ROOT / "app" / "collar_app.c",
       ROOT / "app" / "fence_config.h",
       ROOT / "app" / "sketch.ino"]
)


def expected_files() -> dict[str, str]:
    files = {src.name: src.read_text() for src in SOURCES}
    with tempfile.TemporaryDirectory() as tmp:
        p = Path(tmp) / "track_data.h"
        gen_track.write_c_array(gen_track.generate(DEMO_SCENARIO), p, DEMO_SCENARIO)
        files["track_data.h"] = p.read_text()
    return files


def main() -> int:
    check = "--check" in sys.argv
    stale = []
    for name, content in expected_files().items():
        dst = WOKWI / name
        current = dst.read_text() if dst.exists() else None
        if current == content:
            continue
        stale.append(name)
        if not check:
            dst.write_text(content, newline="\n")
    if check:
        if stale:
            print("wokwi/ is out of date: " + ", ".join(stale))
            print("run: python tools/sync_wokwi.py")
            return 1
        print("wokwi/ is in sync")
        return 0
    print(f"updated {len(stale)} file(s) in wokwi/: " + (", ".join(stale) or "none"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
