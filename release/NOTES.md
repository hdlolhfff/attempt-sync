Fixes **Truncated APX chunk** on valid Attempt Playback practice recordings.

- Corrects the packed APX v5 practice segment size from 52 to **48 bytes**.
- Accepts appended practice PATH snapshots and uses the newest one, matching Attempt Playback's writer/reader.
- Adds regression tests using the published packed struct fields, independent of the parser's offset constants. The old parser reproduces the user's exact error on this valid fixture.
- Includes the playback filename in future sync errors.

Size/attempt comparisons and Sync/Cancel confirmation remain in owner mode. The built-in source is **hdlolhfff/gd-attempts**. The public download is token-free; personal credentials stay local.

**SideStore/JIT-less installation:** Fully close GD. Save the download with the exact filename **local.attempt_sync.geode**, then use **Files → Share → Geode / Geode Launcher** and wait for import confirmation. Use launcher **Settings → Force Resign**, then launch GD. This stages a library for signing that Geode preserves while extracting the package. A manual copy into mods or an in-game import can leave the staging copy missing.

Includes Windows x64, macOS Intel/Apple Silicon, and iOS/iPadOS arm64. Requires GD 2.2081, compatible Geode 5.11.1+ (5.x), and Attempt Playback 1.5.7. [Setup instructions](https://github.com/hdlolhfff/attempt-sync#readme).
