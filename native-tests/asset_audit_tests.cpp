// audit_asset_repository: the cooker --audit inventory — category
// classification, scoped sha256 hashing, duplicate detection, .git
// exclusion and deterministic output.

#include <stellar/engine/asset_audit.hpp>
#include <stellar/engine/sha256.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include <nlohmann/json.hpp>

namespace {

int failures = 0;

void check(bool condition, const char *label) {
  if (!condition) {
    std::cerr << "FAIL: " << label << '\n';
    ++failures;
  }
}

void write(const std::filesystem::path &path, const std::string &bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out << bytes;
}

} // namespace

int main() {
  const auto base =
      std::filesystem::temp_directory_path() / "stellar_asset_audit_tests";
  const auto root = base / "repo";
  std::error_code ec;
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(root);

  // Identical texture payloads → a duplicate entry; the source file is
  // categorized but not hashed; .git content is excluded entirely.
  write(root / "art" / "ship.png", "png-bytes-identical");
  write(root / "art" / "copy" / "ship.png", "png-bytes-identical");
  write(root / "audio" / "hit.wav", "wav-bytes");
  write(root / "src" / "main.cpp", "int main(){}");
  write(root / ".git" / "HEAD", "ref: refs/heads/main");

  // Reports live outside the audited root so the inventory does not
  // self-include.
  const auto report = base / "audit.json";
  stellar::engine::audit_asset_repository(root, report, 4);

  nlohmann::json doc;
  {
    std::ifstream in(report);
    in >> doc;
  }
  check(doc.at("fileCount").get<int>() == 4, ".git files are excluded");
  check(doc.at("errors").empty(), "no errors on a clean tree");

  const auto &files = doc.at("files");
  bool saw_texture = false, saw_audio = false, saw_source = false;
  for (const auto &f : files) {
    const auto path = f.at("path").get<std::string>();
    const auto cat = f.at("category").get<std::string>();
    const auto hash = f.at("sha256").get<std::string>();
    if (cat == "textures") {
      saw_texture = true;
      check(hash.size() == 64, "texture files are hashed");
    }
    if (cat == "audio") {
      saw_audio = true;
      check(hash.size() == 64, "audio files are hashed");
    }
    if (cat == "source") {
      saw_source = true;
      check(hash.empty(), "source files are not hashed");
    }
    check(!path.starts_with(".git"), ".git paths never list");
  }
  check(saw_texture && saw_audio && saw_source,
        "categories cover texture/audio/source");

  // The hash in the report matches an independent digest.
  const auto &tex = *std::ranges::find_if(
      files, [](const auto &f) {
        return f.at("path").get<std::string>() == "art/ship.png";
      });
  check(tex.at("sha256").get<std::string>() ==
            stellar::engine::sha256_file(root / "art" / "ship.png"),
        "reported sha256 matches an independent digest");

  check(doc.at("duplicates").size() == 1,
        "identical textures produce one duplicate entry");
  if (!doc.at("duplicates").empty()) {
    const auto &dup = doc.at("duplicates").front();
    check(dup.at("copies").get<int>() == 2 &&
              dup.at("paths").size() == 2,
          "the duplicate lists both copies");
    check(dup.at("redundantBytes").get<std::uint64_t>() ==
              std::string("png-bytes-identical").size(),
          "redundantBytes counts the extra copy");
  }
  check(doc.at("duplicateAssetBytes").get<std::uint64_t>() ==
            std::string("png-bytes-identical").size(),
        "duplicateAssetBytes aggregates the redundant copies");

  check(doc.at("categories").at("textures").at("count").get<int>() == 2,
        "category counts aggregate");
  check(doc.at("largestFiles").front().at("bytes").get<std::uint64_t>() >=
            doc.at("largestFiles").back().at("bytes").get<std::uint64_t>(),
        "largestFiles sorts descending");

  // Determinism: a second run produces byte-identical JSON.
  const auto report2 = base / "audit2.json";
  stellar::engine::audit_asset_repository(root, report2, 1);
  const auto a = std::filesystem::file_size(report);
  const auto b = std::filesystem::file_size(report2);
  check(a == b, "repeat audits are deterministic");
  {
    std::ifstream x(report, std::ios::binary), y(report2, std::ios::binary);
    check(std::equal(std::istreambuf_iterator<char>(x), {},
                     std::istreambuf_iterator<char>(y)),
          "repeat audits are byte-identical");
  }

  std::filesystem::remove_all(base, ec);
  if (failures == 0) std::cout << "asset audit tests passed\n";
  return failures == 0 ? 0 : 1;
}
