# Offline shader translation tests

Small programs that call the SDK's SPIR-V translator directly, without a GPU
or the game, so a translator change can be checked before a build-and-play
round: translate a hand-built Xbox 360 shader, validate the SPIR-V, convert
it to Metal with SPIRV-Cross at MoltenVK's commit, and look at the result.
This is how the missing raster order group (one FSI shader of 118) was
reproduced and the fix verified (docs/macos_port.md, "Raster order groups
for every EDRAM shader").

- `fsitest.cpp` - a pixel shader that writes depth and has a conditional jump
  (`alloc colors; cjmp b0, L3; exec: max oDepth, c0, c0; L3: exece: max oC0,
  c1, c1`), plus the SDK's depth-only FSI shader, each translated with
  `vulkan_fsi_end_interlock_in_function` off and on. Writes
  `ps_jump_depth_{orig,split}.spv` and `depth_only_{orig,split}.spv`. Also a
  color-only variant (early depth / stencil, so it has the early quad skip)
  with `vulkan_fsi_quad_skip_quad_ops` off and on: `ps_color_{deriv,quad}.spv`.
- `vstest.cpp` - a minimal vertex shader (`oPos = max(r0, r1)`), translated
  as a plain vertex shader (`vs_snap.spv`) and as a quad patch drawn as a
  triangle list (`vs_quad_patch.spv`, `vulkan_quad_patches_in_vs`), for
  checking vertex shader prologue and epilogue changes.
- `stubs.cpp` - defines the `dump_shaders` cvar the translator objects
  reference (it lives in a file not linked here).

The ucode is built from the bit layouts in
`include/rex/graphics/format/ucode.h` (control flow instructions packed two
per three dwords, ALU instructions three dwords, exec addresses in units of
three dwords). Each program prints the SDK's disassembly of its shader first;
check it reads as intended before trusting a result.

## Building (Linux or macOS)

Build the SDK's Release objects once (on macOS `tools/build_sdk_mac.sh` does,
under `thirdparty/rexglue-sdk-src/out/build/<preset>`). Then compile with the
SDK's include paths and link against the translator objects, not the whole
plugin (which pulls in SDL, Vulkan and the window system):

```
SDK=thirdparty/rexglue-sdk-src
B=$SDK/out/build/<preset>          # linux-amd64 or mac-arm64
G=$B/src/graphics/CMakeFiles/rexgpu-xenos.dir/Release
C=$B/src/core/CMakeFiles/rexcore.dir/Release
L=$SDK/out/<preset>/Release
INC="-I$SDK/include -I$SDK/thirdparty/renderdoc -I$SDK -I$B/include -I$SDK/thirdparty/simde \
  -I$SDK/thirdparty/fmt/include -I$SDK/thirdparty/spdlog/include -I$SDK/thirdparty/glslang \
  -I$SDK/thirdparty/vulkan-headers/include -I$SDK/thirdparty/vulkan-memory-allocator/include \
  -I$SDK/thirdparty/xxHash -I$SDK/thirdparty/sdl3/include -I$B/thirdparty/sdl3/include-revision \
  -isystem $SDK/thirdparty/tomlplusplus/include -isystem $SDK/thirdparty/spirv-tools/include"
DEFS="-DREX_HAS_VULKAN=1 -DSPDLOG_COMPILED_LIB -DSPDLOG_FMT_EXTERNAL"   # plus -DREX_PLATFORM_LINUX=1 or _MAC=1
clang++ $DEFS $INC -std=c++23 -fno-char8_t -O1 -c tools/sdk_offline/fsitest.cpp -o fsitest.o
clang++ $DEFS $INC -std=c++23 -fno-char8_t -O1 -c tools/sdk_offline/stubs.cpp -o stubs.o
clang++ -o fsitest fsitest.o stubs.o $G/pipeline/shader/spirv_translator*.o \
  $G/pipeline/shader/spirv_builder.cpp.o $G/pipeline/shader/spirv.cpp.o \
  $G/pipeline/shader/shader.cpp.o $G/pipeline/shader/translator.cpp.o \
  $G/pipeline/shader/translator_disasm.cpp.o $G/format/ucode.cpp.o $G/util/draw.cpp.o \
  $G/registers.cpp.o $(find $C -name '*.o') $B/src/ui/CMakeFiles/rexui.dir/Release/graphics_util.cpp.o \
  $L/libSPIRV.a $L/libglslang.a $L/libMachineIndependent.a $L/libGenericCodeGen.a \
  $L/libOSDependent.a $L/libspdlog.a $L/libfmt.a $L/libxxhash.a -lpthread -ldl
mkdir -p out && ./fsitest out
```

On Linux the objects were built with `-mcmodel=large` (add it to the compile
lines there). The object list is what resolved every symbol on Linux; on
macOS add whatever the linker still reports missing from the same object
directories. `vstest.cpp` builds the same way.

## Checking the output

SPIRV-Cross at MoltenVK's pinned commit. The SDK v0.10.0 builds MoltenVK
`db445ff2` from source (`thirdparty/rexglue-sdk-src/thirdparty/moltenvk`),
whose `ExternalRevisions/SPIRV-Cross_repo_revision` names SPIRV-Cross
`adec7acbf41a988713cdb85f93f26c8ca5ea863e`:

```
git init -q spvc && cd spvc && git remote add origin https://github.com/KhronosGroup/SPIRV-Cross.git
git fetch -q --depth 1 origin adec7acbf41a988713cdb85f93f26c8ca5ea863e && git checkout -q FETCH_HEAD
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSPIRV_CROSS_ENABLE_TESTS=OFF \
  -DSPIRV_CROSS_SHARED=OFF -DSPIRV_CROSS_ENABLE_C_API=OFF && ninja -C build spirv-cross
build/spirv-cross out/ps_jump_depth_split.spv --msl --msl-version 30000 \
  --msl-argument-buffers --msl-argument-buffer-tier 1 --output out/ps.metal
grep -n "xe_edram" out/ps.metal | grep "\[\["     # raster_order_group(0) expected
```

spirv-val from the SDK's own SPIRV-Tools (`thirdparty/spirv-tools`, with
`-DSPIRV-Headers_SOURCE_DIR=$SDK/thirdparty/spirv-headers -DSPIRV_SKIP_TESTS=ON`):
`spirv-val --target-env vulkan1.2 --scalar-block-layout out/*.spv` (the
system constants use C++ `offsetof` offsets, which need scalar block layout;
MoltenVK supports it).

On a Mac, `xcrun -sdk macosx metal -c out/ps.metal` additionally compiles the
MSL, which the Linux setup could not.
