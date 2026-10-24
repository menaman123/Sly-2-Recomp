#!/usr/bin/env bash
# release_prepare_assets.sh -- the two release assets the prepared-game pipeline downloads (decision D-A):
#   dist/engine-bundle-<platform>.tar.xz   the engine bundle of THIS build (cmake --target engine_bundle, asset-scanned)
#   dist/compiler-<platform>.tar.xz        the pinned portable compiler, trimmed from the pinned LLVM archive
#   dist/SHA256SUMS                        both digests (the release index carries them as asset digests too)
#
#   release_prepare_assets.sh <build dir> <release tag> <llvm root> [<platform>]
#
# The CI job: configure with -DPS2R_RELEASE_TAG=<tag> (and the disc / ELF the build regenerates from -- the
# bundle itself carries nothing game-derived), build, run this, upload dist/* to the GitHub release. The
# pinned LLVM archives per platform and their sha256 are in docs/launcher/PREPARE.md; CI downloads the one
# for its runner, verifies the digest, unpacks it and passes the root here.
set -euo pipefail
BUILD=$1; TAG=$2; LLVM=$3; PLATFORM=${4:-}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ENGINE=$ROOT/engine
if [ -z "$PLATFORM" ]; then
  case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) PLATFORM=macos-arm64;; Darwin-x86_64) PLATFORM=macos-x86_64;;
    Linux-x86_64) PLATFORM=linux-x86_64;; MINGW*|MSYS*|CYGWIN*) PLATFORM=windows-x86_64;;
    *) echo "release_prepare_assets: unknown host $(uname -s)-$(uname -m); pass the platform" >&2; exit 2;;
  esac
fi
DIST=$ROOT/dist; mkdir -p "$DIST"
cmake --build "$BUILD" --target engine_bundle
python3 "$ENGINE/tools/ci/asset_scan.py" "$BUILD/dist/engine-bundle-$PLATFORM" --report "$DIST/engine-bundle-$PLATFORM.asset_scan.md"
( cd "$BUILD/dist" && XZ_OPT="-9 -T0" tar -cJf "$DIST/engine-bundle-$PLATFORM.tar.xz" "engine-bundle-$PLATFORM" )
python3 "$ENGINE/tools/ci/make_compiler_bundle.py" --llvm-root "$LLVM" --platform "$PLATFORM" --out "$DIST" --xz-level 9
( cd "$DIST" && shasum -a 256 "engine-bundle-$PLATFORM.tar.xz" "compiler-$PLATFORM.tar.xz" > SHA256SUMS )
echo "release_prepare_assets: $TAG $PLATFORM"; ls -la "$DIST"; cat "$DIST/SHA256SUMS"
