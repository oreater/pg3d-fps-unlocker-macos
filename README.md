# PG3D FPS Unlock

A macOS app that launches the Steam version of **Pixel Gun 3D** with its frame rate unlocked and lighter, reversible rendering settings. Version 1.5 turns the original command-line unlocker into a full app: pick a profile, press **Play**, and watch live engine FPS, Metal FPS and Steam overlay status while you play.

Everything happens in the running game's memory. Game files, saved game settings and your Steam install are never modified; quit the game and start it from Steam to return to normal.

## Download and install

1. Download `PG3D-FPS-Unlock-1.5.0-macOS.zip` from the [latest release](../../releases/latest) and unzip it.
2. Move **PG3D FPS Unlock.app** to Applications (or ~/Applications).
3. The app is not notarized. The first time, Control-click it and choose **Open**, or allow it under System Settings → Privacy & Security.
4. Open Steam and sign in, then open the app, choose a profile and press **Play**.

Requirements: macOS 13 or later, Pixel Gun 3D PC Edition from Steam, and Rosetta on Apple Silicon (the game itself runs as x86_64). Supported game builds: **26.11.0 (151027)** and **26.11.3 (155325)**. A newer game build is refused until it has been reviewed, because the unlocker validates exact native bindings before changing anything.

## What it does

**FPS unlock.** Keeps Unity's `targetFrameRate` at uncapped (or your cap) and `vSyncCount` at 0, reapplies them if the game resets them, and disables Unity's 60 Hz display-link blit path (`UNITY_DISPLAYLINK_BLIT=0`). The Steam overlay keeps working.

**Rendering profiles.** Reversible quality budgets, read back from Unity before and after every change. Each is a ceiling: a lower setting the game already uses is kept. Switch profiles while playing with **Apply in game**.

| Setting | Balanced | Performance |
| --- | --- | --- |
| Camera post-processing | Kept | Off on all active cameras (PostProcessLayer and legacy SSAO) |
| Leftover depth pass and HDR target | Kept | Off on screen cameras whose post-processing was disabled |
| Shadow distance | Up to 25 | Up to 15; real-time shadows off |
| Shadow cascades / resolution | Up to 2 / Medium | 1 / Low |
| MSAA | Up to 2× | Off |
| Per-pixel lights | Up to 2 | Up to 1 |
| LOD bias | Up to 1 | Up to 0.6 |
| Anisotropic filtering | Per texture | Off |
| Soft particles, soft vegetation, real-time reflection probes | Kept | Off |
| Skin weights | Up to 4 bones | Up to 2 bones |
| Particle raycast budget | Up to 256 | Up to 64 |

**Original** keeps the FPS unlock and restores every value the session changed. Settings stay consistent through death and respawn: they are enforced just before Unity renders each frame.

**Fast mouse look (new in 1.5).** Because the game runs under Rosetta, macOS's own window code for every mouse move runs translated too. While you're aiming (cursor locked), each move goes straight to Unity's input handler instead of through macOS's window bookkeeping: about 10 µs per move instead of about 50. Menus, clicks, keys and a free cursor keep the normal route. It stays off, and the log says why, if another library has hooked any step of that route. Can be switched mid-match.

**Job worker threads (new in 1.5).** Unity starts one worker thread per CPU core minus one. On Macs with many cores, waking those workers cost about 1 ms of every frame in profiling, and more while turning, when Unity syncs moved colliders before each of the game's raycasts. This setting starts the game with Unity's own `-job-worker-count`. **4** worked well in testing: steadier FPS and noticeably better mouse input. Don't combine it with **Multithreaded rendering**; that pairing made the screen flicker, and the app warns about it.

**Multithreaded rendering** (experimental) starts the game with Unity's `-force-gfx-jobs native`. It showed no gain in testing.

**Frame profiler.** Times every Unity player-loop stage and compares frames with and without mouse movement, so the log shows what gets slower while you turn. `python3 tools/mouse_profile.py` summarizes the latest run.

What it never does: hide objects, change camera clipping or culling masks, lower resolution or texture quality, change physics timing, touch networking, or patch game files.

## Command line

The app bundles the same backend used from the terminal. From a source checkout:

```sh
./pg3d-fps-unlock --profile performance
./pg3d-fps-unlock --profile performance --mouse fast --job-workers 4
./pg3d-fps-unlock --profile original --fps 240
./pg3d-fps-unlock --set-profile balanced      # switch profile in the running game
./pg3d-fps-unlock --set-mouse fast            # or game
./pg3d-fps-unlock --status                    # latest session log
./pg3d-fps-unlock --dry-run                   # preflight only
./pg3d-fps-unlock --help                      # every option
```

Exit status: 0 success, 1 general failure, 2 invalid arguments, 3 unsupported game build (usually a Pixel Gun update), 4 game build cannot load the unlocker (native Apple Silicon code, hardened runtime, or Rosetta missing), 5 the game started but readiness was not confirmed within 20 seconds (usually still loading).

Session logs are written to `~/Library/Logs/OptimizerUnlocker/`.

## Build from source

Requires the Xcode Command Line Tools; no third-party downloads.

```sh
./backend/tests/run.sh   # unit tests and launcher checks; never launches the game
./build-app.sh           # builds and ad-hoc signs "PG3D FPS Unlock.app" here
```

The app's name and version live in `macos-app/Info.plist`.

| Path | Contents |
| --- | --- |
| `macos-app/` | SwiftUI app, icon generator, Info.plist, Russo One font (SIL OFL) |
| `backend/testificateunlocker` | Launcher script: preflight, options, live controls |
| `backend/src/` | Injected x86_64 library (FPS unlock, profiles, frame guard, fast mouse look, profiler) and the launch helper |
| `backend/tests/` | Unit tests |
| `tools/` | Measurement helpers (profile comparison, paired camera traces, symbolication, mouse profile summary) |

## Notes

- Pixel Gun 3D is a signed third-party game. Use of modifications may be governed by its terms or anti-cheat policy; use this only where permitted.
- Not affiliated with Pixel Gun 3D or Valve.

## Author's note
vibecoded as hell.
