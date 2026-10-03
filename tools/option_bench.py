#!/usr/bin/env python3
"""A/B options in the running game (started by PG3D FPS Unlock 1.6+).

Each comparison alternates two settings for a few rounds and reports the median
engine FPS of each; with 1.7+ also the frame-time 95th/99th percentiles and the
render-to-screen latency. The optimizer logs these every five seconds; the window
in progress at a change is skipped so it never mixes the two settings.

  tools/option_bench.py 'postfx=game' 'postfx=0'
  tools/option_bench.py --preset original --preset performance
  tools/option_bench.py --rounds 3 'ambient-occlusion=game' 'ambient-occlusion=0'
  tools/option_bench.py 'engine-threads=game' 'engine-threads=1'

A side is either KEY=VALUE[,KEY=VALUE...] (applied with --set on top of the
current options) or --preset NAME. The game is left on side A afterwards. Stops early if the game loses focus (the
Mac is in use), since background frames are throttled and not comparable.
"""
import argparse
import os
import re
import statistics
import subprocess
import sys
import time

LOGS = os.path.expanduser("~/Library/Logs/OptimizerUnlocker")
HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_LAUNCHER = os.path.join(os.path.dirname(HERE), "backend", "testificateunlocker")
MEASURE = re.compile(r"engine measurement: rendered=([\d.]+) FPS.*focused=(yes|no)")
PACING = re.compile(r"frame pacing: frames=\d+; frame_ms median=[\d.]+ p95=([\d.]+) p99=([\d.]+)")
LATENCY = re.compile(r"input latency: frames=\d+; render_to_screen_ms median=([\d.]+)")


def latest_log():
    with open(os.path.join(LOGS, "latest")) as f:
        return f.read().strip()


def measurements(log):
    """One record per five-second window: FPS, focus, and the pacing/latency lines that follow it."""
    windows = []
    with open(log, errors="replace") as f:
        for line in f:
            if m := MEASURE.search(line):
                windows.append({"fps": float(m.group(1)), "focused": m.group(2) == "yes"})
            elif windows and (m := PACING.search(line)):
                windows[-1]["p95"], windows[-1]["p99"] = float(m.group(1)), float(m.group(2))
            elif windows and (m := LATENCY.search(line)):
                windows[-1]["render"] = float(m.group(1))
    return windows


def apply(launcher, side):
    kind, value = side
    args = ["--set-profile", value] if kind == "preset" else ["--set", value]
    subprocess.run(["/bin/zsh", launcher, *args], check=True, stdout=subprocess.DEVNULL)


def collect(log, windows, timeout=60):
    """Skip the window in progress at the change, then return `windows` full samples."""
    start = len(measurements(log))
    deadline = time.time() + timeout
    while time.time() < deadline:
        # The newest window's pacing lines may still be on their way: wait for one more.
        samples = measurements(log)[start + 1:-1]
        if len(samples) >= windows:
            return samples[:windows]
        time.sleep(0.5)
    raise SystemExit("no engine measurements; is the game still running?")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("sides", nargs="*", help="KEY=VALUE[,KEY=VALUE...] for side A and side B")
    parser.add_argument("--preset", action="append", default=[], help="a preset as one side")
    parser.add_argument("--rounds", type=int, default=2)
    parser.add_argument("--windows", type=int, default=2, help="five-second windows per phase")
    parser.add_argument("--launcher", default=DEFAULT_LAUNCHER)
    args = parser.parse_args()
    sides = [("preset", p) for p in args.preset] + [("set", s) for s in args.sides]
    if len(sides) != 2:
        parser.error("give exactly two sides")
    log = latest_log()
    results = {0: [], 1: []}
    for _ in range(args.rounds):
        for index, side in enumerate(sides):
            apply(args.launcher, side)
            samples = collect(log, args.windows)
            if not all(sample["focused"] for sample in samples):
                raise SystemExit("the game lost focus (the Mac is in use); stopping without a result")
            results[index] += samples
    apply(args.launcher, sides[0])  # leave the game on side A, so comparisons can be chained
    label = lambda side: side[1] if side[0] == "set" else f"preset {side[1]}"
    fps = [[w["fps"] for w in results[i]] for i in (0, 1)]
    a, b = statistics.median(fps[0]), statistics.median(fps[1])
    print(f"{label(sides[0])}: {a:.1f} FPS  |  {label(sides[1])}: {b:.1f} FPS  |  {100 * (b - a) / a:+.1f}%"
          f"  (median of {len(fps[0])} windows each; A {min(fps[0]):.0f}-{max(fps[0]):.0f},"
          f" B {min(fps[1]):.0f}-{max(fps[1]):.0f})")
    for key, name in (("p95", "frame time p95 ms"), ("p99", "frame time p99 ms"), ("render", "render-to-screen ms")):
        values = [[w[key] for w in results[i] if key in w] for i in (0, 1)]
        if values[0] and values[1]:
            print(f"  {name}: A {statistics.median(values[0]):.3f}  |  B {statistics.median(values[1]):.3f}")


if __name__ == "__main__":
    sys.exit(main())
