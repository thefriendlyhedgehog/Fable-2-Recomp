#!/usr/bin/env bash
# Save edits made in the SDK source tree back into the SDK patch.
#
# tools/build_sdk_mac.sh clones the ReXGlue SDK at v0.10.0 into
# thirdparty/rexglue-sdk-src and applies thirdparty/sdk_mac_vulkan_fixes.patch
# as working-tree changes. To change the SDK: edit files under
# thirdparty/rexglue-sdk-src/src or /include, run this script to regenerate
# the patch from the tree (git diff of src/ and include/ against the tag, new
# files included), then tools/build_sdk_mac.sh (it sees the patch already
# applied and builds incrementally), then ./build.sh -release fable_2.
#
# Run it BEFORE pulling a newer patch or running build_sdk_mac.sh after an
# edit: when the tree does not match the patch, build_sdk_mac.sh resets the
# tree to the tag and re-applies the patch (it keeps a backup diff of what it
# reset, see its output).
#
# Usage: tools/sdk_patch_refresh.sh
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$REPO/thirdparty/rexglue-sdk-src"
PATCH="$REPO/thirdparty/sdk_mac_vulkan_fixes.patch"

if [ ! -d "$SRC/.git" ]; then
    echo "Error: $SRC is not a git checkout; run tools/build_sdk_mac.sh first." >&2
    exit 1
fi
cd "$SRC"
# New files under src/ and include/ only show up in git diff once git knows
# about them (intent to add; nothing is staged).
git ls-files --others --exclude-standard -- src include | while IFS= read -r f; do
    git add -N -- "$f"
done
TMP="$(mktemp "${TMPDIR:-/tmp}/sdk_patch.XXXXXX")"
trap 'rm -f "$TMP"' EXIT
git diff -- src include > "$TMP"
if [ ! -s "$TMP" ]; then
    echo "Error: the SDK tree has no changes against $(git describe --tags 2>/dev/null || echo HEAD);" \
         "refusing to write an empty patch." >&2
    exit 1
fi
# The tree is the tag plus the patch, so the patch must reverse cleanly.
if ! git apply --reverse --check "$TMP"; then
    echo "Error: the regenerated patch does not reverse-apply to the tree; not writing it." >&2
    exit 1
fi
if cmp -s "$TMP" "$PATCH"; then
    echo "$(basename "$PATCH") is already up to date ($(grep -c '^diff --git' "$PATCH") files)."
else
    cp "$TMP" "$PATCH"
    echo "Updated $(basename "$PATCH") ($(grep -c '^diff --git' "$PATCH") files):"
    git -C "$REPO" diff --stat -- "thirdparty/$(basename "$PATCH")" | tail -1
fi
