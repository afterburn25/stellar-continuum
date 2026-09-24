#pragma once

#include "native_research_controller.hpp"
#include "native_dropdown.hpp"
#include <stellar/engine/accessibility.hpp>
#include <stellar/engine/localization.hpp>
#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/ui_viewmodels.hpp>

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <optional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::native_research_ui {

enum class ResearchViewMode { Guided, Tree, Recent, Favorites, Completed, Queue };

enum class WorkspaceCommandKind { None, Close, Select, Execute };

struct WorkspaceCommand {
  WorkspaceCommandKind kind{WorkspaceCommandKind::None};
  bool captured{};
  std::string node_id;
  stellar::native_research::NativeResearchIntent intent{
      stellar::native_research::NativeResearchIntent::None};
};

struct ResearchTabLayout {
  std::size_t index{};
  stellar::native_map::UiRect bounds;
};

struct ResearchWorkspaceLayout {
  float scale{};
  int title_font_pixels{};
  int body_font_pixels{};
  int small_font_pixels{};
  stellar::native_map::UiRect surface;
  stellar::native_map::UiRect title;
  stellar::native_map::UiRect labs;
  stellar::native_map::UiRect search;
  stellar::native_map::UiRect close;
  std::vector<ResearchTabLayout> tabs;
  stellar::native_map::UiRect graph;
  stellar::native_map::UiRect inspector;
  stellar::native_map::UiRect feedback;
  stellar::native_map::UiRect action;
  stellar::native_map::UiRect sidebar,view_tabs,toolbar,active,bookmark,enqueue,tree_focus,completed,queue,filter,sort;

  [[nodiscard]] static ResearchWorkspaceLayout
  for_viewport(int width, int height, std::size_t tab_count);
};

class NativeResearchWorkspace final {
public:
  using TextMeasurer = std::function<stellar::native_map::TextExtent(
      const stellar::native_map::Text &)>;
  using ArtworkResolver = std::function<std::shared_ptr<const stellar::native_map::RgbaImage>(
      std::string_view node_id, bool portrait)>;
  void set_text_measurer(TextMeasurer measure);
  void
  set_localization(const stellar::engine::LocalizationTable *table) noexcept {
    locale_ = table;
  }
  [[nodiscard]] const stellar::core::AdaptiveResearchPlan& plan()const{return plan_;}
  void set_view_mode(ResearchViewMode mode){mode_=mode;guided_scroll_={};}
  [[nodiscard]] ResearchViewMode view_mode()const{return mode_;}
  void set_artwork_resolver(ArtworkResolver resolve);
  void open();
  void close();
  [[nodiscard]] bool popover_open()const{return dropdown_.visible()||why_open_;}
  [[nodiscard]] bool visible() const noexcept;
  [[nodiscard]] bool wants_text_input() const noexcept;

  void set_window(stellar::native_research::NativeResearchWindow window);
  void discard_campaign();
  [[nodiscard]] const stellar::native_research::NativeResearchQuery &
  query() const noexcept;
  [[nodiscard]] bool take_refresh_request() noexcept;
  [[nodiscard]] const std::optional<
      stellar::native_research::NativeResearchWindow> &
  window() const noexcept;
  [[nodiscard]] const std::optional<std::string> &selected_id() const noexcept;

  [[nodiscard]] WorkspaceCommand
  handle(const stellar::native_map::InputEvent &event, int width, int height);
  void render(stellar::native_map::DrawList &out, int width, int height);
  void set_notice(std::string message, bool accepted);

  [[nodiscard]] std::optional<stellar::native_map::UiRect>
  card_bounds(std::string_view node_id, int width, int height) const;
  [[nodiscard]] std::optional<stellar::native_map::UiRect>
  first_actionable_card(int width, int height) const;
  [[nodiscard]] int focus() const noexcept { return focus_; }
  // Localized label of the ringed control for screen-reader/live-region
  // consumers. Empty when nothing is focused.
  [[nodiscard]] std::string focused_label(int width, int height) const;
  // Client-pixel rect of the ringed control — null when nothing is focused.
  [[nodiscard]] std::optional<stellar::native_map::UiRect>
  focused_bounds(int width, int height) const;
  // UIA control kind of the ringed control — Edit on the search field,
  // Custom elsewhere.
  [[nodiscard]] stellar::engine::AnnouncementControl
  focused_control(int width, int height) const;

private:
  struct NodePlacement {
    std::string id;
    std::string domain_id;
    stellar::native_map::UiRect world_bounds;
  };
  struct DomainPlacement {
    std::string label;
    float world_y{};
  };

  void rebuild_topology();
  struct GuidedCard{std::string id;stellar::native_map::UiRect bounds;bool recommended{};};
  struct InterfaceHit{stellar::native_map::UiRect bounds;int action{};std::string id;std::string label;};
  [[nodiscard]] std::vector<GuidedCard> guided_cards(const ResearchWorkspaceLayout&)const;
  void render_dashboard(stellar::native_map::DrawList&,const ResearchWorkspaceLayout&);
  void render_controls(stellar::native_map::DrawList&,const ResearchWorkspaceLayout&);
  std::optional<WorkspaceCommand> handle_controls(const stellar::native_map::InputEvent&,const ResearchWorkspaceLayout&,int,int);
  struct FocusRect {
    stellar::native_map::UiRect bounds;
    std::string label;
    // Set when `bounds` was clipped to a panned/scrolled region: the
    // translated, unclipped rect plus which lane moves it — 1 guided list,
    // 2 tree graph — so keyboard focus can snap the row into view.
    std::optional<stellar::native_map::UiRect> unclipped;
    int scroll_lane{0};
  };
  [[nodiscard]] std::vector<FocusRect>
  focusables(const ResearchWorkspaceLayout&)const;
  stellar::core::AdaptiveResearchPlan plan_;
  ResearchViewMode mode_{ResearchViewMode::Guided};
  int filter_{},sort_{};bool list_view_{},why_open_{};
  stellar::engine::ScrollView guided_scroll_{},active_scroll_{};
  std::vector<InterfaceHit> interface_hits_;
  stellar::native_ui::Dropdown dropdown_;
  std::optional<stellar::native_map::UiRect> inspector_content_clip_;

  void reconcile_selection();
  void select(std::string node_id);
  [[nodiscard]] std::string tr(std::string_view key,
                               std::string_view fallback) const;
  [[nodiscard]] std::string
  trf(std::string_view key, std::initializer_list<std::string> args,
      std::string_view fallback) const;
  [[nodiscard]] const stellar::native_research::NativeResearchNode *
  selected_node() const noexcept;
  [[nodiscard]] stellar::native_map::UiRect
  transformed_card(const NodePlacement &placement,
                   const ResearchWorkspaceLayout &layout) const;

  const stellar::engine::LocalizationTable *locale_{};
  bool visible_{};
  bool search_focused_{};
  bool dragging_{};
  bool refresh_requested_{};
  bool center_selection_{};
  stellar::native_map::Point pointer_{};
  stellar::native_map::Point pan_{24.f, 30.f};
  float zoom_{1.f};
  stellar::native_research::NativeResearchQuery query_;
  std::optional<stellar::native_research::NativeResearchWindow> window_;
  std::optional<std::string> selected_node_id_;
  std::vector<NodePlacement> placements_;
  std::vector<DomainPlacement> domains_;
  std::string topology_signature_;
  std::string notice_;
  bool notice_accepted_{};
  stellar::engine::ScrollView inspector_scroll_{};
  int focus_{-1};
  int inspector_viewport_width_{}, inspector_viewport_height_{};
  TextMeasurer text_measurer_;
  ArtworkResolver artwork_resolver_;
};

} // namespace stellar::native_research_ui
