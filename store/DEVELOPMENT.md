# Store edition: development notes

The store-oriented app combines an LVGL settings interface, Rust recording and local text services, and a C native integration bridge. Normal operation uses the current user's permissions. API keys are stored separately in the user's private configuration directory and are retained by default on uninstall.

## Current state

The installed test build supports DeepSeek and OpenAI, a provider shortcut, a Help page, and visible key entry. Users have verified ZClaw and CLI/ST voice input, status indicators, service controls, the provider shortcut, and the settings interface on a Cardputer Zero. Doubao integration remains hidden and has only been tested against a simulated service.

The integration bridge depends on specific APPLaunch and ZClaw ELF build IDs and internal interfaces. An incompatible build must be reported as unsupported. Compatibility cannot be assumed for other firmware or arbitrary applications. Audio is uploaded only after recording ends and the original target is checked; uploaded audio cannot be recalled.

The application package has been installed, upgraded, removed, and restored in development tests. Its user services are installed under `/usr/lib/voice-input/`. An update that changes the launcher integration may require a device restart. Avoid replacing unrelated launcher environment settings.

Historical 0.1.x release notes describe earlier versions.

## Verification

Run `cargo test --locked --manifest-path rust/Cargo.toml` on Linux. The settings integration test uses a temporary home and mocked systemd; the bridge and broker integration tests use mock applications and disposable sockets. See [package development](package/README.md) and [UI tests](ui/README.md) for commands and build requirements. Device namespace tests do not alter the real keys, screen, or services.

Before a store release, review the supported firmware builds, the current listing and screenshots, third-party license notices, installation and removal behavior, and real-device failure cases. The current store policy requires a short real-device video in the review PR for a first release. Store publishing is a separate step.
