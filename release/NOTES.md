Fixes the **“missing code signature”** error that prevented Attempt Sync 1.0.0 from loading on iOS/iPadOS.

- Ad-hoc signs the final iOS dylib before packaging.
- Verifies the packaged signature with Apple's `codesign --verify --strict`.
- Checks signature presence and executable page hashes again after combining all platforms, and before publishing.

Download **local.attempt_sync.geode** below and replace the older Attempt Sync mod in your Geode launcher, then fully restart GD. This single package includes iOS/iPadOS arm64, macOS Intel/Apple Silicon, and Windows x64. Your Attempt Playback data is unaffected by replacing the mod.

Requires GD 2.2081, Geode 5.11.1+ (compatible 5.x), and Attempt Playback 1.5.7. Sync setup remains the same: a private GitHub replay-data repository and a fine-grained token with Contents read/write in the mod's settings. Tap Sync on the main menu.

The signature checks address the reported load failure. Actual loading on your iPad and sync behavior still need device testing. [Full setup instructions](https://github.com/hdlolhfff/attempt-sync#readme).
