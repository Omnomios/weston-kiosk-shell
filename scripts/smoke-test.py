#!/usr/bin/env python3
"""Verify shell loading and clean shutdown in an isolated headless Weston."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix='weston-kiosk-test-') as runtime:
    log = Path(runtime) / 'weston.log'
    result = subprocess.run(
        [sys.argv[1], '--backend=headless', '--renderer=pixman',
         '--no-config', '--idle-time=0', '--socket=kiosk-test',
         f'--shell={sys.argv[2]}', f'--log={log}', '--', '/usr/bin/true'],
        env={**os.environ, 'XDG_RUNTIME_DIR': runtime},
        timeout=10,
    )
    output = log.read_text() if log.exists() else ''
    print(output)
    if result.returncode != 0:
        sys.exit(result.returncode)
    if f"Loading module '{sys.argv[2]}'" not in output:
        sys.exit('Weston did not load the requested shell')
