#pragma once

#include "native_shipyard_controller.hpp"
#include "native_ship_art_assets.hpp"
#include "native_dropdown.hpp"
#include <filesystem>

#include <stellar/engine/localization.hpp>
#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/ui_viewmodels.hpp>

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace stellar::native_shipyard_ui {

struct ShipyardWorkspaceLayout {
  float scale{};
  int title_font_pixels{};
  int body_font_pixels{};
  int small_font_pixels{};
  stellar::native_map::UiRect surface;
  stellar::native_map::UiRect title;
  stellar::native_map::UiRect close;
  stellar::native_map::UiRect designs;
  stellar::native_map::UiRect design_details;
  stellar::native_map::UiRect orders;
  stellar::native_map::UiRect readiness;
  stellar::native_map::UiRect feedback;
  stellar::native_map::UiRect action;
  stellar::native_map::UiRect categories,search,sort,filter,minus,plus,quantity,favorite;

  [[nodiscard]] static ShipyardWorkspaceLayout for_viewport(int width,
                                                             int height) noexcept;
};

enum class ShipyardWorkspaceCommandKind { None, Start, PrepareCancel, Cancel, MoveUp, MoveDown };

struct ShipyardWorkspaceCommand {
  ShipyardWorkspaceCommandKind kind{ShipyardWorkspaceCommandKind::None};
  bool captured{};
  std::string id;
  int quantity{1};
};

class NativeShipyardWorkspace final {
public:
  void open() noexcept;
  using TextMeasurer=std::function<stellar::native_map::TextExtent(const stellar::native_map::Text&)>;
  void set_text_measurer(TextMeasurer value){measure_=std::move(value);}
  [[nodiscard]] bool popover_open()const{return dropdown_.visible();}
  void bind_preferences(std::filesystem::path);
  [[nodiscard]] bool wants_text_input()const{return visible_&&search_focused_;}
  [[nodiscard]] int focus() const noexcept { return focus_; }
  // Localized label of the ringed control for screen-reader/live-region
  // consumers. Empty when nothing is focused.
  [[nodiscard]] std::string
  focused_label(const ShipyardWorkspaceLayout &) const;
  // Client-pixel rect of the ringed control — null when nothing is focused.
  [[nodiscard]] std::optional<stellar::native_map::UiRect>
  focused_bounds(const ShipyardWorkspaceLayout &) const;
  void close() noexcept;
  [[nodiscard]] bool visible() const noexcept;
  [[nodiscard]] bool confirmation_open() const noexcept {
    return cancel_confirmation_id_.has_value();
  }

  void set_view(stellar::native_shipyard::NativeShipyardView view);
  void discard_campaign();
  void set_notice(std::string message, bool accepted);
  void set_localization(
      const stellar::engine::LocalizationTable *table) noexcept {
    locale_ = table;
  }
  [[nodiscard]] bool arm_cancel_confirmation(std::string_view order_id);
  [[nodiscard]] const std::optional<stellar::native_shipyard::NativeShipyardView> &
  view() const noexcept;
  [[nodiscard]] const std::optional<std::string> &selected_design_id() const noexcept;
  [[nodiscard]] const std::optional<std::string> &selected_order_id() const noexcept;
  [[nodiscard]] std::optional<stellar::native_map::UiRect>
  design_bounds(std::string_view design_id, int width, int height) const;

  [[nodiscard]] ShipyardWorkspaceCommand
  handle(const stellar::native_map::InputEvent &event, int width, int height);
  void render(stellar::native_map::DrawList &out, int width, int height,
              stellar::native_ship_ui::NativeShipArtAssets *ship_art = nullptr) const;
  [[nodiscard]] int last_ship_art_rows() const noexcept {
    return last_ship_art_rows_;
  }

private:
  [[nodiscard]] std::vector<const stellar::native_shipyard::NativeShipDesign*> filtered_designs()const;
  [[nodiscard]] std::string batch_blocker()const;
  [[nodiscard]] stellar::native_map::UiRect card(std::size_t,const ShipyardWorkspaceLayout&)const;
  bool save_preferences();
  [[nodiscard]] const stellar::native_shipyard::NativeShipDesign *
  selected_design() const noexcept;
  [[nodiscard]] const stellar::native_shipyard::NativeShipyardOrder *
  selected_order() const noexcept;
  void reconcile_selection();
  // Keyboard-focus contract: (y,x)-ordered controls across the whole
  // dashboard; activation replays the authoritative click dispatch.
  struct FocusItem {
    stellar::native_map::UiRect rect;
    std::uint64_t target;
    std::string label;
  };
  [[nodiscard]] std::vector<FocusItem> focusables(
      const ShipyardWorkspaceLayout &) const;
  [[nodiscard]] std::string tr(std::string_view key,
                               std::string_view fallback) const;
  [[nodiscard]] std::string
  trf(std::string_view key, std::initializer_list<std::string> args,
      std::string_view fallback) const;

  const stellar::engine::LocalizationTable *locale_{};
  bool visible_{};
  stellar::native_map::Point pointer_{};
  std::optional<stellar::native_shipyard::NativeShipyardView> view_;
  std::optional<std::string> selected_design_id_;
  std::optional<std::string> selected_order_id_;
  std::optional<std::string> cancel_confirmation_id_;
  std::string notice_;
  bool notice_accepted_{};
  mutable stellar::engine::ScrollView design_scroll_{};
  mutable stellar::engine::ScrollView order_scroll_{};
  mutable int last_ship_art_rows_{};
  std::filesystem::path preferences_path_;
  std::vector<std::string> favorites_;
  std::string search_;
  int category_{},sort_{},filter_{},quantity_{1};
  bool search_focused_{};
  int focus_{-1};
  mutable stellar::engine::ScrollView detail_scroll_{};
  stellar::native_ui::Dropdown dropdown_;
  TextMeasurer measure_;
};

} // namespace stellar::native_shipyard_ui
