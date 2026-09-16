#pragma once

#include "native_research_controller.hpp"
#include <stellar/engine/native_map_platform.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::native_research_ui {

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

  [[nodiscard]] static ResearchWorkspaceLayout
  for_viewport(int width, int height, std::size_t tab_count);
};

class NativeResearchWorkspace final {
public:
  using TextMeasurer = std::function<stellar::native_map::TextExtent(
      const stellar::native_map::Text &)>;
  void set_text_measurer(TextMeasurer measure);
  void open();
  void close();
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
  void reconcile_selection();
  void select(std::string node_id);
  [[nodiscard]] const stellar::native_research::NativeResearchNode *
  selected_node() const noexcept;
  [[nodiscard]] stellar::native_map::UiRect
  transformed_card(const NodePlacement &placement,
                   const ResearchWorkspaceLayout &layout) const;

  bool visible_{};
  bool search_focused_{};
  bool dragging_{};
  bool refresh_requested_{};
  stellar::native_map::Point pointer_{};
  stellar::native_map::Point pan_{24.f, 30.f};
  stellar::native_research::NativeResearchQuery query_;
  std::optional<stellar::native_research::NativeResearchWindow> window_;
  std::optional<std::string> selected_node_id_;
  std::vector<NodePlacement> placements_;
  std::vector<DomainPlacement> domains_;
  std::string topology_signature_;
  std::string notice_;
  bool notice_accepted_{};
  float inspector_scroll_{};
  float inspector_scroll_limit_{};
  int inspector_viewport_width_{}, inspector_viewport_height_{};
  TextMeasurer text_measurer_;
};

} // namespace stellar::native_research_ui
