First test release of **Attempt Sync**, a Geode companion for **Attempt Playback 1.5.7**.

Download **local.attempt_sync.geode** below. The same file contains Windows x64, macOS Intel/Apple Silicon, and iOS/iPadOS arm64 builds. Import it through your existing Geode-compatible launcher and restart GD.

Requires Geometry Dash **2.2081**, Geode **5.11.1+ (compatible 5.x)**, and Attempt Playback **1.5.7**.

## Configure sync

1. Create a separate **private replay-data repository**, initialized with a README on `main`.
2. Make a fine-grained GitHub token for that repository only, with **Contents: Read and write**.
3. In Attempt Sync's Geode settings on each device, enter `owner/repository`, branch `main`, and the token.
4. Tap **Sync** on the GD main menu. Optionally enable **Sync at Main Menu**. Return to the main menu and sync after playing before switching devices.

Normal/practice ghosts and practice sessions merge across devices; serial collisions are remapped. Imports wait until gameplay/editor ends and back up replaced local files. Sync is additive: local deletions can be restored from the cloud. Tokens are stored locally in Geode settings in plain text; do not share those files.

## Validation

All GitHub Actions jobs passed: core tests, Windows, macOS, iOS, and combined packaging. Binary inspection confirmed Windows x64, macOS universal x64/arm64, and iOS arm64. Core tests also passed locally with address/undefined-behavior sanitizers. GitHub immutable raw blob downloading was verified.

**In-game/device testing has not been performed.** This is an initial test release, especially for iOS launcher importing and compatibility with other mods. Full setup, limits, backup recovery, and source are in [README](https://github.com/hdlolhfff/attempt-sync#readme).

Mod SHA-256: `bd0e2a30d8befc650b0ba2c65d033e8610340039ac5450de76261c9f93b8f068`
