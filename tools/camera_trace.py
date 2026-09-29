#!/usr/bin/env python3
"""Record paired, user-controlled camera traces without switching app focus.

Run only after the user agrees to remain in one loaded map. Audio cues delimit
stationary and turning phases. Samples add overhead to both phases. No settings
or input are changed. A valid capture still needs the user's scene confirmation.
"""
import argparse
import datetime
import json
import os
from pathlib import Path
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = Path.home() / 'Library/Logs/OptimizerUnlocker'
log = Path((root / 'latest').read_text().strip())
if log.parent != root or not log.name.startswith('run-') or log.suffix != '.log':
    raise SystemExit('Invalid session record')
pid = int(Path(str(log) + '.pid').read_text())
control = Path(str(log) + '.profile')
profile = control.read_text().strip()

def check_session():
    command = subprocess.check_output(['ps', '-p', str(pid), '-o', 'comm='], text=True).strip()
    if not command.endswith('/Contents/MacOS/Pixel Gun 3D'):
        raise RuntimeError('Recorded game is no longer running')
    if Path((root / 'latest').read_text().strip()) != log or control.read_text().strip() != profile:
        raise RuntimeError('Session or profile changed during capture')

check_session()
args.output.mkdir(parents=True, exist_ok=False)
report = {'pid': pid, 'log': str(log), 'profile': profile, 'phases': [],
          'started': datetime.datetime.now().astimezone().isoformat(),
          'limitations': 'Requires the same loaded map throughout. Audio cues and sampling add overhead. No automatic camera-motion verification.'}
try:
    # Start from a fresh foreground report, rather than guessing how quickly
    # the user can return from the assistant window. Never manipulate focus.
    deadline = time.monotonic() + 180
    while True:
        check_session()
        lines = log.read_text(errors='replace').splitlines()
        latest = next((line for line in reversed(lines) if 'engine measurement:' in line), '')
        if time.time() - log.stat().st_mtime < 8 and 'focused=yes' in latest:
            break
        if time.monotonic() >= deadline:
            raise RuntimeError('Timed out waiting for the game to become foreground')
        time.sleep(1)
    subprocess.run(['/usr/bin/say', 'Hold still'], check=True)
    time.sleep(7)
    for phase in ('stationary', 'turning'):
        if phase == 'turning':
            subprocess.run(['/usr/bin/say', 'Turn'], check=True)
            time.sleep(3)
        check_session()
        offset = log.stat().st_size
        started = datetime.datetime.now().astimezone().isoformat()
        trace = args.output / (phase + '.sample.txt')
        result = subprocess.run(['/usr/bin/sample', str(pid), '12', '4', '-file', str(trace)],
                                capture_output=True, text=True)
        check_session()
        with log.open('rb') as stream:
            stream.seek(offset)
            telemetry = stream.read().decode(errors='replace')
        (args.output / (phase + '.telemetry.log')).write_text(telemetry)
        # The first five-second report may straddle the phase boundary.
        measurements = [line for line in telemetry.splitlines() if 'engine measurement:' in line][1:]
        reasons = []
        if result.returncode: reasons.append('sample failed: ' + result.stderr)
        if not measurements: reasons.append('no complete telemetry interval')
        if any('focused=yes' not in line for line in measurements): reasons.append('game lost focus')
        if 'optimizer camera audit: no main camera' in telemetry: reasons.append('no main camera')
        if 'optimizer camera audit: main=' not in telemetry: reasons.append('missing camera audit')
        report['phases'].append({'phase': phase, 'started': started,
                                 'finished': datetime.datetime.now().astimezone().isoformat(),
                                 'complete_intervals': measurements, 'invalid_reasons': reasons})
        if reasons: raise RuntimeError('; '.join(reasons))
except (Exception, KeyboardInterrupt) as error:
    report['error'] = str(error) or 'Interrupted'
finally:
    subprocess.run(['/usr/bin/say', 'Recording stopped' if 'error' in report else 'Done'], check=False)
    report['finished'] = datetime.datetime.now().astimezone().isoformat()
    (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2), flush=True)
