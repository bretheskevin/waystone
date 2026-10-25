#!/usr/bin/env bash
# Headless Android emulator smoke test: boot AVD, install debug APK, launch, assert Setup screen.
# Never talks to any server. Usage: bash android/scripts/smoke.sh   (env: SMOKE_KEEP_EMULATOR=1 to leave it running)
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
export ANDROID_HOME="${ANDROID_HOME:-/opt/homebrew/share/android-commandlinetools}"
export ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-$ANDROID_HOME/ndk/27.1.12297006}"
AVD=waystone-smoke
PORT=5580
SERIAL="emulator-$PORT"
PKG=dev.waystone
ACTIVITY="$PKG/dev.waystone.app.MainActivity"
OUT="${SMOKE_OUT:-$REPO/android/build/smoke}"
case "$(uname -m)" in arm64|aarch64) ABI=arm64-v8a ;; *) ABI=x86_64 ;; esac
IMAGE="system-images;android-34;google_apis;$ABI"
SDKMANAGER="$(command -v sdkmanager || echo "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager")"
AVDMANAGER="$(command -v avdmanager || echo "$ANDROID_HOME/cmdline-tools/latest/bin/avdmanager")"
EMULATOR="$ANDROID_HOME/emulator/emulator"
ADB="$ANDROID_HOME/platform-tools/adb"
EMU_PID=""

log() { echo "[smoke] $*"; }
fail() { echo "[smoke] FAIL: $*" >&2; exit 1; }
cleanup() {
  if [ -n "$EMU_PID" ] && [ "${SMOKE_KEEP_EMULATOR:-0}" != "1" ]; then
    log "stopping emulator $SERIAL"
    "$ADB" -s "$SERIAL" emu kill >/dev/null 2>&1 || kill "$EMU_PID" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

mkdir -p "$OUT"
log "repo=$REPO abi=$ABI out=$OUT"

if [ ! -x "$EMULATOR" ] || [ ! -x "$ADB" ] || [ ! -d "$ANDROID_HOME/system-images/android-34/google_apis/$ABI" ]; then
  log "installing emulator, platform-tools and $IMAGE"
  yes | "$SDKMANAGER" --licenses >/dev/null || true
  "$SDKMANAGER" --install emulator platform-tools "$IMAGE" || fail "sdkmanager install failed"
fi

# No `grep -q`: an early exit would SIGPIPE the producer and trip pipefail.
if ! "$EMULATOR" -list-avds 2>/dev/null | grep -x "$AVD" >/dev/null; then
  log "creating AVD $AVD"
  echo no | "$AVDMANAGER" create avd -n "$AVD" -k "$IMAGE" -d pixel_5 --force || fail "avdmanager create failed"
fi

log "building debug APK (cargoNdkBuild + assembleDebug)"
(cd "$REPO/android" && ./gradlew :engine:cargoNdkBuild :app:assembleDebug) > "$OUT/gradle.log" 2>&1 \
  || { tail -40 "$OUT/gradle.log"; fail "gradle build failed (see $OUT/gradle.log)"; }
APK="$REPO/android/app/build/outputs/apk/debug/app-debug.apk"
[ -f "$APK" ] || fail "APK not found: $APK"
unzip -l "$APK" | grep -E "lib/$ABI/lib(waystone_mobile|jnidispatch)\.so" > "$OUT/native-libs.txt" \
  || fail "APK lacks lib/$ABI native libs"
log "APK ok: $(wc -l < "$OUT/native-libs.txt" | tr -d ' ') native libs for $ABI"

log "booting $AVD headless on port $PORT"
"$EMULATOR" -avd "$AVD" -port "$PORT" -no-window -no-audio -no-boot-anim -no-snapshot \
  -gpu swiftshader_indirect > "$OUT/emulator.log" 2>&1 &
EMU_PID=$!
"$ADB" -s "$SERIAL" wait-for-device
for i in $(seq 1 150); do
  [ "$("$ADB" -s "$SERIAL" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ] && break
  kill -0 "$EMU_PID" 2>/dev/null || { tail -20 "$OUT/emulator.log"; fail "emulator process exited"; }
  sleep 2
done
[ "$("$ADB" -s "$SERIAL" shell getprop sys.boot_completed | tr -d '\r')" = "1" ] || fail "boot timeout (300s)"
log "booted"

"$ADB" -s "$SERIAL" install -r "$APK" > "$OUT/install.log" 2>&1 || { cat "$OUT/install.log"; fail "adb install failed"; }
"$ADB" -s "$SERIAL" shell pm clear "$PKG" >/dev/null
"$ADB" -s "$SERIAL" logcat -c
log "launching $ACTIVITY"
"$ADB" -s "$SERIAL" shell am start -W -n "$ACTIVITY" | tee "$OUT/am-start.txt"
sleep 12

"$ADB" -s "$SERIAL" logcat -d > "$OUT/logcat.txt"
"$ADB" -s "$SERIAL" logcat -d -b crash > "$OUT/logcat-crash.txt"
"$ADB" -s "$SERIAL" exec-out screencap -p > "$OUT/smoke.png"

"$ADB" -s "$SERIAL" shell pidof "$PKG" >/dev/null || fail "process $PKG not running (see $OUT/logcat.txt)"
if grep -E "FATAL EXCEPTION|UnsatisfiedLinkError" "$OUT/logcat.txt" >/dev/null || grep -F "$PKG" "$OUT/logcat-crash.txt" >/dev/null; then
  grep -n -A20 -E "FATAL EXCEPTION|UnsatisfiedLinkError" "$OUT/logcat.txt" | head -60
  fail "crash in logcat"
fi
grep -F "native engine loaded" "$OUT/logcat.txt" >/dev/null || fail "no 'native engine loaded' line (JNA/libwaystone_mobile load failed?)"

"$ADB" -s "$SERIAL" shell uiautomator dump /sdcard/ws-ui.xml >/dev/null
"$ADB" -s "$SERIAL" pull /sdcard/ws-ui.xml "$OUT/ui.xml" >/dev/null
grep -F "Set up Waystone" "$OUT/ui.xml" >/dev/null || fail "Setup screen not shown (see $OUT/ui.xml, $OUT/smoke.png)"

log "PASS — screenshot: $OUT/smoke.png  logcat: $OUT/logcat.txt"
