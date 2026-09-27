#!/usr/bin/env bash
# Build the Fable 2 ReXGlue project on macOS or Linux (the counterpart of
# build.cmd on Windows).
#
# Toolchain: clang + Ninja + CMake >= 3.25. On macOS the Xcode command line
# tools provide clang (xcode-select --install); get CMake and Ninja from
# Homebrew (brew install cmake ninja). Preset: <platform>-debug by default,
# <platform>-release with -release, where <platform> is mac-arm64,
# mac-amd64 or linux-amd64 (see CMakePresets.json).
#
# Usage:
#   ./build.sh                  build fable_2_codegen (runs codegen from fable_2_manifest.toml)
#   ./build.sh fable_2          build the full recompiled executable
#   ./build.sh <other target>   build any other CMake target
#   ./build.sh -release [t]     build as Release (-O3) instead of Debug
#   ./build.sh -r [t]           (same, short form)
#
# Codegen needs default.xex in the repo root (see README).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

case "$(uname -s)-$(uname -m)" in
    Darwin-arm64)  PLAT="mac-arm64" ;;
    Darwin-x86_64) PLAT="mac-amd64" ;;
    Linux-x86_64)  PLAT="linux-amd64" ;;
    *)
        echo "Error: unsupported host $(uname -s) $(uname -m)." >&2
        exit 1
        ;;
esac

for tool in cmake ninja clang++; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Error: $tool not found on PATH." >&2
        exit 1
    fi
done

# Prebuilt ReXGlue SDK: thirdparty/rexglue-sdk-prebuilt/<platform>, fetched on
# first use. (thirdparty/rexglue-sdk is the SDK source submodule, which the
# Windows Release build uses; macOS/Linux build against the prebuilt SDK.)
REXSDK="$PWD/thirdparty/rexglue-sdk-prebuilt/$PLAT"
if [ ! -f "$REXSDK/lib/cmake/rexglue/rexglueConfig.cmake" ]; then
    echo "ReXGlue SDK not found; downloading via tools/setup_sdk.sh ..."
    tools/setup_sdk.sh
fi

# generated/ is not tracked, and CMakeLists.txt includes
# generated/rexglue.cmake, so a fresh checkout cannot configure until codegen
# has written it. Codegen writes that file first, before it needs default.xex.
# It is also SDK-specific, so it is regenerated whenever the installed SDK
# differs from the one recorded in generated/.rexglue_sdk_version (the file
# itself only pins major.minor.patch, which cannot tell a nightly apart).
sdk_ver="$(sed -n 's/^set(PACKAGE_VERSION "\(.*\)")$/\1/p' \
    "$REXSDK/lib/cmake/rexglue/rexglueConfigVersion.cmake")"
stamp=generated/.rexglue_sdk_version
if [ ! -f generated/rexglue.cmake ] || [ "$(cat "$stamp" 2>/dev/null)" != "$sdk_ver" ]; then
    echo "generated/rexglue.cmake missing or for SDK $(cat "$stamp" 2>/dev/null || echo none)" \
         "(installed: $sdk_ver); running rexglue codegen to (re)create it ..."
    rm -f generated/rexglue.cmake
    "$REXSDK/bin/rexglue" codegen fable_2_manifest.toml || true
    if [ ! -f generated/rexglue.cmake ]; then
        echo "Error: rexglue codegen did not create generated/rexglue.cmake." >&2
        exit 1
    fi
    echo "$sdk_ver" > "$stamp"
fi
# The generated file names the manifest and the generated sources after the
# project; an SDK that derives those names differently (the 0.10.0.15 nightly
# turns fable_2 into fable2) produces a build that cannot find them.
manifest="$(sed -n 's/.*codegen \${CMAKE_CURRENT_SOURCE_DIR}\/\([^ ]*_manifest\.toml\).*/\1/p' \
    generated/rexglue.cmake | head -1)"
if [ -n "$manifest" ] && [ ! -f "$manifest" ]; then
    echo "Error: ReXGlue SDK $sdk_ver names this project's files '${manifest%_manifest.toml}_*'," \
         "but the project uses 'fable_2_*' (fable_2_manifest.toml, fable_2_pch.h, ...)." >&2
    echo "       This SDK version is incompatible with the project; go back to the" \
         "release SDK with:  tools/setup_sdk.sh && ./build.sh" >&2
    rm -f generated/rexglue.cmake "$stamp"
    exit 1
fi
if [ ! -f default.xex ]; then
    echo "Warning: default.xex not found in the repo root; codegen will fail" \
         "until the game content is extracted here (see README)." >&2
fi

CONFIG="$PLAT-debug"
TARGET=""
for arg in "$@"; do
    case "$arg" in
        -release|-r) CONFIG="$PLAT-release" ;;
        *) TARGET="$arg" ;;
    esac
done
TARGET="${TARGET:-fable_2_codegen}"

# CC / CXX in the environment override the preset's compilers (e.g. the
# Linux presets ask for clang-20).
EXTRA=()
[ -n "${CC:-}" ] && EXTRA+=("-DCMAKE_C_COMPILER=$CC")
[ -n "${CXX:-}" ] && EXTRA+=("-DCMAKE_CXX_COMPILER=$CXX")

# rexglue_DIR is passed explicitly: find_package caches it, so a build dir
# configured against an older SDK location would otherwise keep using it.
cmake --preset "$CONFIG" \
    -DCMAKE_PREFIX_PATH="$REXSDK" \
    -Drexglue_DIR="$REXSDK/lib/cmake/rexglue" \
    -DREXGLUE_SDK_ROOT="$REXSDK" \
    ${EXTRA[@]+"${EXTRA[@]}"}
cmake --build "out/build/$CONFIG" --target "$TARGET"

# The SDK's CMake helpers copy the prebuilt GPU plugin and runtime next to
# the executable on every link, replacing ones built from source by
# tools/build_sdk_mac.sh (which leaves *.prebuilt backups behind as the sign
# that it staged). Put the source-built ones back after each build.
if [ "$(uname -s)" = Darwin ] && ls "out/build/$CONFIG"/*.dylib.prebuilt >/dev/null 2>&1; then
    stage_args=()
    [ "$CONFIG" = "$PLAT-debug" ] && stage_args+=(-debug)
    echo "Re-staging the source-built ReXGlue plugin/runtime (tools/build_sdk_mac.sh -stage) ..."
    tools/build_sdk_mac.sh -stage ${stage_args[@]+"${stage_args[@]}"} ||
        echo "Warning: could not re-stage the source-built SDK; the game now uses the prebuilt plugin." >&2
fi
