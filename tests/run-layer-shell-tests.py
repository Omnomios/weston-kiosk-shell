#!/usr/bin/env python3
"""Run real layer-shell/XDG clients against an isolated headless compositor."""
import os
import select
from pathlib import Path
import subprocess
import sys
import tempfile
import time

with tempfile.TemporaryDirectory(prefix='kiosk-layer-tests-') as runtime:
    env = {**os.environ, 'XDG_RUNTIME_DIR': runtime, 'WAYLAND_DISPLAY': 'layer-test'}
    log = Path(runtime) / 'weston.log'
    proc = subprocess.Popen([
        sys.argv[1], '--backend=headless', '--renderer=pixman', '--fake-seat',
        '--width=800', '--height=600', '--no-config', '--idle-time=0',
        '--socket=layer-test', f'--shell={sys.argv[2]}', f'--log={log}',
    ], env=env)
    live_client = None
    try:
        for _ in range(100):
            if proc.poll() is not None:
                raise RuntimeError('Weston exited during startup')
            if (Path(runtime) / 'layer-test').exists():
                break
            time.sleep(0.02)
        else:
            raise RuntimeError('Weston startup timed out')
        cases = [None, 'already-committed', 'bad-anchor', 'bad-size', 'bad-keyboard',
                 'bad-serial', 'early-buffer', 'unacked-buffer', 'role-conflict']
        for case in cases:
            subprocess.run([sys.argv[3]] + ([case] if case else []), env=env, check=True, timeout=15)
            if proc.poll() is not None:
                raise RuntimeError('Client test crashed the compositor')
        # Shutdown with mapped surfaces and live role resources, rather than
        # only testing compositor destruction after all clients disconnected.
        live_client = subprocess.Popen([sys.argv[3], 'hold'], env=env,
                                       stdout=subprocess.PIPE, text=True)
        ready, _, _ = select.select([live_client.stdout], [], [], 15)
        if not ready or not live_client.stdout.readline().startswith('READY:'):
            raise RuntimeError('Live shutdown client did not become ready')
    except BaseException:
        print(log.read_text() if log.exists() else 'No Weston log', file=sys.stderr)
        raise
    finally:
        if proc.poll() is None:
            proc.terminate()
        proc.wait(timeout=5)
        if live_client is not None:
            if live_client.poll() is None:
                try:
                    live_client.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    live_client.kill()
                    live_client.wait()
    if proc.returncode != 0:
        print(log.read_text(), file=sys.stderr)
        raise RuntimeError(f'Weston shutdown failed: {proc.returncode}')
