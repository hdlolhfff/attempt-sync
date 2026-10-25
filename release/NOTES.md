Big levels sync now.

- The per-level limit rises from 20 MiB to 64 MiB, so long histories that previously failed with "Attempt file exceeds 20 MiB" can sync.
- A level still above the limit is skipped and named in the result instead of aborting the whole batch. Completed levels are kept and the other files still upload and import.
- Batch memory limits rise to 256 MiB to leave room for merged large files.
- Skipped levels appear in the completion notification and in a short alert, so it is clear which file was left alone.
- Error text now ends with proper punctuation instead of running into "Completed files were kept".
- Everything from 1.4.0 remains: a fresh download reads and writes with no setup, concurrent writes retry six times, and permission failures name the missing collaborator access.
- Everything from 1.3.3 remains: one tree request for comparison, eight-file parallel downloads, the `sync-cache.txt` unchanged-file cache, one Sync All confirmation, merged histories, and backups.

The public build carries the shared writer token by design. Anyone with the file can extract it, so the owner can rotate the token to revoke older builds at once.

SideStore/JIT-less: fully close GD, import the exact **local.attempt_sync.geode** file using **Files → Share → Geode / Geode Launcher**, wait for import confirmation, then use launcher **Settings → Force Resign** and launch GD.

Includes Windows x64, macOS Intel/Apple Silicon, and iOS/iPadOS arm64. Requires GD 2.2081, Geode 5.11.1+ (5.x), and Attempt Playback 1.5.7.
