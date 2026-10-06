# Running the collar firmware on a simulated ESP32 (Wokwi)

This folder is a complete, flat Wokwi project: an ESP32 DevKit, two LEDs that
stand in for the speaker and the vibration motor, and a button that toggles the
radio. GPS input is a built-in NMEA track (`track_data.h`, the
`approach_and_breach` scenario), so no GPS part is needed.

> Status: **built and run in Wokwi on 2026-10-07.** The final Serial line was
> `SUMMARY gps_samples=245;nmea_ok=490;nmea_rejected=0;nmea_unsupported=0;transitions=4;audio=1;vibration=3;telem_sent=41;telem_dropped=0;telem_queued=10;final_state=INSIDE`.
> The cue counts match the host simulator. `telem_queued=10` means the radio
> was switched off with the button when the track ended.
> A second run tested the radio buffer: offline at ~10 s gave
> `telem_sent=8;telem_dropped=11;telem_queued=32`, and pressing again after
> the track ended gave `TELEM_FLUSH sent=32;first_seq=19;remaining=0`.

Do not edit the copies here by hand. They are generated from `core/`, `app/`
and `hal/` by `python tools/sync_wokwi.py`, and CI fails if they drift.

## Wiring (already in `diagram.json`)

| Part | ESP32 pin | Meaning |
|---|---|---|
| Yellow LED + 220 Ω | GPIO 26 → GND | **audio cue** (warning: near the fence) |
| Red LED + 220 Ω | GPIO 27 → GND | **vibration cue** (breach). Pulse length = 300 ms × level |
| Green push button | GPIO 14 → GND (internal pull-up) | toggles **radio online/offline** |
| Serial monitor | TX/RX | event log, 115200 baud |

## Step by step in the browser

1. Open <https://wokwi.com/projects/new/esp32>. This creates a new **ESP32
   Arduino** project with two tabs: `sketch.ino` and `diagram.json`. Sign in
   if you want to save it.
2. Click the **`sketch.ino`** tab, select all (Ctrl+A), and paste the full
   contents of `wokwi/sketch.ino` from this repo.
3. Click the **`diagram.json`** tab, select all, and paste the contents of
   `wokwi/diagram.json`. The board, two LEDs, two resistors and a button appear.
4. Add the remaining 17 files. Click the small **▾ arrow** to the right of the
   file tabs:
   - Choose **"Upload file(s)…"** and select all the files below from the `wokwi/`
     folder in one go, **or**
   - choose **"New file…"**, type the exact file name, then paste its contents.
     Repeat for each file.

   ```
   collar_app.c   collar_app.h   collar_sm.c    collar_sm.h
   fence_config.h geofence.c     geofence.h     hal.h
   hal_esp32.cpp  hal_esp32.h    nmea.c         nmea.h
   power.c        power.h        telemetry.c    telemetry.h
   track_data.h
   ```
   Don't add anything from `hal/host/` or `app/main_host.c`; those are for the PC.
5. Press the green **▶ Run** button. The first build takes 30–60 s.

## What you should see

Simulated time runs **4× faster** than real time (`SIM_SPEEDUP` in
`hal_esp32.h`), so the 245 s track plays in about a minute. The times below are
the log's simulated `ms` values, with real seconds in brackets. They match the
host run of the same scenario.

| Sim time (real) | Serial monitor | Board |
|---|---|---|
| 0 s | `HELLO`, `BOOT fence_vertices=6;confirm_fixes=2` | |
| 1–91 s (0–23 s) | `FIX ... state=INSIDE` every second, `RADIO_TX bytes=20;frame=...` every 5 s | |
| ≈ 92 s (23 s) | `STATE from=INSIDE;to=WARNING`, `AUDIO_CUE` | yellow LED blinks |
| ≈ 103 s (26 s) | `STATE from=WARNING;to=BREACH`, `VIBRATION_CUE level=1` | red LED, short |
| ≈ 113 s / 123 s | `VIBRATION_CUE level=2`, then `level=3` | red LED, longer each time |
| ≈ 177 s (44 s) | `STATE from=BREACH;to=WARNING` (no cue: animal is returning) | |
| ≈ 187 s (47 s) | `STATE from=WARNING;to=INSIDE` | |
| ≈ 245 s (61 s) | `SUMMARY ...`, `TRACK_END` | |

**Radio test:** while the track is playing, press the green button.
- `RADIO online=0` appears, and `RADIO_TX` lines stop. Records now queue in
  the ring buffer.
- After about 40 real seconds offline (32 records × 5 s ÷ 4) the buffer is full
  and `TELEM_DROP dropped_total=...` lines appear: the oldest records are being
  dropped.
- Press the button again: `RADIO online=1`, then
  `TELEM_FLUSH sent=N;first_seq=...` and a burst of `RADIO_TX` lines.

To replay the track, press **Stop** and then **Run** again.

## Troubleshooting

- `fatal error: xyz.h: No such file or directory`: a file is missing or its
  name has a typo. Names are case-sensitive.
- `multiple definition of main` or of `hal_...`: a host-only file
  (`main_host.c`, `hal_host.c`) was added. Delete it.
- No Serial output: check that `diagram.json` has the two `$serialMonitor`
  connections.
