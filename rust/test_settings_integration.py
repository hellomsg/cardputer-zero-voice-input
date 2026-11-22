#!/usr/bin/env python3
"""Exercise settings with a fake user systemd and a disposable home directory."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

binary = Path(os.environ.get('CARDPUTER_SETTINGS_BIN', 'target/release/settings')).resolve()
with tempfile.TemporaryDirectory(prefix='voice-settings-') as tmp:
    root = Path(tmp)
    bindir = root / 'bin'
    bindir.mkdir()
    systemctl = bindir / 'systemctl'
    systemctl.write_text('''#!/bin/sh
printf '%s\\n' "$*" >> "$HOME/systemctl.log"
case "$2" in is-active|is-enabled) exit 1 ;; enable|disable) exit 0 ;; *) exit 2 ;; esac
''')
    systemctl.chmod(0o755)
    env = dict(os.environ, HOME=str(root), PATH=str(bindir) + os.pathsep + os.environ['PATH'])
    def call(*args, data=None):
        return subprocess.run([str(binary), *args], input=data, capture_output=True, env=env)
    assert call('enable').returncode != 0
    saved = call('save-key', data=b'sk-test_example\n')
    assert saved.returncode == 0 and b'sk-test_example' not in saved.stdout + saved.stderr
    key = root / '.config/voice-input/deepseek-api-key'
    assert key.read_bytes() == b'sk-test_example\n' and key.stat().st_mode & 0o777 == 0o600
    assert call('save-key', data=b'bad\ninjected').returncode != 0
    assert key.read_bytes() == b'sk-test_example\n'
    status=call('status').stdout
    assert b'sk-test_example' not in status
    state = json.loads(status)
    assert state['key_hint'] == 'sk-...mple' and state['key_configured'] and not state['voice_active'] and not state['voice_enabled'] and not state['restart_required']
    boot_id = Path('/proc/sys/kernel/random/boot_id')
    if boot_id.exists():
        notice = key.parent / 'restart-required'
        notice.write_text(boot_id.read_text())
        assert json.loads(call('status').stdout)['restart_required']
        notice.write_text('previous-boot')
        assert not json.loads(call('status').stdout)['restart_required'] and not notice.exists()
    assert call('enable').returncode == 0 and call('disable').returncode == 0
    for provider in ('openai',):
        assert call('select-' + provider).returncode == 0
        state = json.loads(call('status').stdout)
        assert state['provider'] == provider and not state['key_configured']
        assert call('enable').returncode != 0
        assert call('save-key', data=('key-' + provider).encode()).returncode == 0
        selected = json.loads(call('status').stdout)
        assert selected['key_configured'] and selected['key_hint'] == 'key...enai'
        assert (key.parent / (provider + '-api-key')).read_text().strip() == 'key-' + provider
    assert call('save-key-deepseek', data=b'explicit-deepseek').returncode == 0
    assert key.read_bytes() == b'explicit-deepseek\n'
    assert call('save-key-deepseek', data=b'sk-test_example').returncode == 0
    assert call('select-doubao').returncode != 0
    assert call('select-deepseek').returncode == 0
    assert json.loads(call('status').stdout)['provider'] == 'deepseek'
    assert key.read_bytes() == b'sk-test_example\n'
    log = (root / 'systemctl.log').read_text()
    assert 'enable --now voice-input-text.service voice-input.service' in log
    assert 'disable --now voice-input-text.service voice-input.service' in log
    assert 'sk-test_example' not in log
    print('Settings: private key, invalid replacement, status, enable and disable passed')
