Fixes the stale "Waiting for Sync All confirmation" message after approving a batch. Uploads were proceeding, but the displayed status was never updated.

- Sync All immediately shows approval and a started notification.
- Status now reports comparing/syncing file N of M and cloud retries.
- Adds diagnostic progress logs without credentials or playback contents.
- Preserves one confirmation per batch, public-upload warning, merged histories and backups.

The public build is token-free. SideStore/JIT-less: fully close GD, import the exact **local.attempt_sync.geode** file using **Files → Share → Geode / Geode Launcher**, wait for import confirmation, then use launcher **Settings → Force Resign** and launch GD.

Includes Windows x64, macOS Intel/Apple Silicon, and iOS/iPadOS arm64. Requires GD 2.2081, Geode 5.11.1+ (5.x), and Attempt Playback 1.5.7.
