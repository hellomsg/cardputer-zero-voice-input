#!/bin/sh
# Run only a VOICE_UI_SELFTEST build, with an isolated mock backend.
set -eu
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/voice-ui-test.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
cat > "$test_dir/settings" <<'SCRIPT'
#!/bin/sh
case "$1" in
 status) provider=$(cat "$VOICE_UI_TEST_DIR/provider" 2>/dev/null || printf deepseek); if [ -f "$VOICE_UI_TEST_DIR/disabled" ]; then printf '%s\n' "{\"provider\":\"$provider\",\"key_configured\":true,\"key_hint\":\"sk-...mple\",\"voice_active\":false,\"text_active\":false,\"launcher_compatible\":true}"; else printf '%s\n' "{\"provider\":\"$provider\",\"key_configured\":true,\"key_hint\":\"sk-...mple\",\"voice_active\":true,\"text_active\":true,\"launcher_compatible\":true}"; fi ;;
 select-deepseek|select-openai|select-doubao) printf %s "${1#select-}" > "$VOICE_UI_TEST_DIR/provider" ;;
 save-key*) key=$(cat); [ "$key" = sk-48 ] || exit 1; : > "$VOICE_UI_TEST_DIR/saved" ;;
 disable) printf 'called\n' >> "$VOICE_UI_TEST_DIR/disabled"; sleep 0.8 ;;
 *) exit 1 ;;
esac
SCRIPT
chmod 700 "$test_dir/settings"
export VOICE_UI_TEST_DIR="$test_dir"
mkdir -p "$test_dir/runtime/voice-input"
export XDG_RUNTIME_DIR="$test_dir/runtime"
export CARDPUTER_VOICE_SETTINGS_BIN="$test_dir/settings"
"${1:?provide the VOICE_UI_SELFTEST executable}"
test -f "$test_dir/saved"
test -f "$test_dir/disabled"
test "$(wc -l < "$test_dir/disabled" | tr -d ' ')" = 1
printf '%s\n' 'UI: visible key entry, private pipe, save and service action passed'
