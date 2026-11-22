# Historical 0.1.1 release candidate

This document records an earlier candidate, not the current development build. It is not a store submission.

- App: Cardputer Voice Settings; package: `cardputer-voice`; architecture: `arm64`.
- Author: HELLOMSG (`hellomsg`); license: MIT.
- Source: <https://github.com/hellomsg/cardputer-zero-voice-input>.
- Candidate package SHA-256: `3f605567e7e0207e94ef482407644a54ce8a319eed284ca587a1b27e35c52b95`.
- The associated `ui/app-builder.json`, screenshots, and share-code candidate `CV09` refer to this older interface; check them again before submission.

The 0.1.1 candidate placed key setup on physical key 4 and service controls on key 8. The user confirmed those controls and layout, and desktop and ARM UI tests passed. Its components matched the tested 0.1.1~test1 package. ZClaw voice input had previously been confirmed on 0.1.0 but was not separately retested end-to-end on 0.1.1. CLI/ST had been tested on a development build but not separately on this candidate. Long-running memory behavior and store CI were not verified.

Before a future submission, prepare a current demonstration video, review all store metadata and package hashes, and follow the store's submission and review process.
