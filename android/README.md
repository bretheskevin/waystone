# Waystone - Android Shell

> Milestone 3. Requires Android SDK/NDK + UniFFI.
> See ARCHITECTURE.md for design details.

## Toolchain

- Android SDK: platform `android-34`, build-tools `34.0.0`, platform-tools
- NDK: `27.1.12297006`
- Rust Android targets: `aarch64-linux-android`, `x86_64-linux-android` (via rustup)
- cargo-ndk (builds `libwaystone_mobile.so` into `engine/src/main/jniLibs/` via `./gradlew :engine:cargoNdkBuild`)

## Environment (prefix every shell invocation)

```sh
export ANDROID_HOME=/opt/homebrew/share/android-commandlinetools
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/27.1.12297006"
```

## Build

```sh
cd android
./gradlew :app:testDebugUnitTest     # compile all modules + run unit tests
./gradlew :engine:cargoNdkBuild :app:assembleDebug   # build Rust jniLibs + APK
```

## Version

`versionName`/`versionCode` come from the repo-root `VERSION` (`X.Y.Z`; code = X*10000 + Y*100 + Z).
Bump `VERSION` once per release — it also feeds the 3DS/Switch builds.

## Release & signing

Release APKs are signed only when these env vars are set (CI sets them from repo secrets):
`WAYSTONE_KEYSTORE_PATH`, `WAYSTONE_KEYSTORE_PASSWORD`, `WAYSTONE_KEY_ALIAS`, `WAYSTONE_KEY_PASSWORD`.
Without them `assembleRelease` produces `app-release-unsigned.apk`; debug builds and unit tests never need them.
The release keystore lives at `~/.waystone/android-release.jks` (+ `android-release.properties`, chmod 600) on the
maintainer's machine — never commit it; losing it means users must uninstall to update.
CI (`.github/workflows/release.yml` `build-android`) publishes `waystone.apk` — the in-app updater matches the `.apk`
suffix, never rename the asset.

## Smoke test (no device)

`bash android/scripts/smoke.sh` boots a headless emulator, installs the debug APK and asserts the Setup screen.
