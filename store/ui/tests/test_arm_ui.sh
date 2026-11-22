#!/bin/sh
# Run only on aarch64 Linux. A private mount namespace hides the real screen
# and keyboard; all input, framebuffer contents and settings are disposable.
set -eu
app=${1:?provide the ARM UI executable}
case "$app" in /*) ;; *) echo 'use an absolute executable path' >&2; exit 1;; esac
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/voice-arm-ui.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
cc -O2 -fPIC -shared -o "$test_dir/fake_device.so" "$here/fake_device.c" -ldl
truncate -s 108800 "$test_dir/fb.raw"
cat > "$test_dir/settings" <<'SCRIPT'
#!/bin/sh
case "$1" in
 status) configured=false; [ ! -f "$TEST_DIR/saved" ] || configured=true; if [ -f "$TEST_DIR/enabled" ]; then active=true; else active=false; fi; printf '%s\n' "{\"key_configured\":$configured,\"voice_active\":$active,\"text_active\":$active,\"launcher_compatible\":true}" ;;
 select-openai) : > "$TEST_DIR/switched" ;;
 save-key*) key=$(cat); case "$key" in sk-48|sk_4.8) printf "%s\n" "$key" >> "$TEST_DIR/saved" ;; *) exit 1 ;; esac ;;
 enable) printf 'called\n' >> "$TEST_DIR/enabled"; sleep 1 ;;
 *) exit 1 ;;
esac
SCRIPT
chmod 700 "$test_dir/settings"
if ! timeout 40 unshare -Urm sh -s -- "$test_dir" "$app" <<'NAMESPACE'
set -eu
test_dir=$1
app=$2
mount --bind /dev/null /dev/fb0
mount -t tmpfs -o size=64k tmpfs /dev/input
mkfifo /dev/input/event0
export LD_PRELOAD="$test_dir/fake_device.so" FAKE_FB="$test_dir/fb.raw"
export CARDPUTER_VOICE_SETTINGS_BIN="$test_dir/settings" TEST_DIR="$test_dir" XDG_RUNTIME_DIR="$test_dir/runtime"
mkdir -p "$XDG_RUNTIME_DIR/voice-input"
touch "$XDG_RUNTIME_DIR/voice-input/recorder.lock"
flock -x "$XDG_RUNTIME_DIR/voice-input/recorder.lock" sleep 35 & lock_pid=$!
sleep .3
"$app" >"$test_dir/app.log" 2>&1 &
pid=$!
trap 'kill "$pid" "$lock_pid" 2>/dev/null || true' EXIT HUP INT TERM
python3 - "$test_dir" <<'PY'
import os, struct, sys, time
root=sys.argv[1]
fd=os.open('/dev/input/event0',os.O_WRONLY)
def key(code):
    for state in (1,0):
        os.write(fd,struct.pack('llHHi',0,0,1,code,state))
        time.sleep(.15)
time.sleep(1)
key(9)  # 8: open Help
key(1)  # Escape returns to settings
key(7)  # 6: change provider
key(5)  # 4: edit key
# The voice chord must not type V into a secret field.
for code,state in ((42,1),(389,1),(47,1),(47,0),(389,0),(42,0)):
    os.write(fd,struct.pack('llHHi',0,0,1,code,state));time.sleep(.08)
for code in (31,37,71,5,9): key(code)  # sk-48 using the physical minus key; 4 and 8 remain text while editing
time.sleep(.5)
with open(root+'/fb.raw','rb') as f, open(root+'/editing.raw','wb') as out: out.write(f.read())
key(28)  # Enter: save
time.sleep(1)
key(5)   # Open editor again; physical underscore and period keycodes.
for code in (31,37,70,5,91,9): key(code)
key(28)
time.sleep(1)
key(6)   # 5: enable
key(6)   # A second press while waiting must not repeat the action.
time.sleep(2)
try:
    key(1)  # Esc may close the FIFO before the release event.
except BrokenPipeError:
    pass
os.close(fd)
PY
wait "$pid"
kill "$lock_pid" 2>/dev/null || true
trap - EXIT HUP INT TERM
NAMESPACE
then
    cat "$test_dir/app.log" >&2 2>/dev/null || true
    exit 1
fi
test -f "$test_dir/switched"
test "$(wc -l < "$test_dir/saved" | tr -d ' ')" = 2
test -f "$test_dir/enabled"
test "$(wc -l < "$test_dir/enabled" | tr -d ' ')" = 1
if [ -n "${CARDPUTER_ARM_TEST_IMAGE:-}" ]; then cp "$test_dir/editing.raw" "$CARDPUTER_ARM_TEST_IMAGE"; fi
printf '%s\n' 'ARM UI: virtual framebuffer, evdev input, visible key save and enable passed'
