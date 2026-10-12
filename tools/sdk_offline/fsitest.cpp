// Translates a small hand-built pixel shader (writes depth, has a conditional
// jump) and the depth-only shader for the FSI path, with and without
// vulkan_fsi_end_interlock_in_function, and writes the SPIR-V. Also a
// color-only variant (early depth / stencil, so it has the early quad skip),
// with vulkan_fsi_quad_skip_quad_ops off and on.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/graphics/format/ucode.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/pipeline/shader/spirv_translator.h>
#include <rex/string/buffer.h>

REXCVAR_DECLARE(bool, vulkan_fsi_end_interlock_in_function);
REXCVAR_DECLARE(bool, vulkan_fsi_quad_skip_quad_ops);

using namespace rex::graphics;


int main(int argc, char** argv) {
  const char* out_dir = argc > 1 ? argv[1] : ".";
  // Four control flow instructions (2 per 3 dwords), then two ALU
  // instructions at slots 2 and 3:
  //   0: alloc colors
  //   1: cjmp b0 == true -> 3
  //   2: exec 2, 1: oDepth = c0
  //   3: exece 3, 1: oC0 = c1
  uint64_t cf[4];
  auto pack = [](uint32_t dword_0, uint32_t dword_1_low16) {
    return uint64_t(dword_0) | (uint64_t(dword_1_low16 & 0xFFFF) << 32);
  };
  // alloc: size 0, type kPsColors (2) at word 1 bits 9:10, opcode at 12:15.
  cf[0] = pack(0, (2u << 9) | (12u << 12));
  // cjmp: address 3 (bits 0:12), not unconditional, not predicated; word 1:
  // bool_address 0 (bits 2:9), condition 1 (bit 10), opcode 11.
  cf[1] = pack(3, (1u << 10) | (11u << 12));
  // exec: address 2 (0:11), count 1 (12:14), sequence 0; opcode 1.
  cf[2] = pack(2 | (1u << 12), (1u << 12));
  // exece: address 3, count 1; opcode 2.
  cf[3] = pack(3 | (1u << 12), (2u << 12));
  std::vector<uint32_t> ucode(6 + 2 * 3, 0);
  for (int pair = 0; pair < 2; ++pair) {
    uint64_t a = cf[pair * 2], b = cf[pair * 2 + 1];
    // Inverse of UnpackControlFlowInstructions.
    ucode[pair * 3 + 0] = uint32_t(a);
    ucode[pair * 3 + 1] = uint32_t(a >> 32) | (uint32_t(b) << 16);
    ucode[pair * 3 + 2] = (uint32_t(b) >> 16) | (uint32_t(b >> 32) << 16);
  }
  auto alu = [](uint32_t* d, uint32_t export_reg, uint32_t constant) {
    // Word 0: vector_dest (0:5), export_data (bit 15), vector_write_mask
    // (16:19), scalar_opc (26:31) = kRetainPrev (50).
    d[0] = export_reg | (1u << 15) | (0xFu << 16) | (50u << 26);
    // Word 1: swizzles 0 (identity), no negation, not predicated.
    d[1] = 0;
    // Word 2: src2_reg (8:15), src1_reg (16:23), vector_opc (24:28) = kMax
    // (2), srcN_sel 0 (constants).
    d[2] = (constant << 8) | (constant << 16) | (2u << 24);
  };
  alu(&ucode[6], 61, 0);  // oDepth = max(c0, c0)
  alu(&ucode[9], 0, 1);   // oC0 = max(c1, c1)

  for (int split = 0; split < 2; ++split) {
    REXCVAR_SET(vulkan_fsi_end_interlock_in_function, split != 0);
    SpirvShaderTranslator::Features features(true);
    SpirvShaderTranslator translator(features, true, true, true, 1, 1);

    Shader shader(xenos::ShaderType::kPixel, 0x1234, ucode.data(), ucode.size(),
                  std::endian::native);
    rex::string::StringBuffer disasm;
    shader.AnalyzeUcode(disasm);
    if (!split) {
      std::printf("%s\n", shader.ucode_disassembly().c_str());
    }
    uint64_t modification = translator.GetDefaultPixelShaderModification(64);
    Shader::Translation& translation = *shader.GetOrCreateTranslation(modification);
    if (!translator.TranslateAnalyzedShader(translation) || !translation.is_valid()) {
      std::printf("translation failed\n");
      return 1;
    }
    std::string path = std::string(out_dir) + "/ps_jump_depth_" + (split ? "split" : "orig") + ".spv";
    FILE* f = std::fopen(path.c_str(), "wb");
    std::fwrite(translation.translated_binary().data(), 1, translation.translated_binary().size(), f);
    std::fclose(f);
    std::printf("wrote %s (%zu bytes)\n", path.c_str(), translation.translated_binary().size());

    std::vector<uint8_t> depth_only = translator.CreateDepthOnlyFragmentShader();
    path = std::string(out_dir) + "/depth_only_" + (split ? "split" : "orig") + ".spv";
    f = std::fopen(path.c_str(), "wb");
    std::fwrite(depth_only.data(), 1, depth_only.size(), f);
    std::fclose(f);
    std::printf("wrote %s (%zu bytes)\n", path.c_str(), depth_only.size());
  }

  // Color only: slot 2 writes oC0 too, so the shader does not write depth and
  // the depth / stencil test runs early.
  alu(&ucode[6], 0, 1);
  REXCVAR_SET(vulkan_fsi_end_interlock_in_function, true);
  for (int quad_ops = 0; quad_ops < 2; ++quad_ops) {
    REXCVAR_SET(vulkan_fsi_quad_skip_quad_ops, quad_ops != 0);
    SpirvShaderTranslator::Features features(true);
    SpirvShaderTranslator translator(features, true, true, true, 1, 1);
    Shader shader(xenos::ShaderType::kPixel, 0x5678, ucode.data(), ucode.size(),
                  std::endian::native);
    rex::string::StringBuffer disasm;
    shader.AnalyzeUcode(disasm);
    uint64_t modification = translator.GetDefaultPixelShaderModification(64);
    Shader::Translation& translation = *shader.GetOrCreateTranslation(modification);
    if (!translator.TranslateAnalyzedShader(translation) || !translation.is_valid()) {
      std::printf("translation failed\n");
      return 1;
    }
    std::string path =
        std::string(out_dir) + "/ps_color_" + (quad_ops ? "quad" : "deriv") + ".spv";
    FILE* f = std::fopen(path.c_str(), "wb");
    std::fwrite(translation.translated_binary().data(), 1, translation.translated_binary().size(), f);
    std::fclose(f);
    std::printf("wrote %s (%zu bytes)\n", path.c_str(), translation.translated_binary().size());
  }
  return 0;
}
