#!/usr/bin/env python3
"""Check package removal selects only opted-in normal users and skips upgrades."""
import os
from pathlib import Path
import subprocess
import tempfile

script = Path(__file__).with_name('prerm').resolve()
postinst = Path(__file__).with_name('postinst').resolve()
with tempfile.TemporaryDirectory(prefix='voice-prerm-test-') as tmp:
    root = Path(tmp)
    bindir = root/'bin'
    bindir.mkdir()
    marked = root/'marked'
    plain = root/'plain'
    (marked/'.local/share/voice-input').mkdir(parents=True)
    plain.mkdir()
    (marked/'.local/share/voice-input/store-setup-v1').touch()
    getent = bindir/'getent'
    getent.write_text('#!/bin/sh\nprintf "%s\\n" "$FAKE_PASSWD"\n')
    getent.chmod(0o755)
    runuser = bindir/'runuser'
    runuser.write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> "$TEST_LOG"\n')
    runuser.chmod(0o755)
    log = root/'calls'
    env = dict(os.environ, PATH=str(bindir)+os.pathsep+os.environ['PATH'], TEST_LOG=str(log), FAKE_PASSWD=f'root:x:0:0:root:{marked}:/bin/sh\nuser1:x:1000:1000:User:{marked}:/bin/sh\nuser2:x:1001:1001:User:{plain}:/bin/sh')
    subprocess.run([str(script), 'upgrade'], env=env, check=True)
    assert not log.exists()
    subprocess.run([str(postinst), 'configure'], env=env, check=True)
    assert not log.exists()
    subprocess.run([str(postinst), 'configure', '0.1.0~test1'], env=env, check=True)
    assert '/usr/lib/voice-input/refresh-user' in log.read_text()
    log.unlink()
    subprocess.run([str(script), 'remove'], env=env, check=True)
    lines = log.read_text().splitlines()
    assert len(lines) == 1 and lines[0].startswith('-u user1 -- env ')
    assert '/usr/lib/voice-input/remove-user' in lines[0]
print('Package lifecycle: only configured users selected; first install and upgrade distinguished')
