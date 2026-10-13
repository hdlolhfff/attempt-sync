Fixes the **“missing code signature”** error that prevented Attempt Sync 1.0.0 from loading on iOS/iPadOS.

- Ad-hoc signs the final iOS dylib before packaging.
- Verifies the packaged signature with Apple's `codesign --verify --strict`.
- Checks signature presence and executable page hashes again after combining all platforms, and before publishing.

Download **local.attempt_sync.geode** below and replace the older Attempt Sync mod in your Geode launcher, then fully restart GD. This single package includes iOS/iPadOS arm64, macOS Intel/Apple Silicon, and Windows x64. Your Attempt Playback data is unaffected by replacing the mod.

Requires GD 2.2081, Geode 5.11.1+ (compatible 5.x), and Attempt Playback 1.5.7. Sync setup remains the same: a private GitHub replay-data repository and a fine-grained token with Contents read/write in the mod's settings. Tap Sync on the main menu.

The signature checks address the reported load failure. Actual loading on your iPad and sync behavior still need device testing. [Full setup instructions](https://github.com/hdlolhfff/attempt-sync#readme).


### SideStore / JIT-less: required device signing

If the error changes to **code signature invalid**, fully close GD, open the **Geode launcher → Settings → Force Resign**, wait for **Resign successful!**, then launch GD again. SideStore JIT-less requires signing with your device's SideStore certificate; the download's ad-hoc signature cannot replace that. If certificate validation fails, refresh/import your SideStore certificate in the launcher and test JIT-less before force resigning. Do not share your signing certificate.
