The playback repository is now built in: **hdlolhfff/gd-attempts**, branch **main**. Repository and Branch text fields have been removed, so no slash entry is needed in Geometry Dash.

- **Owner devices:** enable **Upload My Attempts**, enter your own GitHub token with Contents read/write for gd-attempts, and tap Sync. Recordings merge between your devices.
- **Viewers:** leave Upload My Attempts off (the default). Sync only downloads your playbacks and never uploads the viewer's attempts. Public viewing needs no token; private viewing requires each viewer's own authorized read-only token. The mod does not change repository visibility or embed/share the owner's upload token.

Download **local.attempt_sync.geode** below and replace the old mod. It includes Windows x64, macOS Intel/Apple Silicon, and iOS/iPadOS arm64.

**SideStore + JIT-less:** after replacing the mod, fully close GD and use **Geode launcher → Settings → Force Resign**, then launch again. The launcher must sign mods with your device's certificate.

Requires GD 2.2081, compatible Geode 5.11.1+ (5.x), and Attempt Playback 1.5.7. The core tests include upload suppression in viewer mode; the build verifies the packaged iOS signature. [Setup instructions](https://github.com/hdlolhfff/attempt-sync#readme).
