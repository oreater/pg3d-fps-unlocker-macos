# PG3D FPS Unlock

A macOS app that launches the Steam version of **Pixel Gun 3D** with its frame rate unlocked and lighter, reversible rendering settings. It has 21 graphics options the game itself doesn't offer, in Effects, Lighting and Detail tabs with **Original**, **Balanced** and **Performance** presets, and every one of them changes live while you play. Version 1.7 adds an **Input** tab with a live mouse-to-screen latency meter, plus engine thread priority, GPU priority and a GPU busy readout in the **Engine** tab. Version 1.8 supports the game's move to Unity 2022.3 (Pixel Gun 3D 26.12.0) and finds its native hooks by name, so later game updates on the same Unity version keep working. Pick a preset or tune options, press **Play**, and watch live engine FPS, Metal FPS and what Unity is actually using for each option.

Everything happens in the running game's memory. Game files, saved game settings and your Steam install are never modified; quit the game and start it from Steam to return to normal.

## Download and install

1. Download `PG3D-FPS-Unlock-1.8.0-macOS.zip` from the [latest release](../../releases/latest) and unzip it.
2. Move **PG3D FPS Unlock.app** to Applications (or ~/Applications).
3. The app is not notarized. The first time, Control-click it and choose **Open**, or allow it under System Settings → Privacy & Security.
4. Open Steam and sign in, then open the app, choose a preset (or your own mix of options) and press **Play**.

Requirements: macOS 13 or later, Pixel Gun 3D PC Edition from Steam, and Rosetta on Apple Silicon (the game itself runs as x86_64). Reviewed game builds: **26.11.0 (151027)**, **26.11.3 (155325)** and **26.12.0 (156210, Unity 2022.3)**. Since 1.8, a newer game build still launches when it uses a Unity version the app knows (2021.3 or 2022.3): the unlocker finds every native binding by name, validates it, and leaves alone anything it can't verify. A build on any other Unity version is refused until it has been reviewed.

## What it does

**FPS unlock.** Keeps Unity's `targetFrameRate` at uncapped (or your cap) and `vSyncCount` at 0, reapplies them if the game resets them, and disables Unity's 60 Hz display-link blit path (`UNITY_DISPLAYLINK_BLIT=0`). The Steam overlay keeps working.

**Graphics options (new in 1.6).** Pixel Gun ships one quality level with 150 m shadows in 4 cascades, 4 per-pixel lights, forced anisotropic filtering, a 4096-ray particle budget and no LOD simplification, plus a post-processing stack, and none of it is adjustable in the game. The app adds these options:

| Tab | Option | Choices |
| --- | --- | --- |
| Effects | Post-processing (whole stack, plus the depth and HDR passes it needs) | Game, Off |
| | Ambient occlusion, Bloom, Color grading, Auto exposure, Lens effects, Depth of field & motion blur | Game, Off |
| | Depth pre-pass (camera depth texture) | Game, Off |
| | Distance fog | Game, Off |
| Lighting | Real-time shadows | Game, Hard only, Off |
| | Shadow quality | Game, Medium (25 m, 2 cascades), Low (15 m, 1 cascade) |
| | Per-pixel lights | Game, 2, 1, 0 |
| | Real-time reflections (probes and screen-space) | Game, Off |
| Detail | Anti-aliasing (MSAA) | Game, 2x, Off |
| | Anisotropic filtering | Game, Per texture, Off |
| | Model detail (LOD bias; the game uses 100) | Game, 2.0, 1.0, 0.6, 0.3 |
| | Texture resolution | Game, Half, Quarter |
| | Skinning quality | Game, 4, 2, 1 bones |
| | Soft particles, Soft vegetation | Game, Off |
| | Particle collisions (raycast budget) | Game, 256, 64, 16 |

Every option is a ceiling: a lower setting the game already uses is kept, and **Game** restores the game's own value. Values are read back from Unity after every change; a value Unity refuses is restored and left alone. The individual effects switch off single effects in the game's post-processing profiles, so the rest of the stack keeps running.

Presets: **Original** changes nothing but the frame rate. **Balanced** keeps the look (color grading, auto exposure, shadows) and trims shadow distance to 25 m with 2 cascades, 2 per-pixel lights, 2x MSAA, per-texture anisotropic filtering, LOD bias 1, 4-bone skinning, a 256-ray particle budget, and turns off ambient occlusion, bloom, lens effects, depth of field and motion blur. **Performance** also turns off post-processing, the depth pre-pass, real-time shadows, reflections, MSAA, anisotropic filtering, soft particles and soft vegetation, with 1 per-pixel light, LOD bias 0.6, 2-bone skinning and a 64-ray particle budget. Changing any option shows **Custom**.

**Live changes.** While the game runs, any option, preset or the frame-rate cap applies within a second, and each option shows what the game wanted and what Unity is using now. Settings stay consistent through death, respawn and map loads: they are enforced just before Unity renders each frame.

**Fast mouse look.** Because the game runs under Rosetta, macOS's own window code for every mouse move runs translated too. While you're aiming (cursor locked), each move goes straight to Unity's input handler instead of through macOS's window bookkeeping: about 10 µs per move instead of about 50. Menus, clicks, keys and a free cursor keep the normal route. It stays off, and the log says why, if another library has hooked any step of that route. Can be switched mid-match.

**Job worker threads.** Unity starts one worker thread per CPU core minus one. On Macs with many cores, waking those workers cost about 1 ms of every frame in profiling, and more while turning, when Unity syncs moved colliders before each of the game's raycasts. This setting starts the game with Unity's own `-job-worker-count`. **4** worked well in testing: steadier FPS and noticeably better mouse input. Don't combine it with **Multithreaded rendering**; that pairing made the screen flicker, and the app warns about it.

**Multithreaded rendering** (experimental) starts the game with Unity's `-force-gfx-jobs native`. It showed no gain in testing. Job workers, multithreaded rendering and the Metal HUD are in the **Engine** tab and apply at the next launch; the frame-rate cap and fast mouse look change live.

**Input latency (new in 1.7).** The Input tab shows how long a mouse move takes to reach the screen: from the move's own hardware timestamp to the moment Metal reports the frame that used it on screen, as a median and a 95th percentile. It also shows the part after the game has read the mouse (rendering to screen), which needs no mouse movement, and whether macOS pointer acceleration is off. Nothing needs changing for mouse precision itself: Unity hands the game macOS's own decimal mouse movement, and although macOS merges 1000 Hz reports when several arrive within one frame, the merged movement is exact (tested: 400 of 400 counts arrive).

**Engine thread priority (new in 1.7).** Unity runs its job workers at its "normal" thread priority, which it maps to 25, below macOS's default of 31. When other apps keep the CPU busy, those workers start late or get preempted while the main thread waits for their jobs (for example the collider sync before every raycast). **High** raises the job workers and Unity's render thread to 45, Unity's own highest level; background loading, audio and other threads keep theirs. Changes live and is read back; **Game** restores Unity's values. In the lobby with other programs keeping the CPU busy it gave 4% more FPS and 7% shorter 95th/99th-percentile frame times; on an idle Mac it changes nothing.

**GPU priority (new in 1.7).** Metal command queues on Apple GPUs carry a scheduling priority. **High** gives the game's queues the high level, so its frames are scheduled ahead of other apps' GPU work (a browser video, a Discord stream). Changes live; only the high level or the queue's own original value is ever set. In the lobby with another program keeping the GPU 72% busy it gave 27% more FPS, frames reached the screen 420 times a second instead of 270, and the 95th-percentile frame time fell from 13 to under 5 ms; on an idle Mac it changes nothing. The Engine tab also shows how busy the GPU is for the whole Mac.

A two-frame drawable queue was tried for lower latency during 1.7 development and removed: in the lobby it cost 28% FPS and added about 1.5 ms of latency.

**Frame profiler.** Times every Unity player-loop stage and compares frames with and without mouse movement, so the log shows what gets slower while you turn. `python3 tools/mouse_profile.py` summarizes the latest run.

What it never does: hide objects, change camera clipping or culling masks, lower the screen resolution, change physics timing, touch networking, or patch game files. Texture resolution is only lowered if you pick Half or Quarter.

## Command line

The app bundles the same backend used from the terminal. From a source checkout:

```sh
./pg3d-fps-unlock --profile performance
./pg3d-fps-unlock --profile performance --mouse fast --job-workers 4
./pg3d-fps-unlock --profile balanced --option shadows=0,bloom=game
./pg3d-fps-unlock --profile performance --option engine-threads=1,gpu-priority=1
./pg3d-fps-unlock --profile original --fps 240
./pg3d-fps-unlock --list-options              # every option, its choices and the presets
./pg3d-fps-unlock --set-profile balanced      # switch preset in the running game
./pg3d-fps-unlock --set fog=0,lod=30          # change options in the running game
./pg3d-fps-unlock --set-fps 240               # change the frame-rate cap in the running game
./pg3d-fps-unlock --set-mouse fast            # or game
./pg3d-fps-unlock --probe                     # log what the current scene renders with
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
| `backend/options.tsv` | The graphics, input and engine options, their choices and the three presets (read by the script and the app) |
| `backend/src/` | Injected x86_64 library (FPS unlock, graphics options, post-processing effects, frame guard, fast mouse look, latency meter, thread and GPU priority, profiler, scene probe) and the launch helper |
| `backend/tests/` | Unit tests |
| `tools/` | Game-update binding check (`check_bindings.sh`, run it first after a game update; it never launches the game), measurement helpers (live option A/B benchmark, CPU/GPU load generators, profile comparison, paired camera traces, symbolication, mouse profile summary) |

## Notes

- Pixel Gun 3D is a signed third-party game. Use of modifications may be governed by its terms or anti-cheat policy; use this only where permitted.
- Not affiliated with Pixel Gun 3D or Valve.

## Author's note
vibecoded as hell.
