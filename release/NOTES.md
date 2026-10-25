Sync is now much faster.

- Comparison uses one repository tree request instead of a metadata request per level.
- Changed playback files download eight at a time; large files shrink the window so memory stays bounded.
- Token-free public viewing fetches playback bytes from GitHub's raw CDN instead of the API, avoiding the 60-request hourly quota for large libraries.
- Unchanged levels are remembered by cloud SHA and a local content hash, so a repeat sync with nothing new downloads no playback files at all.
- A cache entry only skips a level when both the cloud and local copies match it exactly; stale or missing entries just trigger a normal comparison.
- Preserves one Sync All confirmation, merged playback histories, backups, and upload conflict retries.

The public build is token-free. SideStore/JIT-less: fully close GD, import the exact **local.attempt_sync.geode** file using **Files → Share → Geode / Geode Launcher**, wait for import confirmation, then use launcher **Settings → Force Resign** and launch GD.

Includes Windows x64, macOS Intel/Apple Silicon, and iOS/iPadOS arm64. Requires GD 2.2081, Geode 5.11.1+ (5.x), and Attempt Playback 1.5.7.
