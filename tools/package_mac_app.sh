#!/usr/bin/env bash
# Package the built macOS game as a double-clickable .app under out/dist.
#
# Usage: tools/package_mac_app.sh [--with-settings] [--no-sign]
#
# The app does NOT include the game content. Put finished app and the content
# (default.xex, data/) in one folder, then double-click the app. Saves, cache,
# fable_2.toml and fable2_config.toml are written next to the .app, not inside
# the bundle.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
REPO=$PWD
BUILD=out/build/mac-arm64-release
DIST=out/dist
APP="$DIST/Fable 2 Recompiled.app"
WITH_SETTINGS=0
SIGN=1
for arg in "$@"; do
    case "$arg" in
        --with-settings) WITH_SETTINGS=1 ;;
        --no-sign) SIGN=0 ;;
        -h|--help) sed -n '2,10p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown argument: $arg" >&2; exit 1 ;;
    esac
done

if [ ! -x "$BUILD/fable_2" ]; then
    echo "Error: no macOS build at $BUILD. Build it first: ./build.sh -release fable_2" >&2
    exit 1
fi

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp -p "$BUILD/fable_2" "$APP/Contents/MacOS/"
shopt -s nullglob
for f in "$BUILD"/*.dylib; do
    cp -p "$f" "$APP/Contents/MacOS/"
done
shopt -u nullglob
if [ -d "$BUILD/vulkan" ]; then
    # The SDK's bundle lookup finds Contents/Resources/vulkan (with lib/ and
    # share/vulkan/icd.d/). Keeping it out of MacOS also stops codesign from
    # treating the icd.d folder as nested code.
    cp -Rp "$BUILD/vulkan" "$APP/Contents/Resources/vulkan"
fi
cp -p assets/macos/Info.plist "$APP/Contents/Info.plist"
cp -p assets/app-icon.icns "$APP/Contents/Resources/"
cp -p docs/MACOS_KNOWN_ISSUES.md "$APP/Contents/Resources/"

if [ "$WITH_SETTINGS" = 1 ]; then
    for f in fable2_config.toml fable_2.toml; do
        if [ -f "$BUILD/$f" ]; then
            cp -p "$BUILD/$f" "$DIST/$f"
        fi
    done
    # The build's fable_2.toml pins game_data_root to this checkout. The app
    # defaults the content root to the folder that holds the .app, so drop the
    # absolute path (and any log_file) for a folder that can be moved.
    if [ -f "$DIST/fable_2.toml" ]; then
        grep -viE '^(game_data_root|log_file)[[:space:]]*=' "$DIST/fable_2.toml" \
            > "$DIST/fable_2.toml.tmp" && mv "$DIST/fable_2.toml.tmp" "$DIST/fable_2.toml"
    fi
    echo "Copied the build's settings next to the app: $DIST/fable2_config.toml, fable_2.toml (build paths removed)"
fi

if [ "$SIGN" = 1 ]; then
    # --deep trips over the Vulkan ICD folder (it is not code); sign the
    # nested dylibs explicitly and then the bundle.
    shopt -s nullglob
    for f in "$APP/Contents/MacOS"/*.dylib "$APP/Contents/Resources"/vulkan/lib/*.dylib; do
        codesign --force --sign - "$f" >/dev/null
    done
    shopt -u nullglob
    codesign --force --sign - "$APP" >/dev/null
    codesign --verify --strict "$APP"
    echo "Ad-hoc signed (local use only; not notarized)."
fi

echo "Built: $APP"
echo "Put the app and the game content (default.xex, data/) in one folder, then double-click it."
echo "Saves, cache, logs and the toml files are written next to the .app, not inside it."
