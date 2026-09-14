#include "native_research_workspace.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

using namespace stellar::native_map;
using namespace stellar::native_research;
using namespace stellar::native_research_ui;

namespace {

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

[[nodiscard]] Point center(UiRect value) noexcept {
  return {value.x + value.width * .5f, value.y + value.height * .5f};
}

[[nodiscard]] bool contained(UiRect outer, UiRect inner) noexcept {
  return inner.x >= outer.x && inner.y >= outer.y &&
         inner.x + inner.width <= outer.x + outer.width &&
         inner.y + inner.height <= outer.y + outer.height;
}

[[nodiscard]] bool contained(UiRect outer, Point point) noexcept {
  return point.x >= outer.x && point.y >= outer.y &&
         point.x <= outer.x + outer.width &&
         point.y <= outer.y + outer.height;
}

[[nodiscard]] bool overlaps(UiRect left, UiRect right) noexcept {
  return left.x < right.x + right.width && left.x + left.width > right.x &&
         left.y < right.y + right.height && left.y + left.height > right.y;
}

[[nodiscard]] NativeResearchWindow sample_window() {
  NativeResearchWindow window;
  window.campaign_generation = 7;
  window.research_revision = 19;
  window.free_effective_labs = 12;
  window.total_effective_labs = 20;
  window.domain_tabs = {{"", "All Research", 2},
                        {"physics", "Physics", 2}};
  NativeResearchNode active;
  active.id = "known-active";
  active.display_name = "Known Active Program";
  active.domain_id = "physics";
  active.domain_label = "Physics";
  active.solution_family = "known family";
  active.maturity = stellar::core::ResearchMaturity::experimental;
  active.graph_depth = 0;
  active.active = true;
  active.total_progress = .42;
  active.stage_progress = .61;
  active.assigned_effective_labs = 8;
  active.readiness_band = "stable";
  active.known_capabilities.push_back(
      {"internal-capability-id", "Visible Capability"});
  active.known_capabilities.push_back({"internal-two", "Second Capability"});
  active.known_capabilities.push_back({"internal-three", "Third Capability"});
  active.blockers = {"Visible facility readiness is incomplete.",
                     "A second visible requirement is pending."};
  active.cost = NativeResearchCost{8, 120, 240, 3, 510, 1.75, 363};
  active.cost->operating_credits_per_day = .020533881;
  active.cost->credits_needed_to_start = 3.041;
  active.primary_action = {NativeResearchIntent::Pause, true, {}};
  active.cancel_action = {NativeResearchIntent::Cancel, false,
                          "Cancellation is unavailable."};

  NativeResearchNode available;
  available.id = "known-candidate";
  available.display_name = "Known Candidate";
  available.domain_id = "physics";
  available.domain_label = "Physics";
  available.solution_family = "public purpose";
  available.maturity = stellar::core::ResearchMaturity::investigable;
  available.graph_depth = 1;
  available.cost = NativeResearchCost{
      4, 90, 110, 2, 320, std::numeric_limits<double>::infinity(), 202};
  available.primary_action = {NativeResearchIntent::Start, true, {}};
  window.nodes = {std::move(active), std::move(available)};
  window.edges = {{"known-active", "known-candidate", "known_prerequisite"}};
  return window;
}

void verify_layout(int width, int height) {
  const auto layout = ResearchWorkspaceLayout::for_viewport(width, height, 18);
  const UiRect viewport{0, 0, static_cast<float>(width),
                       static_cast<float>(height)};
  require(contained(viewport, layout.surface) &&
              contained(viewport, layout.title) &&
              contained(viewport, layout.search) &&
              contained(viewport, layout.close) &&
              contained(viewport, layout.graph) &&
              contained(viewport, layout.inspector) &&
              contained(layout.inspector, layout.feedback) &&
              contained(layout.inspector, layout.action),
          "Research layout escaped the viewport.");
  require(!overlaps(layout.search, layout.close) &&
              !overlaps(layout.graph, layout.inspector) &&
              !overlaps(layout.feedback, layout.action),
          "Research workspace regions overlap.");
  for (const auto &tab : layout.tabs)
    require(contained(viewport, tab.bounds), "Research tab escaped viewport.");
}

void send(NativeResearchWorkspace &workspace, InputEventType type, Point point,
          int width = 1280, int height = 720, Point delta = {},
          float wheel = 0, std::string text = {}) {
  InputEvent event;
  event.type = type;
  event.position = point;
  event.delta = delta;
  event.wheel_y = wheel;
  event.text = std::move(text);
  (void)workspace.handle(event, width, height);
}

[[nodiscard]] bool rendered_text_contains(const DrawList &draw,
                                          std::string_view value) {
  for (const auto &label : draw.text)
    if (label.value.contains(value)) return true;
  for (const auto &command : draw.overlay) {
    if (const auto *label = std::get_if<Text>(&command);
        label && label->value.contains(value))
      return true;
  }
  return false;
}

[[nodiscard]] const Text *find_overlay_text(const DrawList &draw,
                                            std::string_view value) {
  for (const auto &command : draw.overlay) {
    if (const auto *label = std::get_if<Text>(&command);
        label && label->value.contains(value))
      return label;
  }
  return nullptr;
}

void pan_graph(NativeResearchWorkspace &workspace, UiRect graph, Point delta) {
  const Point blank{graph.x + 5.f, graph.y + graph.height - 5.f};
  send(workspace, InputEventType::LeftPressed, blank);
  send(workspace, InputEventType::PointerMove,
       {blank.x + delta.x, blank.y + delta.y}, 1280, 720, delta);
  send(workspace, InputEventType::LeftReleased,
       {blank.x + delta.x, blank.y + delta.y});
}

} // namespace

int main() try {
  for (const auto [width, height] :
       std::array{std::pair{1280, 720}, std::pair{1920, 1080},
                  std::pair{2560, 1440}, std::pair{3840, 2160}})
    verify_layout(width, height);

  {
    auto real_tabs = sample_window();
    real_tabs.domain_tabs = {
        {"", "All Research", 82},
        {"alternative", "Alternative Biochemistry", 1},
        {"biosphere", "Biosphere Agriculture", 3},
        {"biotechnology", "Biotechnology", 1},
        {"computing", "Computing", 7},
        {"cybernetics", "Cybernetics", 2},
        {"trade", "Economic Trade", 1},
        {"energy", "Energy", 6},
        {"foundations", "Foundations", 9},
        {"medicine", "Life Medicine", 8},
        {"logistics", "Logistics", 3},
        {"materials", "Materials", 8},
        {"military", "Military", 5},
        {"planetary", "Planetary", 5},
        {"propulsion", "Propulsion", 2},
        {"infrastructure", "Research Infrastructure", 5},
        {"sensors", "Sensors Comms", 8},
        {"social", "Social Admin", 2}};
    NativeResearchWorkspace tabs_workspace;
    tabs_workspace.open();
    tabs_workspace.set_window(std::move(real_tabs));
    DrawList tabs_draw;
    tabs_workspace.render(tabs_draw, 1280, 720);
    const auto tabs_layout =
        ResearchWorkspaceLayout::for_viewport(1280, 720, 18);
    for (const auto label : {std::string_view{"Alternative Biochemistry 1"},
                             std::string_view{"Research Infrastructure 5"}}) {
      const auto *drawn = find_overlay_text(tabs_draw, label);
      require(drawn && drawn->clip,
              "A long real research tab label was not rendered with a clip.");
      const auto tab = std::ranges::find_if(
          tabs_layout.tabs,
          [&](const auto &candidate) { return candidate.bounds.contains(drawn->at); });
      require(tab != tabs_layout.tabs.end() && contained(tab->bounds, *drawn->clip) &&
                  drawn->clip->height >= 24.f,
              "A long real research tab label escaped or lacked two-line height.");
    }
  }

  for (const auto edge : {std::string_view{"top"}, std::string_view{"right"},
                          std::string_view{"bottom"}}) {
    NativeResearchWorkspace clipped_workspace;
    clipped_workspace.open();
    clipped_workspace.set_window(sample_window());
    const auto clipped_layout =
        ResearchWorkspaceLayout::for_viewport(1280, 720, 2);
    const auto original =
        clipped_workspace.card_bounds("known-active", 1280, 720);
    require(original.has_value(), "Partial-card test could not find its card.");
    Point delta;
    if (edge == "top")
      delta.y = clipped_layout.graph.y - original->y - 12.f;
    else if (edge == "right")
      delta.x = clipped_layout.graph.x + clipped_layout.graph.width -
                original->x - original->width + 12.f;
    else
      delta.y = clipped_layout.graph.y + clipped_layout.graph.height -
                original->y - original->height + 12.f;
    pan_graph(clipped_workspace, clipped_layout.graph, delta);
    const auto moved =
        clipped_workspace.card_bounds("known-active", 1280, 720);
    DrawList clipped_draw;
    clipped_workspace.render(clipped_draw, 1280, 720);
    const auto *title = find_overlay_text(clipped_draw, "Known Active Program");
    require(moved && title && title->clip &&
                std::abs(title->at.x - (moved->x + 9.f)) < .01f &&
                std::abs(title->at.y - (moved->y + 8.f)) < .01f &&
                contained(clipped_layout.graph, *title->clip),
            "A partially clipped card reflowed text or escaped the graph.");
    for (const auto &command : clipped_draw.overlay) {
      if (const auto *line = std::get_if<Line>(&command))
        require(contained(clipped_layout.graph, line->from) &&
                    contained(clipped_layout.graph, line->to),
                "A clipped card edge escaped the graph.");
    }
  }

  NativeResearchWorkspace workspace;
  workspace.open();
  require(workspace.take_refresh_request(), "Opening did not request a view.");
  workspace.set_window(sample_window());
  const auto layout = ResearchWorkspaceLayout::for_viewport(1280, 720, 2);

  auto search_press = workspace.handle(
      {InputEventType::LeftPressed, center(layout.search)}, 1280, 720);
  require(search_press.captured && workspace.wants_text_input(),
          "Search click did not capture and focus text input.");
  send(workspace, InputEventType::TextEntered, center(layout.search), 1280, 720,
       {}, 0, "warp");
  require(workspace.query().search == "warp" &&
              !workspace.take_refresh_request(),
          "Text search rebuilt the Core projection instead of local visibility.");
  DrawList filtered;
  workspace.render(filtered, 1280, 720);
  require(!rendered_text_contains(filtered, "Known Active Program"),
          "Search did not hide a nonmatching known node locally.");
  send(workspace, InputEventType::TextEntered, center(layout.search), 1280, 720,
       {}, 0, "\xc3\xa9");
  send(workspace, InputEventType::BackspacePressed, center(layout.search));
  require(workspace.query().search == "warp",
          "Backspace split a UTF-8 search code point.");
  send(workspace, InputEventType::BackspacePressed, center(layout.search));
  require(workspace.query().search == "war",
          "Backspace did not remove the final search character.");
  send(workspace, InputEventType::BackspacePressed, center(layout.search));
  send(workspace, InputEventType::BackspacePressed, center(layout.search));
  send(workspace, InputEventType::BackspacePressed, center(layout.search));
  require(workspace.query().search.empty(), "Search could not be cleared.");

  workspace.set_window(sample_window());
  const auto card = workspace.card_bounds("known-active", 1280, 720);
  require(card.has_value() && contained(layout.graph, *card),
          "Known card was not visible in the graph.");
  const auto select_command = workspace.handle(
      {InputEventType::LeftPressed, center(*card)}, 1280, 720);
  require(select_command.captured &&
              select_command.kind == WorkspaceCommandKind::Select &&
              select_command.node_id == "known-active",
          "Mouse node selection did not emit its opaque ID.");
  const auto action_command = workspace.handle(
      {InputEventType::LeftPressed, center(layout.action)}, 1280, 720);
  require(action_command.captured &&
              action_command.kind == WorkspaceCommandKind::Execute &&
              action_command.intent == NativeResearchIntent::Pause,
          "Mouse action did not route the canonical primary intent.");

  send(workspace, InputEventType::LeftPressed, center(layout.search));
  send(workspace, InputEventType::TextEntered, center(layout.search), 1280, 720,
       {}, 0, "candidate");
  auto stale_selection = sample_window();
  stale_selection.selected_node_id = "known-active";
  workspace.set_window(std::move(stale_selection));
  require(workspace.selected_id() == std::optional<std::string>{"known-candidate"},
          "A filtered refresh restored a hidden controller selection.");
  const auto filtered_action = workspace.handle(
      {InputEventType::LeftPressed, center(layout.action)}, 1280, 720);
  require(filtered_action.kind == WorkspaceCommandKind::Execute &&
              filtered_action.node_id == "known-candidate" &&
              filtered_action.intent == NativeResearchIntent::Start,
          "Filtered action targeted the hidden prior selection.");
  DrawList candidate_render;
  workspace.render(candidate_render, 1280, 720);
  require(rendered_text_contains(candidate_render, "Planned staffing 4.0") &&
              rendered_text_contains(candidate_render,
                                     "Staffed duration unavailable") &&
              !rendered_text_contains(candidate_render,
                                      "Known Active Program"),
          "Filtered inspector exposed a hidden node or wrong planned cost.");
  send(workspace,InputEventType::LeftPressed,center(layout.search));
  for (std::size_t index = 0; index < std::string_view{"candidate"}.size(); ++index)
    send(workspace, InputEventType::BackspacePressed, center(layout.search));
  workspace.set_window(sample_window());

  send(workspace, InputEventType::TextEntered, center(layout.search), 1280, 720,
       {}, 0, "nothing-visible");
  DrawList empty_filter;
  workspace.render(empty_filter, 1280, 720);
  require(rendered_text_contains(empty_filter,
                                 "No known research matches this view."),
          "Empty local filter rendered a blank graph without guidance.");
  for (std::size_t index = 0;
       index < std::string_view{"nothing-visible"}.size(); ++index)
    send(workspace, InputEventType::BackspacePressed, center(layout.search));
  workspace.set_window(sample_window());

  const Point graph_blank{layout.graph.x + 12, layout.graph.y +
                                                   layout.graph.height - 12};
  send(workspace, InputEventType::LeftPressed, graph_blank);
  const auto before_drag = workspace.card_bounds("known-active", 1280, 720);
  send(workspace, InputEventType::PointerMove,
       {graph_blank.x - 100, graph_blank.y - 20}, 1280, 720, {-100, -20});
  send(workspace, InputEventType::LeftReleased,
       {graph_blank.x - 100, graph_blank.y - 20});
  const auto after_drag = workspace.card_bounds("known-active", 1280, 720);
  require(before_drag && after_drag &&
              std::abs((after_drag->x - before_drag->x) + 100.f) < .01f &&
              std::abs((after_drag->y - before_drag->y) + 20.f) < .01f,
          "Graph drag was not captured in drawable coordinates.");
  require(after_drag->x < layout.graph.x &&
              after_drag->x + after_drag->width > layout.graph.x,
          "Edge-pan setup did not leave a partially visible card.");
  const Point clipped_hit{layout.graph.x + 2.f,
                          after_drag->y + after_drag->height * .5f};
  const auto clipped_select = workspace.handle(
      {InputEventType::LeftPressed, clipped_hit}, 1280, 720);
  require(clipped_select.kind == WorkspaceCommandKind::Select &&
              clipped_select.node_id == "known-active",
          "Visible clipped portion of a card was not clickable.");
  send(workspace, InputEventType::Wheel, center(layout.graph), 1280, 720, {},
       1);
  const auto after_scroll = workspace.card_bounds("known-active", 1280, 720);
  require(after_scroll && after_drag && after_scroll->y > after_drag->y,
          "Graph wheel scrolling did not move the branch view.");

  workspace.set_notice(
      "Program started with a deliberately long visible status message that "
      "must remain above the action without covering it.",true);
  DrawList rendered;
  workspace.render(rendered, 1280, 720);
  require(rendered_text_contains(rendered, "Known Active Program") &&
              rendered_text_contains(rendered, "Visible Capability") &&
              rendered_text_contains(rendered, "Authorization 120.0") &&
              rendered_text_contains(rendered, "Operations 0.0205 cr/day") &&
              rendered_text_contains(rendered, "Reserve to start 3.05 cr") &&
              rendered_text_contains(rendered, "Progress 42%"),
          "Research inspector omitted authoritative visible details.");
  require(!rendered_text_contains(rendered, "internal-capability-id") &&
              !rendered_text_contains(rendered, "SECRET FUTURE") &&
              !rendered_text_contains(rendered, "CANCEL"),
          "Research UI disclosed an internal, unknown, or unsupported action.");
  const auto *cost=find_overlay_text(rendered,"COST & TIME");
  const auto *capabilities=find_overlay_text(rendered,"KNOWN CAPABILITIES");
  const auto *blockers=find_overlay_text(rendered,"REQUIREMENTS / STATUS");
  const auto *notice=find_overlay_text(rendered,"Program started with");
  require(cost&&capabilities&&blockers&&notice&&cost->clip&&capabilities->clip&&
              blockers->clip&&notice->clip,
          "Maximal 720p inspector content did not receive bounded clips.");
  require(!overlaps(*cost->clip,layout.feedback)&&
              !overlaps(*capabilities->clip,layout.feedback)&&
              !overlaps(*blockers->clip,layout.feedback)&&
              contained(layout.feedback,*notice->clip)&&
              !overlaps(layout.feedback,layout.action),
          "Inspector details, feedback and action bands overlap at 720p.");
  workspace.discard_campaign();
  auto replacement=sample_window();
  replacement.campaign_generation=8;
  workspace.set_window(std::move(replacement));
  DrawList replaced;
  workspace.render(replaced,1280,720);
  require(!rendered_text_contains(replaced,"Program started with"),
          "Old campaign notice survived generation replacement.");

  std::cout << "Native research layout, mouse routing, UTF-8 search, graph "
               "capture, costs and secrecy tests passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
