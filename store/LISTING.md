# Voice Input — store listing

**Summary:** Use your voice to enter text on Cardputer Zero.

Use your voice to enter text on Cardputer Zero. Press Fn + V to start recording, speak, then press it again to insert the transcription into the current input field. Text is not sent or executed automatically.

Choose DeepSeek or OpenAI, and switch between providers with configured keys using Shift + Fn + V. Manage keys, start or stop the service, and open the shortcut guide in the settings app. REC and ASR indicators show recording and transcription progress; OK and FAIL show the outcome. Recording stops automatically after 15 seconds and is canceled if the original input target changes or closes. Transcription requests time out after 10 seconds.

Tested with ZClaw and the CLI/ST terminal. Compatibility may vary across apps and system versions. Requires an internet connection and an API key. Audio is sent to the selected provider, whose pricing and data policies apply. OpenAI automatically detects the spoken language.

**Permissions:** microphone, network, background services.

**Author:** HELLOMSG (`hellomsg` on GitHub).

**License:** MIT, with separate third-party notices.

**Share code:** `VOIC` (pending publication).

Chinese and Japanese store descriptions are maintained in `ui/app-builder.json`.
