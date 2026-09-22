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
