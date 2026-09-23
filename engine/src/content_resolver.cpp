#include "stellar/engine/content_resolver.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace stellar::engine {

ContentResolver::ContentResolver(std::string package_id,
                                 std::filesystem::path project_root,
                                 std::filesystem::path exe_dir)
    : package_id_(std::move(package_id)),
      project_root_(std::move(project_root)) {
  for (const auto manifest :
       {exe_dir / "Content" / "runtime.stmanifest",
        project_root_ / "build" / "cooked" / "Content" /
            "runtime.stmanifest"}) {
    if (std::filesystem::is_regular_file(manifest)) {
      cooked_ = std::make_unique<AssetRegistry>(manifest);
      break;
    }
  }
}

const AssetRecord *ContentResolver::find_cooked(std::string_view relpath) const {
  if (!cooked_) return nullptr;
  std::string alias{relpath};
  std::replace(alias.begin(), alias.end(), '\\', '/');
  return cooked_->find(package_id_ + "/content/" + alias);
}

std::optional<std::vector<std::uint8_t>>
ContentResolver::read_cooked(std::string_view relpath, std::size_t chunk) const {
  const auto *record = find_cooked(relpath);
  if (record == nullptr || chunk >= record->chunks.size()) return std::nullopt;
  try {
    return cooked_->read(*record, chunk);
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

std::filesystem::path
ContentResolver::loose_path(std::string_view relpath) const {
  return project_root_ / "packages" / package_id_ / "content" /
         std::filesystem::path(std::string{relpath});
}

std::optional<std::vector<std::uint8_t>>
ContentResolver::read_bytes(std::string_view relpath) const {
  if (const auto cooked = read_cooked(relpath, 0)) return cooked;
  const auto path = loose_path(relpath);
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(in),
                                   std::istreambuf_iterator<char>()};
}

std::size_t ContentResolver::cooked_count() const {
  return cooked_ ? cooked_->records().size() : 0;
}

} // namespace stellar::engine
