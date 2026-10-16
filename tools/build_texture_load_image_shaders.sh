#!/usr/bin/env bash
# Compiles the GLSL compute shaders added for the macOS port in the SDK source
# tree into the SPIR-V headers the Vulkan backend includes, one per variant:
# texture_load_to_image.cs.glsl (vulkan_texture_load_to_image) and
# resolve_direct_fast_32bpp.cs.glsl (direct_host_resolve). Needs
# glslangValidator (Vulkan SDK). Run after editing a shader, then
# tools/sdk_patch_refresh.sh.
set -euo pipefail
cd "$(dirname "$0")/.."
SDK=thirdparty/rexglue-sdk-src/src/graphics/shaders
SRC=$SDK/texture_load_to_image.cs.glsl
OUT=$SDK/vulkan_spirv
build() {
  local name=$1 bpb_log2=$2 convert=$3
  glslangValidator -V --target-env vulkan1.0 -S comp -DBPB_LOG2="$bpb_log2" \
    -DCONVERT="$convert" --vn "texture_load_${name}_image_cs" \
    -o "$OUT/texture_load_${name}_image_cs.h" "$SRC" > /dev/null
  echo "$OUT/texture_load_${name}_image_cs.h"
}
build 8bpb 0 0
build 16bpb 1 0
build 32bpb 2 0
build 64bpb 3 0
build depth_float 2 1
build r5g5b5a1_b5g5r5a1 1 2
resolve() {
  local name=$1 source_uint=$2
  glslangValidator -V --target-env vulkan1.0 -S comp -DSOURCE_UINT="$source_uint" \
    --vn "resolve_direct_fast_32bpp_${name}_cs" \
    -o "$OUT/resolve_direct_fast_32bpp_${name}_cs.h" "$SDK/resolve_direct_fast_32bpp.cs.glsl" > /dev/null
  echo "$OUT/resolve_direct_fast_32bpp_${name}_cs.h"
}
resolve float 0
resolve uint 1
