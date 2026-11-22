#!/usr/bin/env python3
"""Exercise migration and failure handling without touching the real session."""
from pathlib import Path
import os
import subprocess
import tempfile

script = Path(__file__).with_name('setup-user').resolve()
remove = Path(__file__).with_name('remove-user').resolve()
with tempfile.TemporaryDirectory(prefix='voice-setup-test-') as tmp:
    root = Path(tmp)
    home = root / 'home'
    units = home / '.config/systemd/user'
    runtime = root / 'runtime'
    bindir = root / 'bin'
    for p in (units / 'APPLaunch.service.d', runtime, bindir):
        p.mkdir(parents=True, exist_ok=True)
    fake = bindir / 'systemctl'
    fake.write_text('''#!/bin/sh
printf '%s\\n' "$*" >> "$HOME/calls"
if [ "$2" = show ]; then printf '%s\\n' "$FAKE_ENV"; exit 0; fi
if [ "$2" = daemon-reload ] && [ "$FAIL_RELOAD" = 1 ]; then exit 1; fi
exit 0
''')
    fake.chmod(0o755)
    timer = bindir / 'systemd-run'
    timer.write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> "$HOME/timers"\n')
    timer.chmod(0o755)
    env = dict(os.environ, HOME=str(home), XDG_RUNTIME_DIR=str(runtime), PATH=str(bindir)+os.pathsep+os.environ['PATH'], FAKE_ENV='LD_PRELOAD='+str(home)+'/.local/share/voice-input/shared_bridge.so')
    legacy = home / '.config/cardputer-voice'
    legacy.mkdir(parents=True)
    (legacy / 'openai-api-key').write_text('sk-test-legacy')
    (units / 'voice-input.service').write_text('old voice')
    (units / 'voice-input-text.service').write_text('old text')
    old = units / 'APPLaunch.service.d/90-voice-input-test.conf'
    old.write_text('old preload')
    # A reload failure restores the prior configuration.
    failed = subprocess.run([str(script)], env=dict(env, FAIL_RELOAD='1'), capture_output=True)
    assert failed.returncode != 0
    assert (units / 'voice-input.service').read_text() == 'old voice'
    assert old.read_text() == 'old preload'
    assert not (units / 'APPLaunch.service.d/90-voice-input-store.conf').exists()
    # An unrelated preload is never overwritten.
    assert subprocess.run([str(script)], env=dict(env, FAKE_ENV='LD_PRELOAD=/other.so'), capture_output=True).returncode != 0
    assert old.read_text() == 'old preload'
    assert subprocess.run([str(script)], env=env, capture_output=True).returncode == 0
    assert (units / 'voice-input.service').is_symlink() and not old.exists()
    assert '/usr/lib/voice-input/shared_bridge.so' in (units / 'APPLaunch.service.d/90-voice-input-store.conf').read_text()
    backups = list((home / '.local/share/voice-input').glob('store-backup.*'))
    assert any((b / 'voice-input.service').is_file() for b in backups)
    assert (runtime / 'voice-input/launcher-restart-needed').exists()
    before = (home / 'calls').read_text()
    assert subprocess.run([str(script)], env=env, capture_output=True).returncode == 0
    assert (home / 'calls').read_text() == before
    assert (home / '.config/voice-input/openai-api-key').read_text() == 'sk-test-legacy'
    assert (home / '.config/voice-input/openai-api-key').stat().st_mode & 0o777 == 0o600
    key = home / '.config/voice-input/deepseek-api-key'
    key.parent.mkdir(parents=True, exist_ok=True)
    key.write_text('fake-key')
    dropin = units / 'APPLaunch.service.d/90-voice-input-store.conf'
    correct = dropin.read_text()
    dropin.write_text(correct + '# changed by user\n')
    assert subprocess.run([str(script)], env=env, capture_output=True).returncode != 0
    assert subprocess.run([str(remove)], env=env, capture_output=True).returncode != 0
    assert dropin.exists()
    dropin.write_text(correct)
    assert subprocess.run([str(remove)], env=env, capture_output=True).returncode == 0
    assert not dropin.exists() and not (units / 'voice-input.service').is_symlink() and key.read_text() == 'fake-key'
    assert 'disable --now voice-input.service voice-input-text.service' in (home / 'calls').read_text()
    assert 'restart APPLaunch.service' in (home / 'timers').read_text()
print('Per-user setup/removal: migration, conflict, rollback, repeated setup and key preservation passed')
