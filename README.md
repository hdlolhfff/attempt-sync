# Attempt Sync

A Geode companion mod that syncs **Attempt Playback 1.5.7** saved ghosts across **iOS/iPadOS, macOS (Intel and Apple Silicon), and Windows (64-bit)** using the built-in **hdlolhfff/gd-attempts** repository. Targets Geometry Dash 2.2081 and Geode 5.11.1 or compatible newer 5.x.

Download the current test build from [Releases](https://github.com/hdlolhfff/attempt-sync/releases). All three native builds and core tests passed; in-game testing is still needed.

**iOS update:** v1.0.0 shipped an unsigned iOS library and fails with “missing code signature”. Install v1.0.1, which signs the iOS library and verifies the signature in the final package.

## Build and install

1. Open this repository's **Actions → Build Attempt Sync**. Open the latest successful run and download **Attempt-Sync-All-Platforms**. Unzip it to get the `.geode` mod.
2. Import the `.geode` through your Geode launcher's file-import flow, or put it in your installation's `geode/mods` directory and restart. On iPad/iPhone, use an existing Geode-compatible GD launcher that supports importing local mods. This package is a mod, not an IPA or an App Store app.
3. Install/enable Attempt Playback **1.5.7** on every device. The sync mod checks the version before accessing its files.

If an Actions run is still building or fails, there is no validated installable package yet. Check the run before downloading. Source-only ZIP files are not installable mods.

## SideStore / JIT-less loading

JIT-less on a SideStore installation requires the mod to be signed **on your device with the same SideStore certificate used for the launcher**. The ad-hoc signature included in this download is not a trusted SideStore signature. A successful build or `codesign` file check does not prove that iPadOS will accept the library in that app.

After importing/updating this mod:

1. Fully close the running GD game. Save the mod with the exact filename **local.attempt_sync.geode**, then in **Files** use **Share → Geode / Geode Launcher** and wait for import confirmation. This native import stages the library under `unzipped/binaries`; Geode can then preserve the signed library when unpacking the new version. A manual copy into mods or an in-game import can omit that staging step.
2. Open the Geode launcher.
3. Open launcher **Settings**, find the **JIT-less** controls, and tap **Force Resign**.
4. Wait for **Resign successful!**, then launch the game from the launcher.
5. If certificate validation fails, refresh/import the SideStore certificate in those settings, run **Test JIT-Less Mode**, and then **Force Resign** again.

These controls are in the Geode launcher, not the in-game Geode mod menu. The certificate stays on your device; do not send it to anyone. See the [official SideStore/JIT-less guide](https://github.com/geode-sdk/ios-launcher/blob/main/MODERN-IOS-INSTALL.md#jit-less). The launcher's Force Resign operation signs installed mods as well as the game.

## Read and write on a fresh install

Builds published from this repository carry a shared writer token, so a new download can **read and write** without any setup. On first launch the mod creates `settings.json` with **Upload My Attempts** on and that token filled in, then one **Sync** downloads everyone's ghosts and uploads yours.

- The shared token is baked into the published `.geode` only. It is never committed to this repository, and it is scoped to **hdlolhfff/gd-attempts**.
- Anyone who has the build can extract that token, so treat the build as a shared repository key. The owner can rotate it to revoke every existing build at once.
- Turn **Upload My Attempts** off to make a device read-only. A token you paste yourself is never replaced by the built-in one.
- If builds start failing with HTTP 403/404 during upload, the shared token was rotated or revoked. Update the mod, or paste a current token in settings.

## Writer setup (your own token)

The replay repository is built in: **hdlolhfff/gd-attempts**, branch **main**. There are no Repository or Branch text fields, so you never need to type a slash in GD. Old values from earlier mod versions are ignored.

1. Ask the repository owner to add your GitHub account as a **collaborator with Write access**. A public repository does not allow writes from anyone else.
2. Create a **fine-grained personal access token** at https://github.com/settings/personal-access-tokens/new. Select only **gd-attempts** and grant **Contents: Read and write** (Metadata read is automatic).
3. Open Geode → Installed → Attempt Sync → Settings. Enable **Upload My Attempts** and paste your token into **GitHub Token**, then tap **Apply**.
4. Tap **Sync** on the GD main menu. Every writer merges recordings in both directions.
5. Optionally enable **Sync at Main Menu**. Return to the main menu and sync after playing before switching devices.

Tokens stay in Geode's local settings in **plain text**. They are never committed to either repository. Do not share your personal token or settings file; the repository owner can revoke one collaborator without affecting anyone else.

## Viewer setup

**Upload My Attempts is off by default** in builds without a shared token. With it off, the mod only downloads playbacks from **hdlolhfff/gd-attempts**; it never sends a write request to GitHub and does not scan local-only levels for uploading. Imported ghosts merge with the viewer's own local ghosts, with backups before any replacement.

- For a **public** replay repository, viewers can leave GitHub Token blank and tap **Sync**.
- For a **private** replay repository, each viewer needs access through their own GitHub account and their own token with **Contents: Read-only** for gd-attempts. The owner must grant access. Do not give viewers the owner's upload token.
- After downloading, open the matching level in Attempt Playback to see the ghosts. The mod syncs recordings, not the levels themselves.

The owner has chosen public viewing. GitHub repository visibility must be changed by an administrator; check the repository to confirm the change has taken effect.

## Practice playback format fix (1.2.1)

Versions through 1.2.0 incorrectly treated the packed APX v5 practice segment as 52 bytes; its actual size is 48 bytes. Valid practice saves could be rejected as "Truncated APX chunk". The parser now uses the correct layout and accepts appended PATH snapshots, using the newest one just as Attempt Playback does. Regression tests serialize the published fields independently of parser offsets. Errors now identify the playback filename.

## Review before owner sync

Before an owner sync changes anything, all changed files are validated and compared. One centered **Review Playback Sync** popup shows the batch file count, uploads/device updates, total local/GitHub sizes and attempts, unique attempts on each side, and the number of larger local files. Size alone does not determine which recordings to keep.

Tap **Sync All** once to process the entire batch, or **Cancel** to stop before any uploads or replacements. There is no per-file confirmation. Cloud conflicts are merged using the approved local snapshots and retried without extra prompts. Automatic owner sync also asks once per batch. Unchanged batches do not prompt; viewers remain download-only. Preparation is limited to 128 MiB of local and merged data.

If a comparison finishes during gameplay, it waits until you return to the main menu to show the dialog. Local data changed meanwhile is kept and can be included by syncing again.

## What sync does

- Reads only `manshoety.attemptplayback`'s `attempts/*_attempts.apx` files. Colors, sounds, GD account saves, Eclipse settings, and other mods are unaffected.
- Combines normal and practice ghost attempts; compares complete attempt payloads, ignoring only local serial numbers and padding. Byte-identical recordings deduplicate, including indistinguishable recordings made independently.
- Renumbers imported attempts and fixes references in practice sessions. Both devices' distinct practice sessions are kept; differing versions of the same session may appear as separate sessions. Existing cloud practice-session selection is retained.
- Writer mode uses GitHub file SHAs for compare-and-swap writes. If another device or friend writes concurrently, it rereads and merges, retrying up to six times. Repeated unchanged syncs do not create commits.
- Imports downloads only on the main thread, outside gameplay/editor. If you start playing during a request, they wait for your return to the main menu. If local data changed during the request, it skips that import; sync again.
- Keeps each replaced local file under this mod's save directory in `backups/<timestamp>/`. Backups are retained until you remove them. Recover with GD closed by copying the backup file to Attempt Playback's `attempts` directory.
- Stops safely on unsupported, malformed, oversized, or legacy files. Open legacy files in Attempt Playback 1.5.7 to migrate them before syncing. Completed files remain synced if a later file fails.

Sync is **additive**. Deleting ghosts locally does not delete them in the cloud, and a later sync can restore them. To remove a level's recordings everywhere, disable automatic sync on all devices, remove its local file on every device and its matching `attempts/…_attempts.apx` in the data repo, then reenable sync. Backups and Git history may retain older data.

## Faster sync (1.3.3)

- Comparison uses one repository tree request instead of a metadata request per level.
- Playback files download eight at a time, with the window shrinking for large files so memory stays bounded.
- Token-free public viewing downloads playback bytes from GitHub's raw CDN, so sync needs only two API requests plus one per changed file; uploads and token-authenticated viewing still use the API.
- `sync-cache.txt` in this mod's save folder records each level's cloud SHA and a hash of the local file. When both are unchanged, that level is skipped without a download. A repeat sync with nothing new needs only the branch and tree requests.
- Delete `sync-cache.txt` to force every file to be compared again. A missing or stale entry only causes an extra comparison, never a skipped change.

## Limits and verification

- 20 MiB per merged level file, fewer than 1,000 level files, and 128 MiB of pending imports per batch (sync again to finish larger batches). Large histories can exceed GitHub API storage/rate limits; use another storage backend for very large libraries.
- Only APX v8, ATT3 v2, and PATH v5 are accepted. Official online level IDs match across devices. Editor/local levels sync when Attempt Playback gives them the same save ID; this mod does not sync the level itself.
- GitHub credentials are entered by you on each device, except for the shared writer token included in published builds. This repo does not provision accounts, tokens, or iOS launchers.
- Tests also verify that read-only mode never requests uploads, writer mode uploads new attempts, and the fixed source is correct. Core merge tests cover overlapping histories, serial collisions, practice references, repeated sync convergence, malformed/truncated input, and base64 encoding. Actual in-game behavior still needs device testing, particularly iOS importing and compatibility with other gameplay mods.

Run the platform-independent tests locally:

```sh
g++ -std=c++23 -Wall -Wextra -Werror -O2 tests/apx_test.cpp -o apx-test
./apx-test
```

GitHub Actions builds the native mod for all requested platforms and combines them into one `.geode`. To build locally, install the Geode CLI and SDK, set `GEODE_SDK`, and run `geode build` using a host/toolchain for the intended platform.

Format reference: https://github.com/manshoety/AttemptPlaybackMod (1.5.7). This is an independent companion, not an official Attempt Playback release.

Version 1.4.0 makes a fresh download read **and** write: published builds include a shared writer token, and the first launch creates `settings.json` with uploads enabled. It also retries concurrent writes up to six times and explains missing collaborator access instead of a generic GitHub error.

Version 1.3.3 speeds up sync with one folder listing, parallel playback downloads, and a local unchanged-file cache. See [Faster sync](#faster-sync-133).

Version 1.3.2 updates the status immediately after Sync All, and reports comparing/syncing file N of M. Tap the main-menu Sync button during a running job to check current progress. Sync approval, per-file progress, and completion are logged without credentials.

Version 1.3.2 adds a persistent bottom notification for all sync phases, including uploading file N / total and the percentage of files completed. It updates in place and changes to the completion message before dismissing.
