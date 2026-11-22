#!/bin/sh
# Isolate the keyboard and home directory, then exercise real evdev messages.
set -eu
voice=${1:?provide absolute ARM voice executable}
case "$voice" in /*) ;; *) exit 1;; esac
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
d=$(mktemp -d "${TMPDIR:-/tmp}/voice-hotkey.XXXXXX")
trap 'rm -rf "$d"' EXIT HUP INT TERM
cc -O2 -fPIC -shared -o "$d/fake.so" "$here/fake_device.c" -ldl
mkdir -p "$d/home/.config/voice-input" "$d/runtime"
printf 'test-key\n' > "$d/home/.config/voice-input/deepseek-api-key"
chmod 700 "$d/home/.config/voice-input"
chmod 600 "$d/home/.config/voice-input/"*-api-key
unshare -Urm sh -s -- "$d" "$voice" <<'NAMESPACE'
set -eu
d=$1; voice=$2
mount -t tmpfs -o size=64k tmpfs /dev/input
mkdir /dev/input/by-path
mkfifo /dev/input/by-path/platform-3f804000.i2c-event
export HOME="$d/home" XDG_RUNTIME_DIR="$d/runtime" LD_PRELOAD="$d/fake.so" CARDPUTER_VOICE_SOUNDS=0
"$voice" >"$d/log" 2>&1 & pid=$!
trap 'kill "$pid" 2>/dev/null || true' EXIT HUP INT TERM
python3 - "$d" <<'PY'
import json,os,struct,sys,time
from pathlib import Path
root=Path(sys.argv[1]); device='/dev/input/by-path/platform-3f804000.i2c-event'
fd=os.open(device,os.O_WRONLY)
def event(code,value):
 os.write(fd,struct.pack('llHHi',0,0,1,code,value));time.sleep(.03)
def chord():
 event(42,1);event(389,1);event(47,1);event(47,2);event(47,0);event(389,0);event(42,0)
def wait(expected):
 for _ in range(100):
  try:
   if json.loads((root/'runtime/voice-input/status.json').read_text())['state']==expected:return
  except (OSError,ValueError):pass
  time.sleep(.05)
 raise AssertionError('status did not become '+expected)
# Only DeepSeek is configured: keep it selected.
chord();wait('provider_deepseek')
provider=root/'home/.config/voice-input/provider'
assert not provider.exists() or provider.read_text().strip()=='deepseek'
# Neither key: no change, and a clear notice.
(root/'home/.config/voice-input/deepseek-api-key').unlink()
chord();wait('provider_none')
assert not provider.exists() or provider.read_text().strip()=='deepseek'
# Only OpenAI: select it; then add DeepSeek and switch back and forth.
key=root/'home/.config/voice-input/openai-api-key';key.write_text('test-key\n');key.chmod(0o600)
chord();wait('provider_openai')
assert provider.read_text().strip()=='openai'
key=root/'home/.config/voice-input/deepseek-api-key';key.write_text('test-key\n');key.chmod(0o600)
chord();wait('provider_deepseek')
assert provider.read_text().strip()=='deepseek'
chord();wait('provider_openai')
wait('idle')
os.close(fd)
PY
kill "$pid"; wait "$pid" || true
trap - EXIT HUP INT TERM
NAMESPACE
printf '%s\n' 'ARM hotkey: provider switching, key repeat and notice timeout passed'
