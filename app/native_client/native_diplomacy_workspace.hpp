#pragma once

#include "native_diplomacy_controller.hpp"

#include <stellar/engine/localization.hpp>
#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/ui_viewmodels.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::native_diplomacy_ui {

struct DiplomacyWorkspaceLayout {
  float scale{};
  int title_font_pixels{};
  int body_font_pixels{};
  int small_font_pixels{};
  stellar::native_map::UiRect surface;
  stellar::native_map::UiRect title;
  stellar::native_map::UiRect date;
  stellar::native_map::UiRect close;
  stellar::native_map::UiRect contact_panel;
  stellar::native_map::UiRect contact_rows;
  stellar::native_map::UiRect stage;
  stellar::native_map::UiRect stage_caption;
  stellar::native_map::UiRect meter_panel;
  stellar::native_map::UiRect meters;
  stellar::native_map::UiRect actions;
  stellar::native_map::UiRect tabs;
  stellar::native_map::UiRect detail_rows;
  stellar::native_map::UiRect feedback;
  stellar::native_map::UiRect modal_panel;

  [[nodiscard]] static DiplomacyWorkspaceLayout
  for_viewport(int width, int height) noexcept;
};

enum class DiplomacyWorkspaceTab {
  agreements,
  proposals,
  history,
  intelligence,
  overview,
};

enum class DiplomacyWorkspaceCommandKind {
  None,
  Close,
  SelectContact,
  Action,
  ProposalAction,
  FocusSystem,
};

struct DiplomacyWorkspaceCommand {
  DiplomacyWorkspaceCommandKind kind{DiplomacyWorkspaceCommandKind::None};
  bool captured{};
  std::size_t contact_index{};
  std::optional<int> target_civilization_id;
  std::optional<std::int64_t> proposal_id;
  stellar::native_diplomacy::DiplomacyWorkspaceAction action{
      stellar::native_diplomacy::DiplomacyWorkspaceAction::
          establish_communication};
  int focus_system_id{};
  std::uint64_t campaign_generation{};
  std::uint64_t diplomacy_revision{};
};

class NativeDiplomacyWorkspace final {
public:
  using PortraitProvider = std::function<std::shared_ptr<
      const stellar::native_map::RgbaImage>(std::string_view)>;

  void open() noexcept;
  void close() noexcept;
  void
  set_localization(const stellar::engine::LocalizationTable *table) noexcept {
    locale_ = table;
  }
  [[nodiscard]] bool visible() const noexcept;
  void set_view(stellar::native_diplomacy::NativeDiplomacyView view);
  void discard_campaign();
  void set_notice(std::string message, bool accepted);
  [[nodiscard]] bool modal_open() const noexcept;
  void dismiss_modal() noexcept;

  [[nodiscard]] const std::optional<
      stellar::native_diplomacy::NativeDiplomacyView> &
  view() const noexcept;
  [[nodiscard]] std::size_t selected_contact_index() const noexcept;
  [[nodiscard]] bool select_contact_civilization(int civilization_id);
  [[nodiscard]] const std::string &notice() const noexcept;
  [[nodiscard]] int focus() const noexcept { return focus_; }
  // Localized label of the ringed control — the announcement surface for
  // screen-reader/live-region consumers. Empty when nothing is focused.
  [[nodiscard]] std::string focused_label(int width, int height) const;
  // Client-pixel rect of the ringed control — null when nothing is focused.
  [[nodiscard]] std::optional<stellar::native_map::UiRect>
  focused_bounds(int width, int height) const;

  [[nodiscard]] DiplomacyWorkspaceCommand
  handle(const stellar::native_map::InputEvent &event, int width, int height);
  void render(stellar::native_map::DrawList &out, int width, int height,
              const PortraitProvider *portrait_provider = nullptr) const;

private:
  struct ModalState {
    bool negotiation{};
    std::string title;
    std::string description;
    stellar::native_diplomacy::DiplomacyWorkspaceAction action{
        stellar::native_diplomacy::DiplomacyWorkspaceAction::declare_war};
    std::optional<int> target_civilization_id;
    std::uint64_t campaign_generation{};
    std::uint64_t diplomacy_revision{};
    bool danger{};
    std::string confirm_label;
    struct ModalTerm {
      std::string label;
      stellar::native_diplomacy::DiplomacyWorkspaceAction action{
          stellar::native_diplomacy::DiplomacyWorkspaceAction::declare_war};
      bool enabled{};
      // Authoritative status surfaced as the why when `enabled` is false.
      std::string tip;
    };
    std::vector<ModalTerm> terms;
  };

  void reconcile_selection();
  struct FocusRect {
    stellar::native_map::UiRect bounds;
    std::string label;
    // Set when `bounds` was clipped to a scroll viewport: the row's
    // translated, unclipped rect plus which lane scrolls it — 1 contacts,
    // 2 detail — so keyboard focus can snap the row fully into view.
    std::optional<stellar::native_map::UiRect> unclipped;
    int scroll_lane{0};
  };
  [[nodiscard]] std::vector<FocusRect>
  focusables(const DiplomacyWorkspaceLayout &layout) const;
  [[nodiscard]] float
  detail_content_height(const DiplomacyWorkspaceLayout &layout) const noexcept;
  [[nodiscard]] std::vector<const stellar::native_diplomacy::
                                NativeDiplomacyContact *>
  filtered_contacts() const;
  [[nodiscard]] std::string tr(std::string_view key,
                               std::string_view fallback) const;
  [[nodiscard]] std::string
  trf(std::string_view key, std::initializer_list<std::string> args,
      std::string_view fallback) const;

  const stellar::engine::LocalizationTable *locale_{};
  bool visible_{};
  stellar::native_map::Point pointer_{};
  std::optional<stellar::native_diplomacy::NativeDiplomacyView> view_;
  std::size_t selected_contact_index_{};
  std::optional<std::string> selected_contact_id_;
  stellar::native_diplomacy::NativeDiplomacyContactFilter filter_{
      stellar::native_diplomacy::NativeDiplomacyContactFilter::all};
  DiplomacyWorkspaceTab tab_{DiplomacyWorkspaceTab::agreements};
  std::optional<ModalState> modal_;
  std::string notice_;
  bool notice_accepted_{};
  stellar::engine::ScrollView contact_scroll_{};
  stellar::engine::ScrollView detail_scroll_{};
  int focus_{-1};
};

} // namespace stellar::native_diplomacy_ui
