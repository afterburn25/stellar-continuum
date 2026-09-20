#pragma once

#include <stellar/engine/native_map_platform.hpp>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace stellar::native_research_ui {
// Presentation lookup only. Callers supply IDs from the observer's known view.
// Planned bindings are deliberately not loaded into this catalog.
class NativeResearchArt final {
public:
  explicit NativeResearchArt(std::filesystem::path asset_root);
  [[nodiscard]] std::shared_ptr<const stellar::native_map::RgbaImage>
  image(std::string_view known_node_id, bool portrait);
  [[nodiscard]] std::size_t cached_bytes() const noexcept;
  [[nodiscard]] std::size_t decode_count() const noexcept { return decode_count_; }
private:
  struct Cached {
    std::shared_ptr<const stellar::native_map::RgbaImage> image;
    std::size_t used{};
  };
  std::filesystem::path root_;
  std::map<std::string, std::string, std::less<>> bindings_;
  std::map<std::string, Cached, std::less<>> thumbnails_, portraits_;
  std::size_t clock_{}, decode_count_{};
};
}
