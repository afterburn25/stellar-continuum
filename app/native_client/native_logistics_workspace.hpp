#pragma once
#include "native_logistics.hpp"
#include <stellar/engine/native_map_platform.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
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
  void open() noexcept;
  void close() noexcept;
  bool visible() const noexcept { return visible_; }
  float scroll_offset() const noexcept { return scroll_; }
  SupplyCommand handle(const stellar::native_map::InputEvent&, const View&, int width, int height);
  void render(stellar::native_map::DrawList&, const View&, int width, int height) const;
private:
  struct CachedRow { std::size_t index{}; float y{}, height{}, name_height{}; };
  struct CachedRows {
    int viewport_width{}, viewport_height{};
    std::uint64_t measurer_revision{};
    std::vector<NodeRow> nodes;
    std::vector<CachedRow> rows;
    float height{};
    bool valid{};
  };
  [[nodiscard]] const CachedRows &rows_for(const View &, const SupplyLayout &,
                                            int, int) const;
  void clear_rows() noexcept;
  std::function<stellar::native_map::TextExtent(const stellar::native_map::Text&)> measure_;
  bool visible_{}, owned_{};
  mutable float scroll_{};
  std::uint64_t measurer_revision_{};
  mutable CachedRows rows_;
};
} // namespace stellar::native_logistics
