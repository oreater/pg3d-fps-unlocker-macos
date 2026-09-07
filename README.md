# PG3D FPS Unlocker for macOS

This tool removes the observed 60 FPS Metal presentation bottleneck in the Steam macOS build of Pixel Gun 3D. It disables Unity's native display-link blit path with **`UNITY_DISPLAYLINK_BLIT=0` before startup**, then injects an x86_64 library to maintain `Application.targetFrameRate = -1` and `QualitySettings.vSyncCount = 0`. It records engine and Metal presentation measurements separately. It does not patch installed game files or save changes to game preferences.

## Requirements

- Pixel Gun 3D PC Edition for Steam, installed locally.
- Rosetta 2 (the current macOS game binary is x86_64; an installed game already normally has this).
- Xcode Command Line Tools for the one-time local build.

## Build and launch

Keep Steam open and quit any existing game instance, then run:

```zsh
./pg3d-fps-unlock
```

Use this launcher each time. Starting the game normally from Steam does not apply the fix. No extra environment-variable command is needed.

The launcher builds automatically if its library is missing or its source changed. `./build.sh` can also be run manually; it builds and signs a temporary library, then replaces the output atomically so an existing game keeps its original mapped library.

To request a specific frame-rate ceiling:

```zsh
./pg3d-fps-unlock --fps 240
```

The launcher waits about 20 seconds for the library to confirm that its runtime overrides are active. A launch PID or a loaded library alone does not count as success. If startup takes longer, the game continues running and the launcher reports that verification is still pending.

## Verify the result

Once the game has loaded and its window is in the foreground, run:

```zsh
./pg3d-fps-unlock --status
```

Status identifies whether the latest run is still alive and prints its recent measurements:

- `engine measurement`: Unity's frame counters, which can advance faster than Metal presentation.
- `presentation`: actual Metal drawable acquisitions and unique nonzero presentation timestamps, plus skipped/duplicate events.
- `presentation screen`: the game window's display refresh capabilities and mode.

For Apple's on-screen Metal performance overlay, launch with:

```zsh
./pg3d-fps-unlock --hud
```

The requested target is a ceiling, not a guarantee: loading, GPU/CPU workload, background throttling, and display presentation may limit the observed result. Compare the same scene and foreground state when measuring changes. With display synchronization disabled, Metal may submit/present more frames than the panel can fully display; neither engine FPS nor presentation timestamps prove that every frame was scanned out in full. The tested MacBook panel has a maximum refresh rate of 120 Hz.

To measure the original behavior, quit the game and launch an observation run:

```zsh
./pg3d-fps-unlock --observe
./pg3d-fps-unlock --status
```

Observation mode clears `UNITY_DISPLAYLINK_BLIT` from the child environment and records the game's original target, v-sync setting, and presentation behavior without applying pacing overrides. Quit that run before starting the normal unlocker.

Each run has a dedicated log under `~/Library/Logs/PG3DFPSUnlocker/`. The library writes directly to this file because Unity redirects standard error after startup. The `latest` record identifies the most recent run; its adjacent `.pid` file identifies the launched process. Previous measurements remain in their own run logs.

## Options

```text
--fps N          N is 30–1000. Use 0 or "uncapped" for no cap (the default).
--observe        Measure original pacing without applying overrides.
--hud            Show Apple's Metal performance overlay for this run.
--game PATH      Pixel Gun 3D.app location, when it is not in Steam's default library.
--dry-run        Check the installation and library without launching the game.
--status         Show the latest run's process state and measurements.
```

The launcher refuses another live game instance belonging to your user. It launches the executable directly with Steam's app ID and the injection environment, and detaches the game from the invoking shell.

The native `launch-game` helper creates a separate process session before starting the game. This keeps the game alive when the invoking terminal command finishes; merely backgrounding it was insufficient in the test environment.

## Build-specific guard

The implementation targets Pixel Gun 3D PC Edition **26.11.0 (build 151027)**, Unity 2021.3.43f1. It validates the IL2CPP wrapper instructions and binding strings before redirecting their cached native callbacks. Game updates may require deriving new wrapper locations; a rebuild alone does not establish compatibility.

The first implementation waited for the game to call both setters. In this build, the target-frame-rate callback initialized while the v-sync setter remained unused, so the old tool never completed initialization. The revised library resolves the v-sync binding after Unity initializes, applies both overrides, and reports the effective settings and frame rate. V-sync was already zero in the observed baseline; the cold callback alone did not prove a rendering cap.

## Verified results on this installation

On September 7, 2026, the default Unity display-link path produced **exactly 60 Metal drawable acquisitions/presentations per second** while Unity's engine counter reported approximately 1,044–1,068 FPS. Apple's Metal overlay independently showed about 59–60 FPS. The game window's display mode was already 120 Hz. The original settings were already `targetFrameRate=-1`, `vSyncCount=0`, with `fpsParamKey=-1` saved. Earlier engine-counter-only tests were therefore insufficient to verify a visible unlock.

Launching with `UNITY_DISPLAYLINK_BLIT=0` removed that bottleneck: sampled Metal drawable acquisition rates ranged from approximately **259–818 FPS**, and unique nonzero presentation timestamps from **259–650 FPS**, varying with workload. Some submissions were skipped, as expected when rendering faster than the display. These numbers demonstrate removal of the 60 FPS presentation bottleneck, not a claim that the 120 Hz panel displays hundreds of complete frames per second.

Local evidence: `~/Library/Logs/PG3DFPSUnlocker/run-20260907-113046-67461.log` (default path) and `run-20260907-113256-67642.log` (disabled path). Normal launches now set the tested environment flag automatically.

The rebuilt default launcher was then tested with `./pg3d-fps-unlock --hud`, without a manually supplied environment variable. Apple's Metal overlay showed **608.92 FPS, 1.64 ms frame interval** in the lobby; its log is `run-20260907-114029-68001.log`. This independently corroborates the presentation probe and confirms that the fix is included in the launcher itself.

The game's `fpsParamKey` is an integer and its neighboring Boolean is an initialization flag. The separate native Unity Boolean controlling the display-link blit path was the relevant switch in this test. This environment flag is an undocumented, build-specific Unity implementation detail; recheck after game/engine updates.

## Notes

- This changes rendering pacing only. It does not alter gameplay, networking, player data, or game assets.
- All pacing changes apply only to the launched process. To revert, quit it and start normally through Steam.
- It deliberately starts the game executable directly rather than through `open`, because macOS does not reliably carry `DYLD_INSERT_LIBRARIES` through Launch Services.
- The game is a signed third-party client. Use of modifications may be governed by its terms or anti-cheat policy; use this only where permitted.
