"""The real recorder lock controls interception without changing live services."""
import fcntl
import os
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='voice-lock-') as tmp:
    root = Path(tmp)
    (root / 'voice-input').mkdir()
    path = root / 'voice-input/recorder.lock'
    env = dict(os.environ, XDG_RUNTIME_DIR=str(root), VOICE_TEST_REAL_LOCK='1', LD_PRELOAD=str(here / 'shared_bridge_test.so'))
    def run(disabled):
        target = dict(env)
        if disabled:
            target['VOICE_DISABLED_TEST']='1'
        subprocess.run([str(here / 'test_hotkey')], env=target, check=True)
    run(True)  # Missing lock file: do not intercept.
    with path.open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        run(False)
    run(True)  # Stale unlocked file: do not intercept.
