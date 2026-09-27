#!/usr/bin/env bash
# Build the ReXGlue SDK's GPU plugin and runtime from source on macOS, with
# this project's Vulkan fixes (thirdparty/sdk_mac_vulkan_fixes.patch), and
# stage them next to the game in place of the prebuilt ones.
#
# The prebuilt SDK cannot be patched, and the fixes live in the GPU plugin
# (librexgpu-xenos.dylib). The source is the exact v0.10.0 tag the prebuilt
# SDK and the game were built from, so the headers the game compiled against
# match. The macOS counterpart of tools/build_sdk_vulkan.cmd.
#
# Usage:
#   tools/build_sdk_mac.sh            build + stage into out/build/mac-arm64-release
#   tools/build_sdk_mac.sh -debug     build the Debug SDK (asserts on) + stage
#                                     into out/build/mac-arm64-debug
#   tools/build_sdk_mac.sh -restore   put the prebuilt plugin/runtime back
#   tools/build_sdk_mac.sh -codegen   also build the recompiler (rexglue) with
#                                     the patch's codegen fixes and regenerate
#                                     generated/ with it (only files whose code
#                                     changes are rewritten, so the next game
#                                     build recompiles just those); from then
#                                     on build.sh keeps using that recompiler
#   tools/build_sdk_mac.sh -mvk-private
#                                     also build MoltenVK with Metal's private
#                                     API (MVK_USE_METAL_PRIVATE_API) and stage
#                                     it in vulkan/lib: lets MoltenVK really
#                                     disable primitive restart (Metal otherwise
#                                     restarts strips at index 0xFFFF even when
#                                     the game asked it not to). Experimental;
#                                     -restore puts the prebuilt one back
#   tools/build_sdk_mac.sh -stage     copy the already-built plugin/runtime
#                                     next to the game again (no build);
#                                     build.sh runs this after every game
#                                     build, because the SDK's CMake helpers
#                                     copy the prebuilt ones over them on
#                                     every link
#
# Needs Xcode (not only the command line tools: the SDK builds MoltenVK from
# source with xcodebuild), CMake >= 3.25, Ninja, git, ~4 GB of disk.
# The clone is shallow; the first build is long (the whole Vulkan stack).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$REPO/thirdparty/rexglue-sdk-src"
PATCH="$REPO/thirdparty/sdk_mac_vulkan_fixes.patch"
TAG="v0.10.0"
CONFIG="Release"
PLAT="mac-arm64"
case "$(uname -m)" in arm64) PLAT="mac-arm64" ;; x86_64) PLAT="mac-amd64" ;; esac

MODE="build"
CODEGEN=0
MVK_PRIVATE=0
for arg in "$@"; do
    case "$arg" in
        -debug) CONFIG="Debug" ;;
        -restore) MODE="restore" ;;
        -stage) MODE="stage" ;;
        -codegen) CODEGEN=1 ;;
        -mvk-private) MVK_PRIVATE=1 ;;
        *) echo "unknown argument: $arg" >&2; exit 1 ;;
    esac
done
cfg_lower="$(echo "$CONFIG" | tr '[:upper:]' '[:lower:]')"
GAME_DIR="$REPO/out/build/$PLAT-$cfg_lower"

stage() {  # stage <file> : copy into the game dir, keeping a one-time backup
    local f="$1" name; name="$(basename "$f")"
    if [ -f "$GAME_DIR/$name" ] && [ ! -f "$GAME_DIR/$name.prebuilt" ]; then
        cp -p "$GAME_DIR/$name" "$GAME_DIR/$name.prebuilt"
    fi
    cp -p "$f" "$GAME_DIR/$name"
    echo "staged $name -> $GAME_DIR"
}
MVK_DIR_REL="vulkan/lib"  # where the SDK's CMake helpers stage libMoltenVK.dylib

if [ "$MODE" = "restore" ]; then
    for name in librexgpu-xenos.dylib librexgpu-xenosd.dylib librexruntime.dylib librexruntimed.dylib; do
        if [ -f "$GAME_DIR/$name.prebuilt" ]; then
            mv -f "$GAME_DIR/$name.prebuilt" "$GAME_DIR/$name"
            echo "restored prebuilt $name"
        fi
    done
    if [ -f "$GAME_DIR/$MVK_DIR_REL/libMoltenVK.dylib.prebuilt" ]; then
        mv -f "$GAME_DIR/$MVK_DIR_REL/libMoltenVK.dylib.prebuilt" "$GAME_DIR/$MVK_DIR_REL/libMoltenVK.dylib"
        echo "restored prebuilt $MVK_DIR_REL/libMoltenVK.dylib"
    fi
    exit 0
fi

stage_mvk() {  # stage the source-built MoltenVK (private API build) if wanted
    local dst="$GAME_DIR/$MVK_DIR_REL/libMoltenVK.dylib" f
    # Only when asked this run, or when a previous -mvk-private staged it
    # (its backup is the marker).
    [ "$MVK_PRIVATE" = 1 ] || [ -f "$dst.prebuilt" ] || return 0
    f="$(find "$SRC/out" -path "*/$CONFIG/*" -name libMoltenVK.dylib -type f 2>/dev/null | head -1)"
    if [ -z "$f" ]; then
        echo "Warning: no source-built libMoltenVK.dylib under $SRC/out; keeping the prebuilt one." >&2
        return 0
    fi
    mkdir -p "$(dirname "$dst")"
    if [ -f "$dst" ] && [ ! -f "$dst.prebuilt" ]; then
        cp -p "$dst" "$dst.prebuilt"
    fi
    cp -p "$f" "$dst"
    echo "staged libMoltenVK.dylib (private API build) -> $GAME_DIR/$MVK_DIR_REL"
}

stage_built() {  # stage the plugin/runtime from an existing source build
    local found=0 name f
    for name in librexgpu-xenos.dylib librexruntime.dylib librexgpu-xenosd.dylib librexruntimed.dylib; do
        f="$(find "$SRC/out" -path "*/$CONFIG/*" -name "$name" -type f 2>/dev/null | head -1)"
        if [ -n "$f" ]; then stage "$f"; found=1; fi
    done
    stage_mvk
    [ "$found" = 1 ]
}

if [ "$MODE" = "stage" ]; then
    [ -d "$GAME_DIR" ] || { echo "Error: $GAME_DIR does not exist." >&2; exit 1; }
    if ! stage_built; then
        echo "Error: no source-built $CONFIG plugin under $SRC/out; run tools/build_sdk_mac.sh first." >&2
        exit 1
    fi
    exit 0
fi

for tool in cmake ninja git xcodebuild; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Error: $tool not found on PATH (xcodebuild needs a full Xcode install," \
             "selected with xcode-select)." >&2
        exit 1
    fi
done
[ -d "$GAME_DIR" ] || { echo "Error: $GAME_DIR does not exist; build the game first (./build.sh -release fable_2)." >&2; exit 1; }

# 1. Source at the pinned tag, shallow, with shallow submodules.
if [ ! -d "$SRC/.git" ]; then
    echo "Cloning rexglue/rexglue-sdk $TAG (shallow, with submodules) ..."
    git clone --depth 1 --branch "$TAG" --recurse-submodules --shallow-submodules \
        https://github.com/rexglue/rexglue-sdk.git "$SRC"
fi

# 2. Fixes. The tree is reset to the tag first so an updated patch applies
#    on a re-run (the build only recompiles what the patch touches).
cd "$SRC"
git checkout -q -- .
if git apply --check "$PATCH" 2>/dev/null; then
    git apply "$PATCH"
    echo "Applied $(basename "$PATCH")"
else
    echo "Error: $PATCH does not apply to $SRC (SDK tag $TAG expected)." >&2
    exit 1
fi

# 3. Configure + build only what the game loads. The presets use Ninja
#    Multi-Config; outputs land in out/<platform>/<Config>/.
#    The full output goes to a log; on failure only the error lines are
#    shown (the SDK emits hundreds of warnings per file).
LOG="$SRC/build_sdk_mac.log"
# Sticky: once staged (its .prebuilt backup exists), keep building MoltenVK
# with the private API until -restore.
MVK_OPT=OFF
if [ "$MVK_PRIVATE" = 1 ] || [ -f "$GAME_DIR/$MVK_DIR_REL/libMoltenVK.dylib.prebuilt" ]; then
    MVK_PRIVATE=1
    MVK_OPT=ON
fi
cmake --preset "$PLAT" -DREXGLUE_BUILD_TESTS=OFF -DMVK_USE_METAL_PRIVATE_API=$MVK_OPT > "$LOG" 2>&1 || {
    tail -40 "$LOG"; echo "Error: cmake configure failed; full output in $LOG" >&2; exit 1; }
TARGETS=(rexgpu-xenos rexruntime)
[ "$CODEGEN" = 1 ] && TARGETS+=(rexglue)
[ "$MVK_PRIVATE" = 1 ] && TARGETS+=(MoltenVK)
echo "Building ${TARGETS[*]} ($CONFIG); log: $LOG"
if ! cmake --build --preset "$PLAT-$cfg_lower" --target "${TARGETS[@]}" >> "$LOG" 2>&1; then
    echo "Build FAILED. Errors:" >&2
    grep -n -A3 -E "error:|ld: error|Undefined symbols" "$LOG" | grep -v -E "warning:|note:" | head -60 >&2
    echo "(full output: $LOG)" >&2
    exit 1
fi
grep -E "^\[[0-9]+/[0-9]+\]" "$LOG" | tail -3

# 4. Stage. The plugin and runtime must come from the same build.
if ! stage_built; then
    echo "Error: no librexgpu-xenos*.dylib found under $SRC/out; see the build output above." >&2
    exit 1
fi
# 5. Optional: regenerate the recompiled code with the patched recompiler.
#    The codegen fingerprint covers the inputs and the SDK version, not the
#    tool, so drop the stamp to force one run; unchanged files are not
#    rewritten.
if [ "$CODEGEN" = 1 ]; then
    TOOL="$(find "$SRC/out" -path "*/$CONFIG/*" -name rexglue -type f -perm -u+x 2>/dev/null | head -1)"
    [ -n "$TOOL" ] || { echo "Error: no rexglue binary under $SRC/out." >&2; exit 1; }
    [ -f "$REPO/default.xex" ] || { echo "Error: default.xex not found in $REPO (codegen needs it)." >&2; exit 1; }
    echo "Regenerating generated/ with $TOOL ..."
    rm -f "$REPO/generated/codegen.stamp"
    (cd "$REPO" && "$TOOL" codegen fable_2_manifest.toml) > "$SRC/codegen.log" 2>&1 || {
        tail -30 "$SRC/codegen.log"; echo "Error: codegen failed; full output in $SRC/codegen.log" >&2; exit 1; }
    grep -i -E "wrote|written|changed|unchanged" "$SRC/codegen.log" | tail -5 || true
    echo "Codegen done (log: $SRC/codegen.log). Now rebuild the game: ./build.sh -release fable_2"
fi

echo "Done. Launch the game normally; to go back to the prebuilt SDK: tools/build_sdk_mac.sh -restore"
