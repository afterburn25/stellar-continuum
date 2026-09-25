#pragma once

#include "native_economy.hpp"

#include <stellar/engine/localization.hpp>
#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/ui_viewmodels.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::native_economy {

struct EconomyLayout {
  float scale{};
  int heading_font_pixels{}, body_font_pixels{}, small_font_pixels{};
  native_map::UiRect panel, header, body, close, refresh, notice;
  std::array<native_map::UiRect, 3> priority_buttons;

  [[nodiscard]] static EconomyLayout for_viewport(int width, int height) noexcept;
};

enum class EconomyCommandKind { None, Close, Refresh, SetIndustryPriority };

struct EconomyCommand {
  EconomyCommandKind kind{EconomyCommandKind::None};
  bool captured{};
  core::IndustryPriority priority{core::IndustryPriority::Balanced};
  std::uint64_t view_revision{};
};

class NativeEconomyWorkspace final {
 public:
  using TextMeasurer = std::function<native_map::TextExtent(const native_map::Text&)>;

  void open() noexcept;
  void close() noexcept;
  void clear() noexcept;
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  [[nodiscard]] int focus() const noexcept { return focus_; }
  // Localized label of the ringed control for screen-reader/live-region
  // consumers. Empty when nothing is focused.
  [[nodiscard]] std::string focused_label(const NativeEconomyView &) const;
  // Client-pixel rect of the ringed control — null when nothing is focused.
  [[nodiscard]] std::optional<native_map::UiRect>
  focused_bounds(int width, int height) const;
  void set_text_measurer(TextMeasurer measure);
  void set_localization(const stellar::engine::LocalizationTable *table);
  void set_notice(std::string notice);
  [[nodiscard]] float scroll_offset() const noexcept {
    return scroll_.scroll_offset;
  }

  [[nodiscard]] EconomyCommand handle(const native_map::InputEvent&, const NativeEconomyView&,
                                      int width, int height);
  void render(native_map::DrawList&, const NativeEconomyView&, int width, int height) const;

 private:
  struct Row { std::string left, right; bool income{}, warning{}, tile{}; int tile_column{}; float y{}, height{}; };
  struct Cache {
    int width{}, height{};
    std::uint64_t generation{}, revision{}, measure_revision{};
    std::string signature;
    std::vector<Row> rows;
    float content_height{};
    bool valid{};
  };
  enum class PressTarget { None, Close, Refresh, Priority0, Priority1, Priority2, Body };

  [[nodiscard]] const Cache& cache_for(const NativeEconomyView&, const EconomyLayout&, int, int) const;
  void reset_gesture() noexcept;
  [[nodiscard]] PressTarget hit(native_map::Point, const EconomyLayout&) const noexcept;
  [[nodiscard]] static core::IndustryPriority priority_for(PressTarget) noexcept;
  [[nodiscard]] std::string tr(std::string_view key,
                               std::string_view fallback) const;
  [[nodiscard]] std::string
  trf(std::string_view key, std::initializer_list<std::string> args,
      std::string_view fallback) const;

  const stellar::engine::LocalizationTable *locale_{};
  bool visible_{}, pointer_owned_{}, dragging_{};
  int focus_{-1};
  PressTarget pressed_{PressTarget::None};
  native_map::Point press_point_{};
  float press_scroll_{};
  mutable stellar::engine::ScrollView scroll_{};
  TextMeasurer measure_;
  std::string notice_;
  std::uint64_t measure_revision_{};
  std::uint64_t observed_generation_{}, observed_revision_{};
  int observed_observer_{};
  mutable Cache cache_;
};

} // namespace stellar::native_economy
