#pragma once
#include <filesystem>
#include <string>
namespace stellar::engine {
struct AssetCookOptions {
  std::filesystem::path root,output,cache,report;
  std::string profile{"release"},category;
  unsigned threads{4};
  bool clean{},package{true},validate{true};
};
void cook_asset_repository(const AssetCookOptions&);
}
