#!/usr/bin/env bash
# ===========================================================================
# Fetch the prebuilt ReXGlue SDK for this host (macOS / Linux) into
# thirdparty/rexglue-sdk/<platform>/ (relative to the repo root). No-op if
# already present. The macOS/Linux counterpart of tools/setup_sdk.cmd.
#
#   macOS Apple Silicon -> mac-arm64      macOS Intel -> mac-amd64
#   Linux x86_64        -> linux-amd64
#
# The SDK version defaults to 0.10.0 (the version this project is built and
# tested against). Override with REXGLUE_SDK_VERSION=<x.y.z>; anything older
# than 0.10.0 is refused, since that is the release that added macOS support
# (MoltenVK-backed Vulkan).
#
# Run automatically by build.sh when the SDK is missing; can also be run
# manually. Prints the SDK root on the last line of stdout.
# ===========================================================================
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VER="${REXGLUE_SDK_VERSION:-0.10.0}"
MIN_VER="0.10.0"

version_ge() {  # version_ge A B -> true if A >= B
    [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n1)" = "$2" ]
}
if ! version_ge "$VER" "$MIN_VER"; then
    echo "Error: ReXGlue SDK $VER is too old; macOS support needs >= $MIN_VER." >&2
    exit 1
fi

case "$(uname -s)-$(uname -m)" in
    Darwin-arm64)  PLAT="mac-arm64" ;;
    Darwin-x86_64) PLAT="mac-amd64" ;;
    Linux-x86_64)  PLAT="linux-amd64" ;;
    *)
        echo "Error: no prebuilt ReXGlue SDK for $(uname -s) $(uname -m)." >&2
        exit 1
        ;;
esac

DEST="$REPO/thirdparty/rexglue-sdk"
SDK_ROOT="$DEST/$PLAT"
MARKER="$SDK_ROOT/lib/cmake/rexglue/rexglueConfig.cmake"
ZIPNAME="rexglue-sdk-$VER-$PLAT.zip"
URL="https://github.com/rexglue/rexglue-sdk/releases/download/v$VER/$ZIPNAME"

if [ -f "$MARKER" ]; then
    have="$(sed -n 's/^set(PACKAGE_VERSION "\(.*\)")$/\1/p' \
        "$SDK_ROOT/lib/cmake/rexglue/rexglueConfigVersion.cmake" 2>/dev/null || true)"
    if [ "$have" = "$VER" ]; then
        echo "ReXGlue SDK v$VER already present: $SDK_ROOT"
        echo "$SDK_ROOT"
        exit 0
    fi
    echo "Replacing ReXGlue SDK v${have:-unknown} with v$VER"
fi

for tool in curl unzip; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Error: $tool not found on PATH." >&2
        exit 1
    fi
done

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "Downloading $URL"
curl -L --fail -sS -o "$TMP/$ZIPNAME" "$URL"

echo "Extracting to $SDK_ROOT"
unzip -q "$TMP/$ZIPNAME" -d "$TMP/x"
if [ ! -f "$TMP/x/$PLAT/lib/cmake/rexglue/rexglueConfig.cmake" ]; then
    echo "Error: SDK archive has no $PLAT/ root; unexpected layout." >&2
    exit 1
fi
mkdir -p "$DEST"
rm -rf "$SDK_ROOT"
mv "$TMP/x/$PLAT" "$SDK_ROOT"
chmod +x "$SDK_ROOT/bin/rexglue"

echo "OK: ReXGlue SDK v$VER -> $SDK_ROOT"
echo "$SDK_ROOT"
