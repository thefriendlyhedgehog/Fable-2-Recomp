// Translates a minimal vertex shader (oPos = c0) to check the vertex position
// snapping code validates and converts to MSL.
#include <cstdio>
#include <string>
#include <vector>

#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/pipeline/shader/spirv_translator.h>
#include <rex/string/buffer.h>

using namespace rex::graphics;

int main(int argc, char** argv) {
  const char* out_dir = argc > 1 ? argv[1] : ".";
  auto pack = [](uint32_t dword_0, uint32_t dword_1_low16) {
    return uint64_t(dword_0) | (uint64_t(dword_1_low16 & 0xFFFF) << 32);
  };
  uint64_t cf[2];
  cf[0] = pack(0, (1u << 9) | (12u << 12));  // alloc position
  cf[1] = pack(1 | (1u << 12), (2u << 12));  // exece 1, 1
  std::vector<uint32_t> ucode(3 + 3, 0);
  uint64_t a = cf[0], b = cf[1];
  ucode[0] = uint32_t(a);
  ucode[1] = uint32_t(a >> 32) | (uint32_t(b) << 16);
  ucode[2] = (uint32_t(b) >> 16) | (uint32_t(b >> 32) << 16);
  ucode[3] = 62 | (1u << 15) | (0xFu << 16) | (50u << 26);  // oPos, export, xyzw
  ucode[4] = 0;
  ucode[5] = (0u << 8) | (0u << 16) | (2u << 24);  // max c0, c0
  SpirvShaderTranslator::Features features(true);
  SpirvShaderTranslator translator(features, true, true, true, 1, 1);
  Shader shader(xenos::ShaderType::kVertex, 0x5678, ucode.data(), ucode.size(),
                std::endian::native);
  rex::string::StringBuffer disasm;
  shader.AnalyzeUcode(disasm);
  std::printf("%s\n", shader.ucode_disassembly().c_str());
  Shader::Translation& translation =
      *shader.GetOrCreateTranslation(translator.GetDefaultVertexShaderModification(64));
  if (!translator.TranslateAnalyzedShader(translation) || !translation.is_valid()) {
    std::printf("translation failed\n");
    return 1;
  }
  std::string path = std::string(out_dir) + "/vs_snap.spv";
  FILE* f = std::fopen(path.c_str(), "wb");
  std::fwrite(translation.translated_binary().data(), 1, translation.translated_binary().size(), f);
  std::fclose(f);
  std::printf("wrote %s\n", path.c_str());
  return 0;
}
