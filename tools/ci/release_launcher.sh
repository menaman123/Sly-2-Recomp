#!/usr/bin/env bash
# tools/ci/release_launcher.sh -- the release a player downloads, for macOS (arm64 / x86_64) and Linux (x86_64):
#
#   <product>-<tag>-<platform>.zip          (release.asset_pattern in launcher/title.json)
#   └── <product>-<platform>/               (release.bundle_folder)
#       ├── <Name>.app | <product>-launcher  the engine's launcher (engine/app/launcher, Qt 6 Widgets, dynamic Qt, LGPL)
#       │   └── Contents/Resources/titles/title.json, BUILD-ID, LICENSE, THIRD_PARTY_NOTICES.md, licenses/
#       ├── engine/                          the prebuilt engine bundle for this platform (--engine-bundle DIR)
#       └── tools/                           the pinned portable compiler + linker for this platform (--compiler-bundle DIR)
#
# Nothing game-derived is in it (decision D-A): the player's disc is read and the game prepared on their machine, by the
# launcher, with the compiler in tools/. The engine/ and tools/ halves are produced by the prepare track's packaging and
# are COPIED here; this script fails when they are asked for and missing, and stages a launcher-only zip (for the
# launcher's own CI) when --launcher-only is given.
#
#   tools/ci/release_launcher.sh [--qt PREFIX] [--build-dir DIR] [--dist-dir DIR] [--engine DIR]
#                                [--engine-bundle DIR] [--compiler-bundle DIR] [--launcher-only] [--skip-build] [--tag TAG]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
QT="${QT_PREFIX:-/opt/homebrew/opt/qt6}"; BUILD="$ROOT/build-launcher"; DIST_ROOT="$ROOT/dist"; ENGINE="$ROOT/engine"
ENGINE_BUNDLE=""; COMPILER_BUNDLE=""; LAUNCHER_ONLY=0; SKIP_BUILD=0; TAG=""
while [ $# -gt 0 ]; do
  case "$1" in
    --qt) QT="$2"; shift 2 ;;
    --build-dir) BUILD="$2"; shift 2 ;;
    --dist-dir) DIST_ROOT="$2"; shift 2 ;;
    --engine) ENGINE="$2"; shift 2 ;;
    --engine-bundle) ENGINE_BUNDLE="$2"; shift 2 ;;
    --compiler-bundle) COMPILER_BUNDLE="$2"; shift 2 ;;
    --launcher-only) LAUNCHER_ONLY=1; shift ;;
    --skip-build) SKIP_BUILD=1; shift ;;
    --tag) TAG="$2"; shift 2 ;;
    -h|--help) sed -n '2,19p' "$0"; exit 0 ;;
    *) echo "release_launcher: unknown argument $1" >&2; exit 2 ;;
  esac
done
case "$BUILD" in /*) ;; *) BUILD="$ROOT/$BUILD" ;; esac
case "$DIST_ROOT" in /*) ;; *) DIST_ROOT="$ROOT/$DIST_ROOT" ;; esac
step() { echo; echo "=== $*"; }
[ -f "$ENGINE/app/launcher/CMakeLists.txt" ] || { echo "release_launcher: the engine at $ENGINE has no app/launcher" >&2; exit 2; }
MANIFEST="$ROOT/launcher/title.json"
field() { python3 -c "import json,sys;print(json.load(open(sys.argv[1]))$2)" "$MANIFEST"; }
PRODUCT="$(field "" "['product']")"; NAME="$(field "" "['name']")"; TITLE_ID="$(field "" "['id']")"
SAFE_NAME="$(echo "$NAME" | tr ':/\\*?"<>|' '         ' | tr -s ' ' | sed 's/ *$//')"   # a file name every file system takes
ASSET_PATTERN="$(field "" "['release']['asset_pattern']")"; BUNDLE_PATTERN="$(field "" "['release']['bundle_folder']")"
OS="$(uname -s)"; ARCH="$(uname -m)"
case "$OS" in Darwin) PLAT_OS=macos ;; Linux) PLAT_OS=linux ;; *) echo "release_launcher: $OS is not a target here (Windows: tools/ci/release_launcher.ps1)" >&2; exit 2 ;; esac
case "$ARCH" in arm64|aarch64) PLAT_ARCH=arm64 ;; x86_64|amd64) PLAT_ARCH=x86_64 ;; *) PLAT_ARCH="$ARCH" ;; esac
PLATFORM="$PLAT_OS-$PLAT_ARCH"
[ "$PLAT_OS" = linux ] && [ "$PLAT_ARCH" = x86_64 ] && PLATFORM="linux-x86_64"
python3 - "$MANIFEST" "$PLATFORM" <<'EOF' || { echo "release_launcher: $PLATFORM is not one of the manifest's targets" >&2; exit 2; }
import json, sys
sys.exit(0 if sys.argv[2] in json.load(open(sys.argv[1]))["targets"] else 1)
EOF
[ -n "$TAG" ] || TAG="$(git -C "$ROOT" describe --tags --always 2>/dev/null || git -C "$ROOT" rev-parse --short HEAD)"
ASSET="$(echo "$ASSET_PATTERN" | sed "s|\${product}|$PRODUCT|g; s|\${tag}|$TAG|g; s|\${platform}|$PLATFORM|g")"
BUNDLE="$(echo "$BUNDLE_PATTERN" | sed "s|\${product}|$PRODUCT|g; s|\${platform}|$PLATFORM|g")"
echo "product $PRODUCT ($TITLE_ID) · platform $PLATFORM · tag $TAG · asset $ASSET"

if [ "$SKIP_BUILD" = 0 ]; then
  step "build the launcher ($ENGINE/app/launcher -> $BUILD)"
  cmake -S "$ENGINE/app/launcher" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$QT" \
        "-DPS2R_LAUNCHER_VERSION=$TAG" > "$BUILD.configure.log" 2>&1 || { tail -30 "$BUILD.configure.log"; exit 1; }
  mkdir -p "$BUILD"; mv "$BUILD.configure.log" "$BUILD/configure.log"
  cmake --build "$BUILD" > "$BUILD/build.log" 2>&1 || { grep -n "error\|FAILED" "$BUILD/build.log" | head -20; exit 1; }
  step "the launcher's tests (LauncherModels, LauncherScreens, LauncherLint, LauncherOneClass)"
  (cd "$BUILD" && QT_QPA_PLATFORM=offscreen ctest --output-on-failure 2>&1 | tail -8)
fi

DIST="$DIST_ROOT/$BUNDLE"
step "stage $DIST"
rm -rf "$DIST"; mkdir -p "$DIST"
if [ "$PLAT_OS" = macos ]; then
  APP_SRC="$BUILD/ps2r_launcher.app"; [ -d "$APP_SRC" ] || { echo "release_launcher: no $APP_SRC" >&2; exit 1; }
  APP="$DIST/$SAFE_NAME.app"; cp -R "$APP_SRC" "$APP"; RES="$APP/Contents/Resources"
  PLIST="$APP/Contents/Info.plist"
  /usr/libexec/PlistBuddy -c "Set :CFBundleName $NAME" "$PLIST"
  /usr/libexec/PlistBuddy -c "Add :CFBundleDisplayName string $NAME" "$PLIST" 2>/dev/null || /usr/libexec/PlistBuddy -c "Set :CFBundleDisplayName $NAME" "$PLIST"
  /usr/libexec/PlistBuddy -c "Set :CFBundleIdentifier dev.nativeee.launcher.$(echo "$TITLE_ID" | tr 'A-Z' 'a-z')" "$PLIST"
  /usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $TAG" "$PLIST" 2>/dev/null || /usr/libexec/PlistBuddy -c "Add :CFBundleShortVersionString string $TAG" "$PLIST"
else
  BIN_SRC="$BUILD/ps2r_launcher"; [ -x "$BIN_SRC" ] || { echo "release_launcher: no $BIN_SRC" >&2; exit 1; }
  mkdir -p "$DIST/launcher"; cp "$BIN_SRC" "$DIST/launcher/$PRODUCT-launcher"; RES="$DIST/launcher"
fi
mkdir -p "$RES/titles" "$RES/licenses"
cp "$MANIFEST" "$RES/titles/title.json"
{
  echo "$PRODUCT launcher $TAG ($PLATFORM)"
  echo "title   $(git -C "$ROOT" rev-parse HEAD)"
  echo "engine  $(git -C "$ENGINE" rev-parse HEAD)"
  echo "qt      $("$QT/bin/qmake" -query QT_VERSION 2>/dev/null || echo unknown)"
  echo "built   $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$RES/BUILD-ID"
cat "$RES/BUILD-ID"
[ -f "$ROOT/LICENSE" ] && cp "$ROOT/LICENSE" "$RES/LICENSE"
QT_VERSION="$("$QT/bin/qmake" -query QT_VERSION 2>/dev/null || echo unknown)"
for f in LICENSE.LGPL3 LICENSE.GPL3-EXCEPT LICENSE.GPL3 LICENSE.FDL; do [ -f "$QT/share/qt/licenses/$f" ] && cp "$QT/share/qt/licenses/$f" "$RES/licenses/Qt-$f" || true; done
[ -f "$RES/licenses/Qt-LICENSE.LGPL3" ] || { [ -f "$ENGINE/tools/dist/licenses/Qt-LICENSE.LGPL3.txt" ] && cp "$ENGINE/tools/dist/licenses/Qt-LICENSE.LGPL3.txt" "$RES/licenses/Qt-LICENSE.LGPL3" || true; }
{
  echo "# $NAME -- third-party notices -- $(head -1 "$RES/BUILD-ID")"
  echo
  echo "This application is the NativeEE engine's launcher (engine/app/launcher, MIT) carrying this title's description. It"
  echo "contains NO game data and no code derived from the game: it reads your own disc image and prepares the game on your"
  echo "machine with the tools in tools/. Qt $QT_VERSION is linked dynamically under the LGPL v3 (licenses/Qt-LICENSE.LGPL3)"
  echo "and may be replaced by another build of the same major version. The engine bundle in engine/ and the tools in"
  echo "tools/ carry their own notices."
} > "$RES/THIRD_PARTY_NOTICES.md"

if [ "$PLAT_OS" = macos ]; then
  step "macdeployqt (Qt frameworks and plugins into the bundle, dynamic) + codesign"
  [ -x "$QT/bin/macdeployqt" ] || { echo "release_launcher: no macdeployqt under $QT/bin" >&2; exit 2; }
  "$QT/bin/macdeployqt" "$APP" -no-strip > "$DIST_ROOT/macdeployqt-$PLATFORM.log" 2>&1 || { tail -20 "$DIST_ROOT/macdeployqt-$PLATFORM.log"; exit 1; }
  codesign --force --deep --sign "${CODESIGN_IDENTITY:--}" "$APP" && codesign --verify --deep --strict "$APP"
fi

if [ "$LAUNCHER_ONLY" = 0 ]; then
  step "the engine bundle and the compiler (from the prepare track's packaging)"
  [ -n "$ENGINE_BUNDLE" ] && [ -d "$ENGINE_BUNDLE" ] || { echo "release_launcher: --engine-bundle DIR (the prebuilt engine for $PLATFORM) is required; --launcher-only stages the launcher alone" >&2; exit 2; }
  [ -n "$COMPILER_BUNDLE" ] && [ -d "$COMPILER_BUNDLE" ] || { echo "release_launcher: --compiler-bundle DIR (the pinned portable compiler + linker for $PLATFORM) is required" >&2; exit 2; }
  cp -R "$ENGINE_BUNDLE" "$DIST/engine"; cp -R "$COMPILER_BUNDLE" "$DIST/tools"
fi

step "no game-derived file in the staged folder"
[ -f "$ENGINE/tools/ci/asset_scan.py" ] && python3 -P "$ENGINE/tools/ci/asset_scan.py" "$DIST" || echo "(asset scan not available in this engine; skipped)"

step "$DIST_ROOT/$ASSET"
(cd "$DIST_ROOT" && rm -f "$ASSET" "$ASSET.sha256" && zip -qry "$ASSET" "$BUNDLE" && shasum -a 256 "$ASSET" > "$ASSET.sha256")
ls -la "$DIST_ROOT/$ASSET"; cat "$DIST_ROOT/$ASSET.sha256"
