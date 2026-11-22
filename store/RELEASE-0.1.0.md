# Historical 0.1.0 release candidate

This document records an earlier candidate, not the current development build. It is not a store submission.

- App: Cardputer Voice Settings; package: `cardputer-voice`; architecture: `arm64`.
- Author: HELLOMSG (`hellomsg`); license: MIT.
- Source: <https://github.com/hellomsg/cardputer-zero-voice-input>.
- Candidate package SHA-256: `9cf60659e1fd0ee9f5cc8c04e86f97a84e13c90ae77b0008e420bbb455f12f5f`.
- The associated `ui/app-builder.json`, screenshots, and share-code candidate `CV09` refer to this older interface; check them again before submission.

The candidate was installed as an upgrade and retained its API key and startup configuration after reboot. The user confirmed the settings UI, ZClaw voice input, sound cues, and REC/ASR indicators. ARM component, isolated UI, install, and uninstall tests passed. The native bridge depends on specific APPLaunch and ZClaw builds. CLI/ST had been tested on a development build but not separately on this candidate. Long-running memory behavior and store CI were not verified.

Before a future submission, prepare a current demonstration video, review all store metadata and package hashes, and follow the store's submission and review process.
