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
# It also pins the SDK version that generated it (find_package(rexglue X.Y.Z)),
# so after switching to an older SDK (tools/setup_sdk.sh) configure would fail;
# regenerate it whenever the pinned version differs from the installed SDK.
# (Codegen pins major.minor.patch only, also for nightlies like 0.10.0.15.)
sdk_ver="$(sed -n 's/^set(PACKAGE_VERSION "\(.*\)")$/\1/p' \
    "$REXSDK/lib/cmake/rexglue/rexglueConfigVersion.cmake" | cut -d. -f1-3)"
gen_ver="$(sed -n 's/.*find_package(rexglue \([0-9][0-9.]*\) QUIET CONFIG).*/\1/p' \
    generated/rexglue.cmake 2>/dev/null)"
if [ ! -f generated/rexglue.cmake ] || [ "$gen_ver" != "$sdk_ver" ]; then
    echo "generated/rexglue.cmake missing or for SDK ${gen_ver:-none} (installed: $sdk_ver);" \
         "running rexglue codegen to (re)create it ..."
    rm -f generated/rexglue.cmake
    "$REXSDK/bin/rexglue" codegen fable_2_manifest.toml || true
    if [ ! -f generated/rexglue.cmake ]; then
        echo "Error: rexglue codegen did not create generated/rexglue.cmake." >&2
        exit 1
    fi
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
