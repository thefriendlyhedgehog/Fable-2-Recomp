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

# ReXGlue SDK: thirdparty/rexglue-sdk/<platform>, fetched on first use.
REXSDK="$PWD/thirdparty/rexglue-sdk/$PLAT"
if [ ! -f "$REXSDK/lib/cmake/rexglue/rexglueConfig.cmake" ]; then
    echo "ReXGlue SDK not found; downloading via tools/setup_sdk.sh ..."
    tools/setup_sdk.sh
fi

# generated/ is not tracked, and CMakeLists.txt includes
# generated/rexglue.cmake, so a fresh checkout cannot configure until codegen
# has written it. Codegen writes that file first, before it needs default.xex.
if [ ! -f generated/rexglue.cmake ]; then
    echo "generated/rexglue.cmake missing; running rexglue codegen to create it ..."
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

cmake --preset "$CONFIG" \
    -DCMAKE_PREFIX_PATH="$REXSDK" \
    -DREXGLUE_SDK_ROOT="$REXSDK" \
    ${EXTRA[@]+"${EXTRA[@]}"}
cmake --build "out/build/$CONFIG" --target "$TARGET"
