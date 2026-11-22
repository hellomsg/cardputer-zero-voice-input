# Cardputer Zero Voice Input

Voice Input adds push-to-toggle speech input to supported Cardputer Zero applications. Press **Fn + V** to start recording, speak, then press it again to stop. The selected speech recognition service returns text, which is inserted at the original input position without automatically sending a message or running a command.

Voice Input is available on the [CardputerZero Store](https://cardputer.cc/#/apps/VOIC). It has been tested on a Cardputer Zero with APPLaunch, ZClaw, and the CLI/ST terminal.

## Features

- Choose DeepSeek ASR or OpenAI (`gpt-transcribe`), with a separate API key for each provider. OpenAI automatically detects the spoken language.
- Press **Shift + Fn + V** to cycle through providers that have configured keys. A short badge shows the selected provider; if neither key is configured, it prompts you to add one. Provider switching is disabled while recording or transcribing.
- A red **REC** badge indicates recording, blue **ASR** indicates transcription, green **OK** indicates successful insertion, and red **FAIL** indicates failure.
- Recording is canceled if the original input target changes or closes. Recording stops automatically after 15 seconds; transcription requests time out after 10 seconds.
- A settings app provides service controls, key management, provider selection, and an English Help page.

Actual language support and recognition quality depend on the selected provider. Audio is sent to that provider after recording stops; its pricing and data policies apply. The experimental Doubao adapter is hidden and has not been validated with a real account.

## Using the settings app

Open **Voice Input** from APPLaunch. The labels at the bottom align with the physical keys under the screen:

| Key | Action |
| --- | --- |
| 4 | Set the API key for the selected provider |
| 5 | Start or stop the service |
| 6 | Select the ASR provider |
| 8 | Open Help |
| Esc | Go back or exit |

New keys are visible while you type or paste. Existing keys are identified by a short prefix and last four characters, for example `sk-...CHgA`; entering a new key replaces the selected provider’s key. Press **Enter** to save or **Esc** to cancel. For a long key, start ZeroDesk on the device, open Voice Input from ZeroDesk's application list, and use its screen-paste function from your computer. The app remains usable without ZeroDesk.

The selected provider needs a configured key to start the service. The global provider shortcut only selects providers whose keys are configured. A switch already in progress shows a waiting animation and ignores repeated presses.

## Compatibility

Linux does not provide a universal Unicode insertion interface for every application. This project uses a shared native adapter for supported APPLaunch and ZClaw builds, including the CLI/ST terminal. The adapter checks executable build IDs; a firmware or application update may require a compatible adapter update. Other applications may require their own integration. Password fields are excluded from the current native adapter.

The current hardware configuration uses the built-in keyboard and ALSA capture device `plughw:1,0`. Network access and a working provider account are required.

## Privacy and local data

The Rust recorder and text broker run as the user services `voice-input.service` and `voice-input-text.service`. The app does not expose a network control port. Provider keys are stored separately in `~/.config/voice-input/` as `deepseek-api-key` and `openai-api-key`, with private file permissions. Full keys are not included in packages or logs and are retained by default on uninstall. The settings screen displays a short key identifier. Temporary recordings are private and are removed after processing.

## Building and testing

The Rust services are in `rust/`, the LVGL settings app is in `store/ui/`, and the native integration bridge is in `shared_bridge.c`. See [package development](store/package/README.md) and [development notes](store/DEVELOPMENT.md) for packaging and testing details. The source package build requires the AArch64 Rust target and C/C++ cross compiler, CMake, Ninja, and `dpkg-deb`.

On Linux, run:

```sh
cargo test --locked --manifest-path rust/Cargo.toml
cargo build --locked --release --manifest-path rust/Cargo.toml --bins
make test
```

Python files retained in the repository are test tools, not runtime services. The full Rust text service and device tests use Linux-specific interfaces and cannot run natively on macOS.

## License

Project code is licensed under [MIT](LICENSE). The settings UI includes code from the CardputerZero template; its original [MIT license and copyright notice](store/ui/LICENSE) are retained. Additional third-party notices are in [licenses/](licenses/) and `store/ui/assets/fonts/LICENSE.txt`.
