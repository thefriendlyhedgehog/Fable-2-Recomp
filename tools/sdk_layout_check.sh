#!/usr/bin/env bash
# Check that the SDK classes the game allocates or derives from have the same
# size and alignment in the patched SDK source as in the prebuilt headers.
#
# The game compiles against thirdparty/rexglue-sdk-prebuilt (its own copy of
# rex_app.cpp included), but runs with the runtime and GPU plugin built from
# thirdparty/rexglue-sdk-src. A class the game allocates with the prebuilt
# size and the SDK constructs with a bigger one overflows the heap: on
# 2026-10-21 a member appended to the debug overlay dialog corrupted malloc
# and crashed MoltenVK and the audio thread. tools/build_sdk_mac.sh runs this
# before staging; it can also be run alone.
#
# Usage: tools/sdk_layout_check.sh    (exit 1 and a diff on a mismatch)
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLAT="mac-arm64"
case "$(uname -m)" in arm64) PLAT="mac-arm64" ;; x86_64) PLAT="mac-amd64" ;; esac
PREBUILT="$REPO/thirdparty/rexglue-sdk-prebuilt/$PLAT/include"
SRC="$REPO/thirdparty/rexglue-sdk-src/include"
PROBE="$REPO/tools/sdk_layout_probe.cpp"
OUT="$REPO/out/sdk_layout"
mkdir -p "$OUT"

# The game's flags for rex_app.cpp. Third-party headers (imgui, fmt, ...) come
# from the prebuilt tree in both builds; only rex/ differs.
FLAGS=(-std=gnu++23 -O0 -w -DREX_HAS_VULKAN=1 -DSPDLOG_COMPILED_LIB -DSPDLOG_FMT_EXTERNAL
       -DREXGLUE_BUILD_CONFIG=\"Release\")
build() {  # build <name> <include dirs...>
    local name="$1"; shift
    local inc=() d
    for d in "$@"; do inc+=(-isystem "$d"); done
    clang++ "${FLAGS[@]}" "${inc[@]}" "$PROBE" -o "$OUT/$name" 2> "$OUT/$name.log" || {
        grep -m 10 "error:" "$OUT/$name.log" >&2
        echo "Error: the layout probe did not compile against the $name headers (log: $OUT/$name.log)." >&2
        exit 1
    }
    "$OUT/$name" > "$OUT/$name.txt"
}
build prebuilt "$PREBUILT"
build source "$SRC" "$PREBUILT"

if diff "$OUT/prebuilt.txt" "$OUT/source.txt" > "$OUT/diff.txt"; then
    echo "SDK layout check: $(wc -l < "$OUT/prebuilt.txt" | tr -d ' ') classes match the prebuilt headers"
    exit 0
fi
echo "Error: SDK classes the game allocates changed size or alignment" >&2
echo "(< prebuilt headers, what the game allocates; > patched SDK source):" >&2
grep -E "^[<>]" "$OUT/diff.txt" >&2
echo "Keep their layout: move new members into the .cpp (CLAUDE.md, Conventions)." >&2
exit 1
