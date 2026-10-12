#include "../../src/core/xex_verify.h"
#include <iostream>
#include <stdexcept>

static void Require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
  using namespace fable2::xexverify;
  try {
    const auto root = std::filesystem::temp_directory_path() /
        ("fable2-xex-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Require(std::filesystem::create_directory(root), "fixture directory already exists");
    struct Cleanup {
      std::filesystem::path path;
      ~Cleanup() {
        // Only this newly-created, uniquely named synthetic fixture directory.
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
      }
    } cleanup{root};
    const auto fixture = root / "synthetic.xex";
    { std::ofstream out(fixture, std::ios::binary); out << "abc"; }
    std::string digest;
    Require(Sha256File(fixture, digest) && digest == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 abc vector");
    Require(Check(root / "missing", root / "marker").result == Result::ReadFailed, "missing file accepted");
    Require(Check(fixture, root / "marker").result == Result::Mismatch, "unknown fixture accepted");
    Require(!IsAcceptedHash("unknown") && DefaultLanguage("unknown") == 0, "unknown version accepted");
    for (const auto& version : fable2::versions::kVersions) {
      Require(IsAcceptedHash(version.hash) == IsEnabledVersion(version), "catalogue acceptance differs");
      Require(DefaultLanguage(version.hash) == version.language, "catalogue language differs");
      if (!IsEnabledVersion(version)) {
        Require(!HasCompatibleContent(root, version.hash), "disabled/unsupported profile accepted");
        continue;
      }
      const auto content = root / version.id;
      std::filesystem::create_directory(content);
      for (auto marker : version.required_files) {
        std::filesystem::create_directories((content / marker).parent_path());
        std::ofstream(content / marker) << "synthetic";
      }
      Require(HasCompatibleContent(content, version.hash), "catalogue markers rejected");
      for (auto marker : version.required_files) {
        std::filesystem::remove(content / marker);
        Require(!HasCompatibleContent(content, version.hash), "missing required marker accepted");
        std::ofstream(content / marker) << "synthetic";
      }
      for (auto marker : version.reject_files) {
        std::filesystem::create_directories((content / marker).parent_path());
        std::ofstream(content / marker) << "synthetic";
      }
      for (auto marker : version.reject_directories)
        std::filesystem::create_directories(content / marker);
      if (!version.reject_files.empty() || !version.reject_directories.empty()) {
        Require(!HasCompatibleContent(content, version.hash), "mixed content accepted");
        for (auto marker : version.reject_files) {
          std::filesystem::remove(content / marker);
          Require(HasCompatibleContent(content, version.hash), "partial rejection rule rejected a matching dump");
          std::ofstream(content / marker) << "synthetic";
        }
      }
    }
    for (int i = 1; i < argc; ++i) {
      auto first = Check(argv[i], root / "marker");
      Require(first.result == Result::VerifiedFresh, "original profile hash failed or cross-root cache reused");
      Require(Check(argv[i], root / "marker").result == Result::VerifiedCached, "same-root cache failed");
      std::cout << "Verified original profile " << first.actual_hash << '\n';
    }
    std::cout << "Catalogue-driven hash/language/content and cache tests passed: " << kBuildProfile << '\n';
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
