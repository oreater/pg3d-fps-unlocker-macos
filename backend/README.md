# PG3D FPS Unlock backend

The launcher script and injected library behind **PG3D FPS Unlock.app** (see the README one folder up for what the app does). The app bundles this folder as `Contents/Resources/Launcher` and runs `testificateunlocker`; it also works on its own from a terminal.

## Pieces

| Path | Role |
| --- | --- |
| `testificateunlocker` | Preflight (reviewed game build or supported Unity version, x86_64/Rosetta, hardened runtime, Steam), launch, and live controls for the running game |
| `options.tsv` | The options: key, tab, label, hint, choices and the Original / Balanced / Performance presets. The app reads the same file. Keys must match `src/options.c` (the tests check this). Engine rows (`engine-threads`, `gpu-priority`) have `-` presets: presets leave them alone |
| `src/pg3d_fps_unlock.c` | Library entry: validated FPS and v-sync wrapper overrides, the quarter-second timer, engine FPS measurement, live frame-rate cap |
| `src/wrapper_scan.c` | Finds the FPS and v-sync wrappers by binding name in game builds whose addresses aren't listed yet |
| `src/options.c` | Parses the session's options file; per-option value validation |
| `src/optimizer.c` | QualitySettings and RenderSettings options (ceilings, texture limit as a floor), Unity readback, the state file for the app, change polling |
| `src/effects.c` | Camera post-processing layer and legacy SSAO components, and the depth / HDR passes they leave behind |
| `src/volumes.c` | Individual Post Processing Stack v2 effects (AO, bloom, color grading, auto exposure, lens effects, depth of field, motion blur, screen-space reflections) |
| `src/frame_guard.c` | Runs enforcement just before Unity renders each frame; optional per-stage frame profiler |
| `src/input_probe.m` | Input telemetry and fast mouse look |
| `src/latency.c` | Mouse-to-screen and render-to-screen latency, frame pacing percentiles |
| `src/metal_tuning.m` | GPU priority of the game's Metal command queues |
| `src/threads.c` | Engine thread priority (Unity job workers and render thread) |
| `src/probe.c` | On-demand scene inventory (`--probe`) |
| `src/presentation_probe.m`, `src/steam_overlay_probe.c` | Metal presentation telemetry and Steam overlay readiness |
| `src/launch_game.c` | Detached launch with Steam's loader and overlay preloaded |

## Session files

Each run writes `~/Library/Logs/OptimizerUnlocker/run-<time>-<pid>.log` plus sidecars next to it:

- `.pid`: the game's process ID.
- `.profile.options`: `key=value` for every option (`game` keeps the game's value) and `fps=`. The library re-reads it within a quarter second of a change; `--set`, `--set-profile`, `--set-fps` and the app write it atomically.
- `.profile.state`: `key state game current` per option, rewritten every five seconds and after each change. States: `game`, `applied`, `already` (the game already runs this low), `rejected` (Unity refused it; restored and left alone), `unavailable`. For effects, `game` is how many exist and `current` how many are on.
- `.profile.mouse`, `.profile.occlusion`: fast mouse look and the occlusion diagnostic.
- `.profile.probe`: touched by `--probe`; the inventory goes to the log as `probe:` lines.

`latest` in the same folder points at the newest run.

## Command line

```sh
./testificateunlocker --profile performance --mouse fast --job-workers 4
./testificateunlocker --profile balanced --option shadows=0,bloom=game
./testificateunlocker --list-options
./testificateunlocker --set-profile original     # live, all options
./testificateunlocker --set fog=0,lod=30         # live, some options
./testificateunlocker --set engine-threads=1,gpu-priority=1   # live engine settings
./testificateunlocker --set-fps 240              # live frame-rate cap (0 = uncapped)
./testificateunlocker --probe                    # scene inventory into the log
./testificateunlocker --set-occlusion off        # diagnostic; can increase GPU work
./testificateunlocker --status
./testificateunlocker --dry-run
```

`--observe` measures the game's own pacing and changes nothing. Exit statuses are listed at the top of the script.

`../tools/option_bench.py` A/B-tests options or presets in the running game from engine FPS (for example `tools/option_bench.py --preset original --preset performance`).

## Build and test

Requires the Xcode Command Line Tools; no third-party downloads.

```sh
./tests/run.sh    # unit tests with fake Unity/IL2CPP objects, launcher checks; never launches the game
./build.sh        # the x86_64 library and the launch helper
```
