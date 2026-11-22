# Voice Input settings UI

This LVGL settings app is based on the CardputerZero template; see [LICENSE](LICENSE). It invokes the Rust settings program at `/usr/lib/voice-input/voice-input-settings`. In isolated tests, `CARDPUTER_VOICE_SETTINGS_BIN` can override that path.

The footer actions align with the physical keys: 4 sets the selected provider's key, 5 starts or stops the services, 6 selects the provider, and 8 opens Help. Esc cancels or goes back. New keys are visible while editing and passed through a private pipe, not command-line arguments; previously saved keys are identified by a short prefix and last four characters.

For the desktop simulator test, configure CMake with `VOICE_UI_SELFTEST=ON`, build, then run `sh test_ui.sh <test-executable>`. Restore the option to `OFF` for normal builds. On ARM Linux, `sh tests/test_arm_ui.sh <absolute-executable-path>` and `sh tests/test_arm_hotkey.sh <absolute-voice-executable-path>` run in isolated namespaces with simulated devices and disposable settings.

See [development notes](../DEVELOPMENT.md) for the current scope and release limitations.
