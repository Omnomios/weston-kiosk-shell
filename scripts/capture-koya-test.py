#!/usr/bin/env python3
"""Start the nested Koya fixture, capture its output, then clean up the session."""
import os
from pathlib import Path
import signal
import subprocess
import time

root = Path(__file__).resolve().parent.parent
output = root / 'build/screenshots'
output.mkdir(parents=True, exist_ok=True)
log = root / 'build/koya-capture.log'
previous = set(output.glob('*.png'))
with log.open('w') as stream:
    proc = subprocess.Popen(
        ['stdbuf', '-oL', str(root / 'scripts/test-koya.sh')], cwd=root,
        stdout=stream, stderr=subprocess.STDOUT,
        env={**os.environ, 'WESTON_DEBUG': '1'}, start_new_session=True,
    )
    try:
        socket = Path(os.environ['XDG_RUNTIME_DIR']) / f'weston-kiosk-{proc.pid}'
        for _ in range(150):
            if proc.poll() is not None:
                raise RuntimeError('Test compositor exited; see ' + str(log))
            if socket.exists() and '[layer-shell-test] READY' in log.read_text():
                break
            time.sleep(0.1)
        else:
            raise RuntimeError('Fixture startup timed out; see ' + str(log))
        for role in ('background', 'panel', 'overlay'):
            if f'[layer-shell-test] {role}: CREATED' not in log.read_text():
                raise RuntimeError(role + ' creation failed; see ' + str(log))
        time.sleep(0.5)
        subprocess.run(
            ['weston-screenshooter'], cwd=root, check=True, timeout=10,
            env={**os.environ, 'WAYLAND_DISPLAY': socket.name, 'XDG_PICTURES_DIR': str(output)},
        )
        captures = set(output.glob('*.png')) - previous
        if not captures:
            raise RuntimeError('No screenshot was written')
        for path in sorted(captures):
            print(path)
    finally:
        try:
            os.killpg(proc.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        proc.wait(timeout=5)
