# Attempt Sync

A Geode companion mod that syncs **Attempt Playback 1.5.7** saved ghosts across **iOS/iPadOS, macOS (Intel and Apple Silicon), and Windows (64-bit)** using the built-in **hdlolhfff/gd-attempts** repository. Targets Geometry Dash 2.2081 and Geode 5.11.1 or compatible newer 5.x.

Download the current test build from [Releases](https://github.com/hdlolhfff/attempt-sync/releases/tag/v1.2.0). All three native builds and core tests passed; in-game testing is still needed.

**iOS update:** v1.0.0 shipped an unsigned iOS library and fails with “missing code signature”. Install v1.0.1, which signs the iOS library and verifies the signature in the final package.

## Build and install

1. Open this repository's **Actions → Build Attempt Sync**. Open the latest successful run and download **Attempt-Sync-All-Platforms**. Unzip it to get the `.geode` mod.
2. Import the `.geode` through your Geode launcher's file-import flow, or put it in your installation's `geode/mods` directory and restart. On iPad/iPhone, use an existing Geode-compatible GD launcher that supports importing local mods. This package is a mod, not an IPA or an App Store app.
3. Install/enable Attempt Playback **1.5.7** on every device. The sync mod checks the version before accessing its files.

If an Actions run is still building or fails, there is no validated installable package yet. Check the run before downloading. Source-only ZIP files are not installable mods.

## SideStore / JIT-less loading

JIT-less on a SideStore installation requires the mod to be signed **on your device with the same SideStore certificate used for the launcher**. The ad-hoc signature included in this download is not a trusted SideStore signature. A successful build or `codesign` file check does not prove that iPadOS will accept the library in that app.

After importing/updating this mod:

1. Fully close the running GD game and open the Geode launcher.
2. Open launcher **Settings**, find the **JIT-less** controls, and tap **Force Resign**.
3. Wait for **Resign successful!**, then launch the game from the launcher.
4. If certificate validation fails, refresh/import the SideStore certificate in those settings, run **Test JIT-Less Mode**, and then **Force Resign** again.

These controls are in the Geode launcher, not the in-game Geode mod menu. The certificate stays on your device; do not send it to anyone. See the [official SideStore/JIT-less guide](https://github.com/geode-sdk/ios-launcher/blob/main/MODERN-IOS-INSTALL.md#jit-less). The launcher's Force Resign operation signs installed mods as well as the game.

## Owner setup (hdlolhfff)

The replay repository is built in: **hdlolhfff/gd-attempts**, branch **main**. There are no Repository or Branch text fields, so you never need to type a slash in GD. Old values from earlier mod versions are ignored.

1. Create a **fine-grained personal access token** at https://github.com/settings/personal-access-tokens/new. Select only **gd-attempts** and grant **Contents: Read and write** (Metadata read is automatic).
2. Open Geode → Installed → Attempt Sync → Settings. Enable **Upload My Attempts** and paste your token into **GitHub Token**, then tap **Apply**.
3. Tap **Sync** on the GD main menu. On each of your own devices, enable the same owner switch and provide a token authorized for that repository. These devices merge recordings in both directions.
4. Optionally enable **Sync at Main Menu**. Return to the main menu and sync after playing before switching devices.

The token stays in Geode's local settings in **plain text**. It is never built into the mod, committed to either repository, or sent to viewers. Do not distribute your token or settings file.

## Viewer setup

**Upload My Attempts is off by default.** With it off, the mod only downloads playbacks from **hdlolhfff/gd-attempts**; it never sends a write request to GitHub and does not scan local-only levels for uploading. Imported ghosts merge with the viewer's own local ghosts, with backups before any replacement.

- For a **public** replay repository, viewers can leave GitHub Token blank and tap **Sync**.
- For a **private** replay repository, each viewer needs access through their own GitHub account and their own token with **Contents: Read-only** for gd-attempts. The owner must grant access. Do not give viewers the owner's upload token.
- After downloading, open the matching level in Attempt Playback to see the ghosts. The mod syncs recordings, not the levels themselves.

The owner has chosen public viewing. GitHub repository visibility must be changed by an administrator; check the repository to confirm the change has taken effect.

## Review before owner sync

Before an owner sync changes a file, a **Review Playback Sync** dialog shows its filename, local/GitHub sizes, attempt counts, and the unique attempts present only on each side. It identifies which file is larger but does not assume that byte size equals attempt count. Equal-size files can contain different recordings.

Tap **Sync** to keep both histories and publish any new local data, or **Cancel** to stop the sync before changing that file. Files completed earlier in the same batch remain synced. Conflicts with another device are reread and reviewed again. Unchanged files do not prompt. With automatic sync enabled, changed owner files still require confirmation. Viewers remain download-only.

If a comparison finishes during gameplay, it waits until you return to the main menu to show the dialog. Local data changed meanwhile is kept and can be included by syncing again.

## What sync does

- Reads only `manshoety.attemptplayback`'s `attempts/*_attempts.apx` files. Colors, sounds, GD account saves, Eclipse settings, and other mods are unaffected.
- Combines normal and practice ghost attempts; compares complete attempt payloads, ignoring only local serial numbers and padding. Byte-identical recordings deduplicate, including indistinguishable recordings made independently.
- Renumbers imported attempts and fixes references in practice sessions. Both devices' distinct practice sessions are kept; differing versions of the same session may appear as separate sessions. Existing cloud practice-session selection is retained.
- Owner mode uses GitHub file SHAs for compare-and-swap writes. If another device writes concurrently, it rereads and merges, retrying up to three times. Repeated unchanged syncs do not create commits.
- Imports downloads only on the main thread, outside gameplay/editor. If you start playing during a request, they wait for your return to the main menu. If local data changed during the request, it skips that import; sync again.
- Keeps each replaced local file under this mod's save directory in `backups/<timestamp>/`. Backups are retained until you remove them. Recover with GD closed by copying the backup file to Attempt Playback's `attempts` directory.
- Stops safely on unsupported, malformed, oversized, or legacy files. Open legacy files in Attempt Playback 1.5.7 to migrate them before syncing. Completed files remain synced if a later file fails.

Sync is **additive**. Deleting ghosts locally does not delete them in the cloud, and a later sync can restore them. To remove a level's recordings everywhere, disable automatic sync on all devices, remove its local file on every device and its matching `attempts/…_attempts.apx` in the data repo, then reenable sync. Backups and Git history may retain older data.

## Limits and verification

- 20 MiB per merged level file, fewer than 1,000 level files, and 128 MiB of pending imports per batch (sync again to finish larger batches). Large histories can exceed GitHub API storage/rate limits; use another storage backend for very large libraries.
- Only APX v8, ATT3 v2, and PATH v5 are accepted. Official online level IDs match across devices. Editor/local levels sync when Attempt Playback gives them the same save ID; this mod does not sync the level itself.
- GitHub credentials are entered by you on each device. This repo does not provision accounts, tokens, or iOS launchers.
- Tests also verify that viewer mode never requests uploads, owner mode uploads new attempts, and the fixed source is correct. Core merge tests cover overlapping histories, serial collisions, practice references, repeated sync convergence, malformed/truncated input, and base64 encoding. Actual in-game behavior still needs device testing, particularly iOS importing and compatibility with other gameplay mods.

Run the platform-independent tests locally:

```sh
g++ -std=c++23 -Wall -Wextra -Werror -O2 tests/apx_test.cpp -o apx-test
./apx-test
```

GitHub Actions builds the native mod for all requested platforms and combines them into one `.geode`. To build locally, install the Geode CLI and SDK, set `GEODE_SDK`, and run `geode build` using a host/toolchain for the intended platform.

Format reference: https://github.com/manshoety/AttemptPlaybackMod (1.5.7). This is an independent companion, not an official Attempt Playback release.
