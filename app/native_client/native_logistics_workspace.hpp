#pragma once
#include "native_logistics.hpp"
#include <stellar/engine/localization.hpp>
#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/ui_viewmodels.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::native_logistics {
struct SupplyLayout {
  stellar::native_map::UiRect panel, body, close, refresh;
  float scale{};
  static SupplyLayout for_viewport(int width, int height);
};
struct SupplyCommand { bool captured{}, refresh{}; };
class SupplyWorkspace {
public:
  void set_text_measurer(std::function<stellar::native_map::TextExtent(const stellar::native_map::Text&)> value);
  void set_localization(const stellar::engine::LocalizationTable *table) noexcept { locale_ = table; }
  void open() noexcept;
  void close() noexcept;
  bool visible() const noexcept { return visible_; }
  int focus() const noexcept { return focus_; }
  // Localized label of the ringed control for screen-reader/live-region
  // consumers. Empty when nothing is focused.
  [[nodiscard]] std::string focused_label(const View &) const;
  // Client-pixel rect of the ringed control — null when nothing is focused.
  [[nodiscard]] std::optional<stellar::native_map::UiRect>
  focused_bounds(int width, int height) const;
  float scroll_offset() const noexcept { return scroll_.scroll_offset; }
  SupplyCommand handle(const stellar::native_map::InputEvent&, const View&, int width, int height);
  void render(stellar::native_map::DrawList&, const View&, int width, int height) const;
private:
  struct CachedRow {
    std::size_t index{};
    float y{}, height{}, name_height{};
    // 0 = node row (index into View::nodes), 1 = corridor section header,
    // 2 = corridor row (index into View::links).
    int kind{};
  };
  struct CachedRows {
    int viewport_width{}, viewport_height{};
    std::uint64_t measurer_revision{};
    std::vector<NodeRow> nodes;
    std::vector<LinkRow> links;
    std::vector<CachedRow> rows;
    float height{};
    bool valid{};
  };
  [[nodiscard]] const CachedRows &rows_for(const View &, const SupplyLayout &,
                                            int, int) const;
  void clear_rows() noexcept;
  [[nodiscard]] std::string tr(std::string_view key, std::string_view fallback) const;
  [[nodiscard]] std::string trf(std::string_view key, std::initializer_list<std::string> args,
                                std::string_view fallback) const;
  const stellar::engine::LocalizationTable *locale_{};
  std::function<stellar::native_map::TextExtent(const stellar::native_map::Text&)> measure_;
  bool visible_{}, owned_{};
  int focus_{-1};
  stellar::native_map::Point pointer_{};
  mutable stellar::engine::ScrollView scroll_{};
  std::uint64_t measurer_revision_{};
  mutable CachedRows rows_;
};
} // namespace stellar::native_logistics
