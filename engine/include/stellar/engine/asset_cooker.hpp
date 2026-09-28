#pragma once
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
namespace stellar::engine {
struct AssetCookOptions {
  std::filesystem::path root,output,cache,report;
  std::string profile{"release"},category;
  // `scan_content` switches discovery to a generic recursive scan of `root`:
  // every regular file becomes a recipe whose runtime alias is its relative
  // POSIX path, packaged under `package_group`. This is the project mode —
  // no reviewed Stellar Continuum export manifests are required, so any game
  // project can cook its own content tree.
  std::string package_group{"Game"};
  unsigned threads{4};
  bool clean{},package{true},validate{true},scan_content{};
  // Optional per-asset progress hook invoked from cook workers as each
  // recipe finishes; used by tools to stream cook progress.
  std::function<void(std::size_t done,std::size_t total)> progress;
};
void cook_asset_repository(const AssetCookOptions&);
}
