# Attempt Sync

A Geode companion mod that syncs **Attempt Playback 1.5.7** saved ghosts across **iOS/iPadOS, macOS (Intel and Apple Silicon), and Windows (64-bit)** using a private GitHub repository. Targets Geometry Dash 2.2081 and Geode 5.11.1 or compatible newer 5.x.

Download the current test build from [Releases](https://github.com/hdlolhfff/attempt-sync/releases/tag/v1.0.1). All three native builds and core tests passed; in-game testing is still needed.

**iOS update:** v1.0.0 shipped an unsigned iOS library and fails with “missing code signature”. Install v1.0.1, which signs the iOS library and verifies the signature in the final package.

## Build and install

1. Open this repository's **Actions → Build Attempt Sync**. Open the latest successful run and download **Attempt-Sync-All-Platforms**. Unzip it to get the `.geode` mod.
2. Import the `.geode` through your Geode launcher's file-import flow, or put it in your installation's `geode/mods` directory and restart. On iPad/iPhone, use an existing Geode-compatible GD launcher that supports importing local mods. This package is a mod, not an IPA or an App Store app.
3. Install/enable Attempt Playback **1.5.7** on every device. The sync mod checks the version before accessing its files.

If an Actions run is still building or fails, there is no validated installable package yet. Check the run before downloading. Source-only ZIP files are not installable mods.

## One-time GitHub setup

1. Create a **private** repository for replay data and initialize it with a README, using the `main` branch. A separate data repo keeps recordings out of this mod's source and build history.
2. Create a **fine-grained personal access token** at https://github.com/settings/personal-access-tokens/new. Select only that data repository. Give it **Contents: Read and write** (Metadata read is automatic). Choose an expiration you can maintain. Do not grant access to all your repositories.
3. In Geode → Installed → Attempt Sync → Settings, enter:
   - **Repository**: `your-username/your-data-repo`
   - **Branch**: `main`
   - **GitHub Token**: the fine-grained token
4. Tap the **Sync** button near the right edge of the GD main menu. Configure the same data repository on your other devices and tap Sync there too. Each device can use a different token for the same repository.
5. Optionally enable **Sync at Main Menu** to sync whenever the main menu opens. Quit the level normally first so Attempt Playback saves your new attempts. After a recording session, return to the main menu and sync before switching devices.

The token is saved in Geode's local settings in **plain text** and shown in the settings field. Never share those settings files or screenshots showing the token. The mod sends the token only to `api.github.com` over verified HTTPS; it does not put it in the replay repository or logs. Recordings are private through GitHub repository permissions, not end-to-end encrypted.

## What sync does

- Reads only `manshoety.attemptplayback`'s `attempts/*_attempts.apx` files. Colors, sounds, GD account saves, Eclipse settings, and other mods are unaffected.
- Combines normal and practice ghost attempts; compares complete attempt payloads, ignoring only local serial numbers and padding. Byte-identical recordings deduplicate, including indistinguishable recordings made independently.
- Renumbers imported attempts and fixes references in practice sessions. Both devices' distinct practice sessions are kept; differing versions of the same session may appear as separate sessions. Existing cloud practice-session selection is retained.
- Uses GitHub file SHAs for compare-and-swap writes. If another device writes concurrently, it rereads and merges, retrying up to three times. Repeated unchanged syncs do not create commits.
- Imports downloads only on the main thread, outside gameplay/editor. If you start playing during a request, they wait for your return to the main menu. If local data changed during the request, it skips that import; sync again.
- Keeps each replaced local file under this mod's save directory in `backups/<timestamp>/`. Backups are retained until you remove them. Recover with GD closed by copying the backup file to Attempt Playback's `attempts` directory.
- Stops safely on unsupported, malformed, oversized, or legacy files. Open legacy files in Attempt Playback 1.5.7 to migrate them before syncing. Completed files remain synced if a later file fails.

Sync is **additive**. Deleting ghosts locally does not delete them in the cloud, and a later sync can restore them. To remove a level's recordings everywhere, disable automatic sync on all devices, remove its local file on every device and its matching `attempts/…_attempts.apx` in the data repo, then reenable sync. Backups and Git history may retain older data.

## Limits and verification

- 20 MiB per merged level file, fewer than 1,000 level files, and 128 MiB of pending imports per batch (sync again to finish larger batches). Large histories can exceed GitHub API storage/rate limits; use another storage backend for very large libraries.
- Only APX v8, ATT3 v2, and PATH v5 are accepted. Official online level IDs match across devices. Editor/local levels sync when Attempt Playback gives them the same save ID; this mod does not sync the level itself.
- GitHub credentials are entered by you on each device. This repo does not provision accounts, tokens, or iOS launchers.
- Core merge tests cover overlapping histories, serial collisions, practice references, repeated sync convergence, malformed/truncated input, and base64 encoding. Actual in-game behavior still needs device testing, particularly iOS importing and compatibility with other gameplay mods.

Run the platform-independent tests locally:

```sh
g++ -std=c++23 -Wall -Wextra -Werror -O2 tests/apx_test.cpp -o apx-test
./apx-test
```

GitHub Actions builds the native mod for all requested platforms and combines them into one `.geode`. To build locally, install the Geode CLI and SDK, set `GEODE_SDK`, and run `geode build` using a host/toolchain for the intended platform.

Format reference: https://github.com/manshoety/AttemptPlaybackMod (1.5.7). This is an independent companion, not an official Attempt Playback release.
