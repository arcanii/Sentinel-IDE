#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build build/macos/Sentinel-IDE.app from the Cargo workspace — the macOS counterpart of
# scripts\build.bat. Needs snc (see crates/sentinel-sys/build.rs for where it looks) and
# the Xcode command-line tools (cc, sips, iconutil, codesign, install_name_tool).
#
#   scripts/make-macos-app.sh            release build
#   PROFILE=debug scripts/make-macos-app.sh
#
# The bundle is signed AD HOC (codesign -s -): enough to run on this machine and to keep
# the Sentinel dylib loadable, not enough to pass Gatekeeper on someone else's. Developer ID
# signing and notarisation are milestone M5.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
PROFILE=${PROFILE:-release}
OUT="$ROOT/build/macos"
APP="$OUT/Sentinel-IDE.app"

if [ "$PROFILE" = release ]; then
  cargo build --manifest-path "$ROOT/Cargo.toml" -p host-macos --release
else
  cargo build --manifest-path "$ROOT/Cargo.toml" -p host-macos --profile "$PROFILE"
fi
BIN="$ROOT/target/$PROFILE/sentinel-ide"

# The dylib sentinel-sys built for this profile; the binary's build-tree rpath names it.
DYLIB_DIR=$(otool -l "$BIN" | awk '/LC_RPATH/{r=1} r&&/ path /{print $2; r=0}' | grep -v '^@' | head -1)
DYLIB="$DYLIB_DIR/libsentinel_parsers.dylib"
[ -f "$DYLIB" ] || { echo "no libsentinel_parsers.dylib beside $BIN's rpath ($DYLIB_DIR)" >&2; exit 1; }

# Version: the workspace's marketing version, and build.bat's build number formula
# (commit count + BUILDBASE) so the same commit stamps the same number on both platforms.
VERSION=$(awk -F'"' '/^\[workspace.package\]/{w=1} w&&/^version/{print $2; exit}' "$ROOT/Cargo.toml")
BUILDBASE=$(awk -F'=' '/^set "BUILDBASE=/{gsub(/"/,"",$2); print $2}' "$ROOT/scripts/build.bat" | tr -d '\r')
COUNT=$(git -C "$ROOT" rev-list --count HEAD)
BUILD=$((COUNT + BUILDBASE))
if [ -n "$(git -C "$ROOT" status --porcelain)" ]; then
  echo "BUILD_DIRTY $BUILD — the tree does not match HEAD; never ship this bundle" >&2
fi

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/Sentinel-IDE"
cp "$DYLIB" "$APP/Contents/Frameworks/"
# The bundle loads its own copy through @executable_path/../Frameworks; the build-tree
# rpath would let it pick up whatever the next `cargo build` left in target/ instead.
install_name_tool -delete_rpath "$DYLIB_DIR" "$APP/Contents/MacOS/Sentinel-IDE"
sed -e "s/@VERSION@/$VERSION/" -e "s/@BUILD@/$BUILD/" "$ROOT/packaging/macos/Info.plist" > "$APP/Contents/Info.plist"
printf 'APPL????' > "$APP/Contents/PkgInfo"

# Icons: the S2 shield for the app (554x657, so padded square first — sips keeps the
# transparency), the page-and-padlock for .sntproject/.sentinel documents.
make_icns() { # source.png name
  local tmp; tmp=$(mktemp -d)
  local set="$tmp/$2.iconset"; mkdir -p "$set"
  local w h side
  w=$(sips -g pixelWidth "$1" | awk '/pixelWidth/{print $2}')
  h=$(sips -g pixelHeight "$1" | awk '/pixelHeight/{print $2}')
  side=$(( w > h ? w : h ))
  sips --padToHeightWidth "$side" "$side" "$1" --out "$tmp/square.png" >/dev/null
  for s in 16 32 128 256 512; do
    sips -z "$s" "$s" "$tmp/square.png" --out "$set/icon_${s}x${s}.png" >/dev/null
    sips -z $((s * 2)) $((s * 2)) "$tmp/square.png" --out "$set/icon_${s}x${s}@2x.png" >/dev/null
  done
  iconutil -c icns "$set" -o "$APP/Contents/Resources/$2.icns"
  rm -rf "$tmp"
}
make_icns "$ROOT/art/S2_icon.png" AppIcon
make_icns "$ROOT/art/A_simple_clean_Windows_file_icon_design_for_a_sou-1782659827808.png" SentinelFile

codesign --force --sign - "$APP/Contents/Frameworks/libsentinel_parsers.dylib"
codesign --force --sign - "$APP"
codesign --verify --strict "$APP"

echo "MACOS_APP_OK $APP ($VERSION, build $BUILD)"
