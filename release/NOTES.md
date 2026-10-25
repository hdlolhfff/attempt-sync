Download and play: this build can **read and write** with no setup.

- The mod now creates `settings.json` on first launch with **Upload My Attempts** on and a shared writer token filled in, so a fresh install both downloads everybody's ghosts and uploads yours.
- The token is baked into this `.geode` only; it is not in the source repository and is limited to **hdlolhfff/gd-attempts**. Turn uploads off to make a device read-only. A token you paste yourself is never overwritten.
- Concurrent writers are handled better: compare-and-swap uploads retry up to six times, merging each time.
- Missing collaborator access or a rotated token now reports a clear HTTP 403/404 explanation instead of a generic failure.
- Everything from 1.3.3 remains: one tree request for comparison, eight-file parallel downloads, the `sync-cache.txt` unchanged-file cache, one Sync All confirmation, merged histories, backups, and no commits for unchanged syncs.

The public build carries the shared token by design. Anyone with the file can extract it, so the owner can rotate the token to revoke older builds at once.

SideStore/JIT-less: fully close GD, import the exact **local.attempt_sync.geode** file using **Files → Share → Geode / Geode Launcher**, wait for import confirmation, then use launcher **Settings → Force Resign** and launch GD.

Includes Windows x64, macOS Intel/Apple Silicon, and iOS/iPadOS arm64. Requires GD 2.2081, Geode 5.11.1+ (5.x), and Attempt Playback 1.5.7.
