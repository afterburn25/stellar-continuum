#include "native_research_art.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>

namespace stellar::native_research_ui {
namespace {
bool safe_id(std::string_view value) {
  return !value.empty() && value.size() <= 160 &&
      std::ranges::all_of(value, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
      });
}
}
NativeResearchArt::NativeResearchArt(std::filesystem::path asset_root)
    : root_(std::move(asset_root) / "assets/visual/catalog") {
  std::ifstream stream(root_ / "catalog.json");
  if (!stream) throw std::runtime_error("Cannot load research illustration catalog: " + root_.string());
  const auto catalog = nlohmann::json::parse(stream);
  if (catalog.at("schemaVersion") != 1 || !catalog.at("research").is_array() ||
      catalog.at("research").size() > 4096)
    throw std::runtime_error("Unsupported research illustration catalog.");
  std::set<std::string> families;
  for (const auto &record : catalog.at("research")) {
    const auto id = record.at("id").get<std::string>();
    const auto art = record.at("art").get<std::string>();
    if (!safe_id(id) || !safe_id(art) || !art.starts_with("research-") ||
        !bindings_.emplace(id, art).second)
      throw std::runtime_error("Invalid or duplicate research illustration binding.");
    families.insert(art);
  }
  if (families.size() > 128) throw std::runtime_error("Research thumbnail cache budget exceeded.");
}

std::shared_ptr<const stellar::native_map::RgbaImage>
NativeResearchArt::image(std::string_view known_node_id, bool portrait) {
  const auto binding = bindings_.find(known_node_id);
  if (binding == bindings_.end()) return {};
  auto &cache = portrait ? portraits_ : thumbnails_;
  auto found = cache.find(binding->second);
  if (found != cache.end()) {
    found->second.used = ++clock_;
    return found->second.image;
  }
  const auto path = root_ / (portrait ? "portraits" : "thumbnails") / (binding->second + ".png");
  auto image = stellar::native_map::decode_rgba_image(path);
  const int expected = portrait ? 512 : 128;
  if (!image || image->width() != expected || image->height() != expected)
    throw std::runtime_error("Research illustration has incorrect dimensions: " + path.string());
  if (portrait && cache.size() >= 12) {
    const auto oldest = std::ranges::min_element(cache, {}, [](const auto &entry) { return entry.second.used; });
    cache.erase(oldest);
  }
  ++decode_count_;
  cache.emplace(binding->second, Cached{image, ++clock_});
  return image;
}

std::size_t NativeResearchArt::cached_bytes() const noexcept {
  std::size_t bytes{};
  for (const auto &entry : thumbnails_) bytes += entry.second.image->byte_size();
  for (const auto &entry : portraits_) bytes += entry.second.image->byte_size();
  return bytes;
}
}
