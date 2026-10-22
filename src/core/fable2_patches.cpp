// fable2_patches - see fable2_patches.h for the design notes.
//
// SCOPE: this table is for GUEST-IMAGE (data) patches only. Code-region
// Xenia patches must be implemented as mid-asm hooks instead
// ([[entrypoint.midasm_hook]] in fable_2_manifest.toml + src/core/fable2_hooks.cpp).
//
// There is no separate patch file: each patch is switched on by a
// [patches] key in fable2_config.toml (src/core/fable2_config.h), and the
// table below is built from those switches.

#include "fable2_patches.h"

#include <atomic>
#include <bit>
#include <format>

#include <rex/logging.h>
#include <rex/system/xmemory.h>

#include "fable2_config.h"

REXCVAR_DEFINE_BOOL(high_tick_rate, false, "Game",
                    "Run the game's update ticks at twice the original rate: 30 Hz, and "
                    "60 Hz for the fast tick with [patches] higher_hf_tick_rate. Smoother "
                    "movement, camera and UI. Takes effect immediately: turn it off for a "
                    "scripted scene that does not start with it on (the glowing circle in "
                    "Lord Lucien's castle in the prologue) and back on afterwards. Default "
                    "comes from fable2_config.toml [patches] high_tick_rate.");

namespace fable2::patches {

namespace {

// The tick-rate doubles the frame loop reads, and where sub_8233AE50 (game
// init) copies the game's own values from with the two stores the tick-rate
// hooks skip (0x8233AEB4 and 0x8233AE98).
constexpr uint32_t kLfTick = 0x83319510;
constexpr uint32_t kHfTick = 0x83319518;
constexpr uint32_t kLfTickOriginal = 0x820A0228;  // 15.0
constexpr uint32_t kHfTickOriginal = 0x833194F0;  // 30.0

double ReadGuestDouble(rex::memory::Memory* memory, uint32_t address) {
  const uint8_t* host = memory->TranslateVirtual<const uint8_t*>(address);
  uint64_t bits = 0;
  for (int i = 0; i < 8; ++i) {
    bits = (bits << 8) | host[i];  // big-endian in guest memory
  }
  return std::bit_cast<double>(bits);
}

// One 64-bit store, so the frame loop never reads half of an old value.
void WriteGuestDouble(rex::memory::Memory* memory, uint32_t address, double value) {
  auto* host = memory->TranslateVirtual<uint64_t*>(address);
  std::atomic_ref<uint64_t>(*host).store(std::byteswap(std::bit_cast<uint64_t>(value)),
                                         std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// The data halves of the Xenia patches from
// "4D5307F1 - Fable II (GOTY/Platinum Edition).patch.toml"
// (https://github.com/xenia-canary/game-patches). The code halves (the NOPs
// of the stores that would overwrite these values) are mid-asm hooks in
// src/core/fable2_hooks.cpp, gated on the same config keys.
// ---------------------------------------------------------------------------
std::vector<Patch> BuildPatches() {
  return {
      {
          "High Tick Rate",
          "Doubles the LF tick to 30 Hz. Vastly improves in-game UI framerate. "
          "Improves input delay.",
          "Guy",
          // The cvar, seeded from [patches] high_tick_rate (it can change
          // later; see ApplyTickRates).
          HighTickRateActive(),
          {
              // LF tick double at 0x83319510: 15.0 -> 30.0.
              {Op::Width::kBe8, 0x83319511, 0x3E},
          },
      },
      {
          "Higher HF Tick Rate",
          "Doubles the HF tick to 60 Hz, keeping the 2:1 HF:LF ratio with "
          "High Tick Rate.",
          "Ultra",
          // Requires High Tick Rate (same gate as the hook in fable2_hooks.cpp).
          HigherHfTickRateActive(),
          {
              // HF tick double at 0x83319518: 30.0 -> 60.0.
              {Op::Width::kBe8, 0x83319519, 0x4E},
          },
      },
  };
}

size_t WidthBytes(Op::Width w) {
  switch (w) {
    case Op::Width::kBe8:
      return 1;
    case Op::Width::kBe16:
      return 2;
    case Op::Width::kBe32:
      return 4;
    case Op::Width::kBe64:
      return 8;
  }
  return 0;
}

const char* WidthName(Op::Width w) {
  switch (w) {
    case Op::Width::kBe8:
      return "be8";
    case Op::Width::kBe16:
      return "be16";
    case Op::Width::kBe32:
      return "be32";
    case Op::Width::kBe64:
      return "be64";
  }
  return "?";
}

// Guest memory is big-endian on the host; format a value as big-endian hex.
std::string ToBeHex(uint64_t value, size_t bytes) {
  std::string s = "0x";
  for (size_t i = bytes; i-- > 0;) {
    s += std::format("{:02x}", uint8_t(value >> (8 * i)));
  }
  return s;
}

// The SDK marks XEX code/rodata pages read-only after loading (see
// XexModule::LoadContinue's page-descriptor protection pass), so writing a
// patch to a read-only page faults. This guard flips the guest pages an op
// touches to read/write and restores their original protection afterwards.
// Safe at OnPostLoadXexImage time: the guest module has not launched yet.
class PageWriteGuard {
 public:
  PageWriteGuard(rex::memory::BaseHeap* heap, uint32_t address, size_t bytes)
      : heap_(heap), page_size_(heap->page_size()) {
    uint32_t page = address & ~(page_size_ - 1);
    const uint32_t end = address + bytes;  // exclusive
    while (page < end) {
      const uint32_t next = page + page_size_;
      const uint32_t hi = next < end ? next : end;
      access_ = heap_->QueryRangeAccess(page, hi - 1);
      if (access_ == rex::memory::PageAccess::kReadOnly) {
        // Protect covers every page the range touches (partial ranges are
        // rounded up to whole pages, like the XEX section pass relies on).
        const uint32_t range = hi - page;
        if (heap_->Protect(page, range, rex::memory::kMemoryProtectRead |
                                            rex::memory::kMemoryProtectWrite)) {
          flipped_.push_back({page, range});
        } else {
          REXSYS_ERROR("[patches]   failed to make guest page 0x{:08X} writable; "
                       "op will be skipped", page);
          return;
        }
      } else if (access_ != rex::memory::PageAccess::kReadWrite) {
        REXSYS_ERROR("[patches]   guest page 0x{:08X} is not readable/writable "
                     "(no-access); op will be skipped", page);
        return;
      }
      page = next;
    }
    ok_ = true;
  }

  ~PageWriteGuard() {
    // Restore in reverse so a multi-page op unwinds cleanly.
    for (auto it = flipped_.rbegin(); it != flipped_.rend(); ++it) {
      heap_->Protect(it->page, it->size, rex::memory::kMemoryProtectRead);
    }
  }

  bool ok() const { return ok_; }

 private:
  struct Flip {
    uint32_t page;
    uint32_t size;
  };
  rex::memory::BaseHeap* heap_;
  uint32_t page_size_;
  rex::memory::PageAccess access_;
  std::vector<Flip> flipped_;
  bool ok_ = false;
};

}  // namespace

const std::vector<Patch>& Patches() {
  // Built on first use; the config is loaded (OnPostInitLogging) before the
  // patches are applied (OnPostLoadXexImage).
  static const std::vector<Patch> patches = BuildPatches();
  return patches;
}

size_t ApplyAll(rex::memory::Memory* memory, const rex::PPCImageInfo& image) {
  if (!memory) {
    REXSYS_ERROR("[patches] no guest memory available; not applying patches");
    return 0;
  }

  size_t applied = 0;
  size_t skipped = 0;
  const uint64_t img_lo = image.image_base;
  const uint64_t img_hi = image.image_base + image.image_size;
  const uint64_t code_lo = image.code_base;
  const uint64_t code_hi = image.code_base + image.code_size;

  for (const Patch& p : Patches()) {
    if (!p.is_enabled) {
      REXSYS_INFO("[patches] '{}' disabled, skipping", p.name);
      continue;
    }
    REXSYS_INFO("[patches] applying '{}' by {} ({} op{}){}", p.name, p.author,
                p.ops.size(), p.ops.size() == 1 ? "" : "s",
                p.desc.empty() ? std::string{} : std::format(" - {}", p.desc));

    for (const Op& op : p.ops) {
      const size_t n = WidthBytes(op.width);
      if (n == 0) {
        REXSYS_ERROR("[patches]   {}: unknown op width; skipped", p.name);
        ++skipped;
        continue;
      }
      const uint64_t addr = op.address;
      if (addr < img_lo || addr + n > img_hi) {
        REXSYS_ERROR("[patches]   {} {} 0x{:08X} is outside the image "
                     "[0x{:08X}, 0x{:08X}); skipped",
                     p.name, WidthName(op.width), addr, img_lo, img_hi);
        ++skipped;
        continue;
      }

      auto* heap = memory->LookupHeap(op.address);
      if (!heap) {
        REXSYS_ERROR("[patches]   {} {} 0x{:08X} has no guest heap; skipped",
                     p.name, WidthName(op.width), addr);
        ++skipped;
        continue;
      }
      PageWriteGuard guard(heap, op.address, n);
      if (!guard.ok()) {
        ++skipped;
        continue;
      }

      uint8_t* host = memory->TranslateVirtual<uint8_t*>(op.address);
      uint64_t old_value = 0;
      for (size_t i = 0; i < n; ++i) {
        old_value = (old_value << 8) | host[i];  // big-endian in guest memory
      }

      for (size_t i = 0; i < n; ++i) {
        host[i] = uint8_t(op.value >> (8 * (n - 1 - i)));
      }

      const bool in_code =
          code_lo != 0 && addr >= code_lo && addr < code_hi;
      REXSYS_INFO("[patches]   {} {} 0x{:08X}: {} -> {} ({})", p.name,
                  WidthName(op.width), addr, ToBeHex(old_value, n),
                  ToBeHex(op.value, n),
                  in_code
                      ? "code region: guest .text is not executed in this "
                        "recomp (native code runs instead) - no runtime "
                        "effect"
                      : "data: takes effect at runtime");
      ++applied;
    }
  }

  REXSYS_INFO("[patches] done: {} op(s) applied, {} skipped", applied,
              skipped);

  // Xenia's Fable II code patches (Disable MSAA, 1280x720, 60 FPS, Disable
  // Texture Morphing) rewrite instruction immediates, which a recomp never
  // executes; each has to become a mid-asm hook, which needs the exact
  // instruction and register at the site. Log the instruction words around
  // each site once (read-only) so the hooks can be written from a log.
  struct CodeSite {
    const char* name;
    uint32_t first;
    uint32_t words;
  };
  static const CodeSite kCodeSites[] = {
      {"Disable MSAA (be8 0x8238DF3F) / 1280x720 (be16 0x8238DF5A)", 0x8238DF20, 24},
      {"60 FPS (be8 0x82B9C8EB)", 0x82B9C8D8, 8},
      {"Disable Texture Morphing (be16 0x8220EF10)", 0x8220EF00, 8},
      {"High Tick Rate (be32 0x8233AEB4)", 0x8233AEA4, 8},
  };
  for (const CodeSite& site : kCodeSites) {
    const uint64_t end = uint64_t(site.first) + uint64_t(site.words) * 4;
    if (site.first < img_lo || end > img_hi || !memory->LookupHeap(site.first)) {
      continue;
    }
    const uint8_t* host = memory->TranslateVirtual<const uint8_t*>(site.first);
    std::string words;
    for (uint32_t i = 0; i < site.words; ++i) {
      const uint8_t* w = host + i * 4;
      words += std::format("{}{:02X}{:02X}{:02X}{:02X}", i ? " " : "", w[0], w[1], w[2],
                           w[3]);
    }
    REXSYS_INFO("[patches] code at 0x{:08X} ({}): {}", site.first, site.name, words);
  }
  return applied;
}

bool HighTickRateActive() { return REXCVAR_GET(high_tick_rate); }

bool HigherHfTickRateActive() {
  return HighTickRateActive() && fable2::config::Get().higher_hf_tick_rate;
}

void ApplyTickRates(rex::memory::Memory* memory) {
  if (!memory) {
    return;
  }
  const double lf_old = ReadGuestDouble(memory, kLfTick);
  const double hf_old = ReadGuestDouble(memory, kHfTick);
  const double lf = HighTickRateActive() ? 30.0 : ReadGuestDouble(memory, kLfTickOriginal);
  const double hf = HigherHfTickRateActive() ? 60.0 : ReadGuestDouble(memory, kHfTickOriginal);
  WriteGuestDouble(memory, kLfTick, lf);
  WriteGuestDouble(memory, kHfTick, hf);
  REXSYS_INFO("[tick-rate] high_tick_rate {}: LF tick {} -> {} Hz, HF tick {} -> {} Hz",
              HighTickRateActive() ? "on" : "off", lf_old, lf, hf_old, hf);
  if (HighTickRateActive() && !HigherHfTickRateActive()) {
    REXSYS_WARN("[tick-rate] HF tick left at {} Hz: [patches] higher_hf_tick_rate is false in "
                "fable2_config.toml, and cloth physics misbehaves at 1:1",
                hf);
  }
}

}  // namespace fable2::patches
