#!/usr/bin/env python3
"""One command to build and test everything:

    python tools/run_all.py

1. configure + build with CMake (Ninja if available)
2. run the Unity unit tests through CTest
3. run the pytest scenario tests (generates tracks and event logs)
4. plot every scenario into docs/<scenario>.png
5. refresh the Wokwi copies of the shared sources
6. syntax-check the ESP32 HAL + sketch against stub headers
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"

# Windows convenience: WinLibs (installed by winget) is not always on PATH.
WINLIBS = Path(os.environ.get("LOCALAPPDATA", "")) / (
    "Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin")
if os.name == "nt" and not shutil.which("gcc") and WINLIBS.exists():
    os.environ["PATH"] = str(WINLIBS) + os.pathsep + os.environ["PATH"]


def run(cmd: list[str]) -> None:
    print("\n$ " + " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True, cwd=ROOT)


def main() -> int:
    if not (BUILD / "CMakeCache.txt").exists():
        cfg = ["cmake", "-S", ".", "-B", "build", "-DCMAKE_BUILD_TYPE=Debug"]
        if shutil.which("ninja"):
            cfg += ["-G", "Ninja"]
        run(cfg)
    run(["cmake", "--build", "build"])
    run(["ctest", "--test-dir", "build", "--output-on-failure"])
    run([sys.executable, "-m", "pytest", "tests", "-v"])
    sys.path.insert(0, str(ROOT / "tools"))
    import gen_track
    for name in gen_track.SCENARIOS:
        run([sys.executable, "tools/plot_run.py", name])
    run([sys.executable, "tools/sync_wokwi.py"])
    # ESP32 code can't be built here without the ESP32 toolchain; at least
    # syntax-check it against tiny Arduino/FreeRTOS stub headers.
    for f in ("hal_esp32.cpp", "sketch.ino"):
        run(["g++", "-std=gnu++17", "-Wall", "-Wextra", "-fsyntax-only", "-Itests/esp32_stub",
             "-Iwokwi", "-x", "c++", f"wokwi/{f}"])
    print("\nALL OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
