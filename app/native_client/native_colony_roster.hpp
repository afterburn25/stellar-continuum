#pragma once

#include <stellar/core/fresh_campaign.hpp>
#include <stellar/engine/localization.hpp>
#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/ui_viewmodels.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace stellar::native_colony_roster {

struct Row {
  int colony_id{}, body_id{}, system_id{};
  std::string name, body_name, system_name, kind_label, population;
  double population_millions{};
  bool can_open{};
  std::string reason;
  bool operator==(const Row &) const = default;
};

struct View {
  std::uint64_t generation{};
  int player_id{};
  std::vector<Row> rows;
  std::string message;
  bool available{};
  bool developer_inspection{};
};

[[nodiscard]] View build(const stellar::core::FreshCampaignState &,
                         std::uint64_t generation,
                         const stellar::engine::LocalizationTable *locale =
                             nullptr);

struct RosterLayout {
  stellar::native_map::UiRect panel, list, close, refresh, search;
  float scale{}, row_height{};
  [[nodiscard]] static RosterLayout for_viewport(int width,
                                                 int height) noexcept;
};

struct RosterCommand {
  bool captured{}, refresh{};
  std::optional<int> open_colony_id;
  std::uint64_t generation{};
  int player_id{}, body_id{}, system_id{};
};

class RosterWorkspace final {
public:
  void set_view(View);
  [[nodiscard]] const View &view() const noexcept { return view_; }
  void open() noexcept;
  void close() noexcept;
  void discard_campaign() noexcept;
  void cancel_pending_input() noexcept { clear_press(); }
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  [[nodiscard]] bool wants_text_input() const noexcept {
    return visible_ && search_focused_;
  }
  [[nodiscard]] float scroll_offset() const noexcept { return list_.scroll_offset; }
  [[nodiscard]] int focus() const noexcept { return focus_; }
  void set_notice(std::string value) { notice_ = std::move(value); }
  void set_localization(
      const stellar::engine::LocalizationTable *table) noexcept {
    locale_ = table;
  }
  [[nodiscard]] RosterCommand handle(const stellar::native_map::InputEvent &,
                                     int width, int height);
  void render(stellar::native_map::DrawList &, int width, int height) const;
  [[nodiscard]] stellar::native_map::UiRect
  row_button(int row_index, int width, int height) const noexcept;

private:
  enum class PressTarget { none, close, refresh, row };
  void clear_press() noexcept;
  void sync_scroll(const RosterLayout &) const noexcept;
  [[nodiscard]] float maximum_scroll(const RosterLayout &) const noexcept;
  [[nodiscard]] std::string tr(std::string_view key,
                               std::string_view fallback) const;
  void rebuild_table();
  void apply_display_order();
  void apply_filter();
  // 0=colony,1=world,2=population; -1 when the point misses the headers.
  [[nodiscard]] int header_column(stellar::native_map::Point,
                                  const RosterLayout &) const noexcept;
  [[nodiscard]] std::vector<stellar::native_map::UiRect>
  focusables(const RosterLayout &) const;
  const stellar::engine::LocalizationTable *locale_{};
  View view_;
  bool visible_{};
  mutable stellar::engine::VirtualizedList list_{};
  stellar::engine::TableModel table_{};
  std::vector<int> display_order_{}; // display position -> view_.rows index
  std::string notice_, search_;
  bool search_focused_{};
  std::optional<int> pressed_row_;
  PressTarget pressed_target_{PressTarget::none};
  bool pointer_owned_{};
  int focus_{-1};
  stellar::native_map::Point pointer_{};
  int viewport_width_{}, viewport_height_{};
  std::uint64_t pressed_generation_{};
  int pressed_player_id_{};
};

} // namespace stellar::native_colony_roster
