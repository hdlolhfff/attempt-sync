**Owner sync now compares files and asks before changing them.**

For every changed level, the Review Playback Sync dialog shows:
- Local and GitHub file sizes, and which is larger.
- Attempt counts on each side.
- Local-only and GitHub-only unique recordings, compared by content rather than device serial numbers.
- A warning that uploaded recordings are downloadable by anyone when gd-attempts is public.

Tap **Sync** to combine both histories, or **Cancel** to stop before changing that file. Files completed earlier in the batch are kept. Unchanged files do not prompt. Automatic owner sync still requires confirmation for changed files; comparisons completed during gameplay wait for the main menu. Viewers only download.

The source remains built in: **hdlolhfff/gd-attempts**, branch **main**. Larger byte size suggests more stored data but does not necessarily mean more attempts, so the mod also compares recording contents. It merges histories and backs up replaced local files.

The owner has authorized public viewing; GitHub visibility is controlled in the data repository's settings. Public viewing needs no token. Uploads still require the owner's token, which is never embedded in the mod.

Download **local.attempt_sync.geode** below. Includes Windows x64, macOS Intel/Apple Silicon, and iOS/iPadOS arm64. Requires GD 2.2081, compatible Geode 5.11.1+ (5.x), and Attempt Playback 1.5.7.

**SideStore + JIT-less:** after updating, fully close GD and use **Geode launcher → Settings → Force Resign**, then launch again. [Setup instructions](https://github.com/hdlolhfff/attempt-sync#readme).
