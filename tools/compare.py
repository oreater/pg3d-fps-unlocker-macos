#!/usr/bin/env python3
"""Compare profiles in an already running, stationary foreground scene.
Do not move the camera or interact during collection. Restores the starting
profile on exit. A scene-specific check, not a general gameplay benchmark.
"""
import argparse, datetime, json, os, re, signal, statistics, subprocess, time
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--seconds', type=int, default=30)
args = parser.parse_args()
if args.seconds < 25:
    parser.error('collect at least 25 seconds per phase')
root = Path.home() / 'Library/Logs/OptimizerUnlocker'
log = Path((root / 'latest').read_text().strip())
if log.parent != root or not log.name.startswith('run-') or log.suffix != '.log':
    raise SystemExit('Invalid session record')
pid = int(Path(str(log) + '.pid').read_text())
command = subprocess.check_output(['ps', '-p', str(pid), '-o', 'comm='], text=True).strip()
if not command.endswith('/Contents/MacOS/Pixel Gun 3D'):
    raise SystemExit('Recorded game is no longer running')
control = Path(str(log) + '.profile')
original = control.read_text().strip()
if original not in ('original', 'balanced', 'performance'):
    raise SystemExit('Invalid starting profile')

def set_profile(name):
    temp = control.with_name(control.name + f'.compare-{os.getpid()}')
    temp.write_text(name + '\n')
    temp.replace(control)

def interrupted(*_):
    raise KeyboardInterrupt()

signal.signal(signal.SIGTERM, interrupted)
report = {'started': datetime.datetime.now().astimezone().isoformat(), 'log': str(log),
          'pid': pid, 'scene': 'User must keep the same scene and camera throughout.',
          'starting_profile': original, 'phases': []}
profile = original
engine = re.compile(r'rendered=([\d.]+) FPS.*window=([\d.]+)s.*target=(-?\d+), vSyncCount=(\d+), focused=(yes|no)')
try:
    for profile in ('original', 'balanced', 'performance', 'original'):
        set_profile(profile)
        # Cover profile polling (5 seconds) plus the interval straddling a switch.
        time.sleep(12)
        if control.read_text().strip() != profile:
            raise RuntimeError('Profile changed externally; comparison interrupted')
        offset = log.stat().st_size
        time.sleep(args.seconds)
        os.kill(pid, 0)
        if control.read_text().strip() != profile:
            raise RuntimeError('Profile changed externally; comparison interrupted')
        with log.open('rb') as stream:
            stream.seek(offset)
            raw = stream.read().decode(errors='replace')
        samples, pending = [], None
        for line in raw.splitlines():
            match = engine.search(line)
            if match:
                pending = {'fps': float(match[1]), 'seconds': float(match[2]),
                           'target': int(match[3]), 'vsync': int(match[4]),
                           'focused': match[5] == 'yes'}
            elif pending is not None and 'optimizer status:' in line:
                pending['verified'] = (f'profile={profile};' in line and re.search(r'verified=(\d+)/\1;', line) is not None and 'failed=0;' in line)
            elif pending is not None and 'optimizer camera audit:' in line:
                pending['camera'] = line.split('optimizer camera audit: ', 1)[-1]
            elif pending is not None and 'presentation sample:' in line:
                match = re.search(r'presented_fps=([\d.]+)', line)
                pending['presented_fps'] = float(match[1]) if match else None
                samples.append(pending)
                pending = None
        valid = [x for x in samples if x['focused'] and x.get('verified') and x['target'] == -1 and x['vsync'] == 0]
        phase = {'profile': profile, 'samples': samples, 'valid_samples': len(valid),
                 'median_engine_fps': statistics.median(x['fps'] for x in valid) if valid else None,
                 'median_presented_fps': statistics.median(x['presented_fps'] for x in valid) if valid else None,
                 'raw': raw}
        report['phases'].append(phase)
        print(f"{profile}: {len(valid)}/{len(samples)} usable samples; median engine FPS={phase['median_engine_fps']}", flush=True)
except (KeyboardInterrupt, Exception) as error:
    report['error'] = str(error) or 'Interrupted'
finally:
    # Avoid overwriting a deliberate user profile change while we were measuring.
    if control.read_text().strip() == profile:
        set_profile(original)
    report['finished'] = datetime.datetime.now().astimezone().isoformat()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f'Saved {args.output}', flush=True)
