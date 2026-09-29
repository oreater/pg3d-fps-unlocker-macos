#!/usr/bin/env python3
"""Summarize mouse-motion cost from an Optimizer Unlocker run log.

Reads the frame profiler ("frame profile" lines: frames with mouse motion vs
frames without, per player-loop stage) and the input probe ("input sample"
lines, including fast mouse look counts). Five-second windows are grouped by
whether fast mouse look handled most motion in that window.

    python3 tools/mouse_profile.py [~/Library/Logs/OptimizerUnlocker/run-….log]
"""
import re
import sys
from pathlib import Path

NUM = r'([-+]?[0-9.]+)'


def value(line, key):
    match = re.search(re.escape(key) + NUM, line)
    return float(match.group(1)) if match else None


def main():
    if len(sys.argv) > 1:
        path = Path(sys.argv[1]).expanduser()
    else:
        logs = Path.home() / 'Library/Logs/OptimizerUnlocker'
        path = Path((logs / 'latest').read_text().strip())
    lines = path.read_text(errors='replace').splitlines()
    groups = {'fast': [], 'game': []}
    engine = None
    last_input = None
    for line in lines:
        if 'engine measurement:' in line:
            engine = line
        elif 'input sample:' in line:
            last_input = line
        elif re.search(r'\] frame profile: ', line) and engine and last_input:
            counts = re.search(r'moving/still/other=(\d+)/(\d+)/(\d+)', line)
            motion = value(last_input, 'motion_events=') or 0
            fast = value(last_input, 'fast_events=') or 0
            focused = 'focused=yes' in engine
            if not counts or not focused or motion == 0:
                continue
            key = 'fast' if fast / motion > 0.5 else 'game'
            groups[key].append({
                'fps': value(engine, 'rendered='),
                'moving_frames': int(counts.group(1)), 'still_frames': int(counts.group(2)),
                'frame_moving': value(line, 'frame_ms moving='), 'frame_still': value(line, ' still='),
                'between_moving': value(line, 'between_frames_ms moving='),
                'send_us': value(last_input, 'sendEvent_mean_us='),
            })
    print(f'Log: {path}')
    for key, rows in groups.items():
        if not rows:
            print(f'{key}: no focused windows with mouse motion')
            continue
        frames = sum(r['moving_frames'] for r in rows)
        weighted = lambda k: sum((r[k] or 0) * r['moving_frames'] for r in rows) / max(frames, 1)
        print(f"{key}: {len(rows)} windows; engine FPS mean {sum(r['fps'] or 0 for r in rows) / len(rows):.0f}; "
              f"moving-frame ms {weighted('frame_moving'):.3f} (between frames {weighted('between_moving'):.3f}); "
              f"motion dispatch {weighted('send_us'):.1f} µs/event")
    session = [line for line in lines if 'frame profile session' in line]
    if session:
        print('Latest whole-session comparison:')
        for line in session[-2:]:
            print('  ' + line.split('] ', 1)[-1])
    else:
        print('No whole-session profile yet (written every 60 s once both still and moving frames exist).')


if __name__ == '__main__':
    main()
