One **Sync All** confirmation now covers the entire playback batch, including 55+ files. Cancel stops before any uploads or replacements.

- Larger centered summary text, short rows, and no clipped scrolling paragraphs.
- Shows total sizes/attempts, files to upload/update, and local-only/cloud-only recordings.
- Retains the public-upload warning, merged histories, and local backups.
- Cloud conflicts retry without another prompt; local changes during syncing remain protected.
- Validates every file before approval. Includes the APX practice format fixes from 1.2.1.

The public build is token-free. SideStore/JIT-less: fully close GD, import the exact **local.attempt_sync.geode** file using **Files → Share → Geode / Geode Launcher**, wait for import confirmation, then use launcher **Settings → Force Resign** and launch GD.

Includes Windows x64, macOS Intel/Apple Silicon, and iOS/iPadOS arm64. Requires GD 2.2081, Geode 5.11.1+ (5.x), and Attempt Playback 1.5.7.
