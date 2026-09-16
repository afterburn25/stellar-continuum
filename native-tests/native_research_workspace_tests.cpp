#include "native_research_workspace.hpp"
#include "native_ui_layout.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

using namespace stellar::native_map;
using namespace stellar::native_research;
using namespace stellar::native_research_ui;

namespace {

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
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
         point.x <= outer.x + outer.width && point.y <= outer.y + outer.height;
}

[[nodiscard]] bool overlaps(UiRect left, UiRect right) noexcept {
  return left.x < right.x + right.width && left.x + left.width > right.x &&
         left.y < right.y + right.height && left.y + left.height > right.y;
}

[[nodiscard]] NativeResearchWindow sample_window() {
  NativeResearchWindow window;
  window.campaign_generation = 7;
  window.research_revision = 19;
  window.funding_revision = 3;
  window.currency = {"United Earth Dollar", "UED", "$", 10'000'000.};
  window.treasury_credits = 500.;
  window.formatted_treasury = "$5B UED";
  window.free_effective_labs = 12;
  window.total_effective_labs = 20;
  window.domain_tabs = {{"", "All Research", 2}, {"physics", "Physics", 2}};
  NativeResearchNode active;
  active.id = "known-active";
  active.display_name = "Known Active Program";
  active.domain_id = "physics";
  active.domain_label = "Physics";
  active.solution_family = "known family";
  active.purpose = "Tests a visible, practical research method for the current program.";
  active.benefits = "Confirms a visible capability for follow-on programs.";
  active.research_points = 12500.;
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
  active.cost->operating_credits_per_day = 1e-15;
  active.cost->credits_needed_to_start = 3.041;
  active.cost->formatted_authorization = "$1.2B UED";
  active.cost->formatted_milestone_commitment = "$2.4B UED";
  active.cost->formatted_operating_cost_rate = "Under $0.01 UED/day";
  active.cost->formatted_estimated_total = "$5.1B UED";
  active.cost->formatted_credits_needed_to_start = "$30.41M UED";
  active.primary_action = {NativeResearchIntent::Pause, true, {}};
  active.cancel_action = {NativeResearchIntent::Cancel, false,
                          "Cancellation is unavailable."};

  NativeResearchNode available;
  available.id = "known-candidate";
  available.display_name = "Known Candidate";
  available.domain_id = "physics";
  available.domain_label = "Physics";
  available.solution_family = "public purpose";
  available.purpose = "Develops a visible candidate program.";
  available.benefits = "Provides a visible foundation for later work.";
  available.research_points = 9000.;
  available.maturity = stellar::core::ResearchMaturity::investigable;
  available.graph_depth = 1;
  available.cost = NativeResearchCost{
      4, 90, 110, 2, 320, std::numeric_limits<double>::infinity(), 202};
  available.cost->formatted_authorization = "$900M UED";
  available.cost->formatted_milestone_commitment = "$1.1B UED";
  available.cost->formatted_operating_cost_rate = "−$20M UED/day";
  available.cost->formatted_estimated_total = "$3.2B UED";
  available.cost->formatted_credits_needed_to_start = "$2.02B UED";
  available.primary_action = {NativeResearchIntent::Start, true, {}};
  window.nodes = {std::move(active), std::move(available)};
  window.edges = {{"known-active", "known-candidate", "known_prerequisite"}};
  return window;
}

void verify_layout(int width, int height) {
  const auto layout = ResearchWorkspaceLayout::for_viewport(width, height, 18);
  const UiRect viewport{0, 0, static_cast<float>(width),
                        static_cast<float>(height)};
  const auto navigation = NativeUiLayout::for_viewport(width, height);
  require(contained(viewport, layout.surface) &&
              contained(viewport, layout.title) &&
              contained(viewport, layout.search) &&
              contained(viewport, layout.close) &&
              contained(viewport, layout.graph) &&
              contained(viewport, layout.inspector) &&
              contained(layout.inspector, layout.feedback) &&
              contained(layout.inspector, layout.action),
          "Research layout escaped the viewport.");
  require(layout.graph.x >= navigation.research.x + navigation.research.width &&
              layout.title.x >= navigation.research.x + navigation.research.width,
          "Research content overlaps the navigation rail.");
  require(!overlaps(layout.search, layout.close) &&
              !overlaps(layout.graph, layout.inspector) &&
              !overlaps(layout.feedback, layout.action),
          "Research workspace regions overlap.");
  for (const auto &tab : layout.tabs)
    require(contained(viewport, tab.bounds), "Research tab escaped viewport.");
}

void send(NativeResearchWorkspace &workspace, InputEventType type, Point point,
          int width = 1280, int height = 720, Point delta = {}, float wheel = 0,
          std::string text = {}) {
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
    if (label.value.contains(value))
      return true;
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

[[nodiscard]] TextExtent measured_text(const Text &text) {
  const auto character_width = std::max(1.f, text.font_pixel_size * .56f);
  const auto columns = std::max(
      1, static_cast<int>(std::floor(text.wrap_width / character_width)));
  int lines = 1;
  std::size_t line_length{};
  for (const char character : text.value) {
    if (character == '\n') {
      ++lines;
      line_length = 0;
    } else if (++line_length > static_cast<std::size_t>(columns)) {
      ++lines;
      line_length = 1;
    }
  }
  return {static_cast<int>(text.wrap_width),
          lines * (text.font_pixel_size + 3)};
}

} // namespace

int main() try {
  for (const auto [width, height] :
       std::array{std::pair{1280, 720}, std::pair{1920, 1080},
                  std::pair{2560, 1440}, std::pair{3840, 2160}})
    verify_layout(width, height);

  {
    auto real_tabs = sample_window();
    real_tabs.domain_tabs = {{"", "All Research", 82},
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
      const auto tab =
          std::ranges::find_if(tabs_layout.tabs, [&](const auto &candidate) {
            return candidate.bounds.contains(drawn->at);
          });
      require(
          tab != tabs_layout.tabs.end() &&
              contained(tab->bounds, *drawn->clip) &&
              drawn->clip->height >= 24.f,
          "A long real research tab label escaped or lacked two-line height.");
    }
  }

  {
    auto category_window = sample_window();
    category_window.domain_tabs = {{"", "All Research", 2},
                                   {"physics", "Physics", 1},
                                   {"engineering", "Engineering", 1}};
    category_window.nodes[0].domain_id = "foundations";
    category_window.nodes[0].domain_label = "Foundations";
    category_window.nodes[1].domain_id = "propulsion";
    category_window.nodes[1].domain_label = "Propulsion";
    NativeResearchWorkspace category_workspace;
    category_workspace.open();
    category_workspace.set_window(std::move(category_window));
    const auto category_layout = ResearchWorkspaceLayout::for_viewport(1280, 720, 3);
    (void)category_workspace.handle(
        {InputEventType::LeftPressed, center(category_layout.tabs[1].bounds)}, 1280, 720);
    require(category_workspace.query().domain_id == std::optional<std::string>{"physics"} &&
                category_workspace.card_bounds("known-active", 1280, 720) &&
                !category_workspace.card_bounds("known-candidate", 1280, 720),
            "Presentation category filtering did not include its mapped known domain.");
  }

  for (const auto edge : {std::string_view{"top"}, std::string_view{"right"},
                          std::string_view{"bottom"}}) {
    NativeResearchWorkspace clipped_workspace;
    clipped_workspace.open();
    clipped_workspace.set_window(sample_window());
    // First render applies the one-time selected-card focus.  Compute drag
    // geometry from that settled camera rather than the pre-focus default.
    DrawList focused_draw;
    clipped_workspace.render(focused_draw, 1280, 720);
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
    const auto moved = clipped_workspace.card_bounds("known-active", 1280, 720);
    DrawList clipped_draw;
    clipped_workspace.render(clipped_draw, 1280, 720);
    const auto *title = find_overlay_text(clipped_draw, "Known Active Program");
    require(moved && title && title->clip &&
                title->at.x > moved->x && title->at.y > moved->y &&
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
  std::vector<std::string> measured_inspector_blocks;
  workspace.set_text_measurer([&](const Text &label) {
    measured_inspector_blocks.push_back(label.value);
    return measured_text(label);
  });
  require(workspace.take_refresh_request(), "Opening did not request a view.");
  workspace.set_window(sample_window());
  const auto layout = ResearchWorkspaceLayout::for_viewport(1280, 720, 2);

  // Artwork is requested only after the controller's known-node projection;
  // a resolver must never receive a catalog or planned-program identity.
  const auto artwork = RgbaImage::create(1, 1, {255, 255, 255, 255});
  std::vector<std::pair<std::string, bool>> artwork_requests;
  workspace.set_artwork_resolver([&](std::string_view id, bool portrait) {
    require(id == "known-active" || id == "known-candidate",
            "Artwork resolution received an unknown research identity.");
    artwork_requests.emplace_back(std::string(id), portrait);
    return artwork;
  });
  DrawList artwork_draw;
  workspace.render(artwork_draw, 1280, 720);
  require(std::ranges::any_of(artwork_requests, [](const auto &request) {
            return !request.second;
          }) &&
              std::ranges::any_of(artwork_requests, [](const auto &request) {
                return request.second;
              }),
          "Known research cards and selected inspector did not request artwork.");
  const auto artwork_images = std::ranges::count_if(artwork_draw.overlay,
      [](const auto &command) { return std::holds_alternative<Image>(command); });
  require(artwork_images >= 3,
          "Known research artwork was not clipped into card and portrait regions.");

  auto search_press = workspace.handle(
      {InputEventType::LeftPressed, center(layout.search)}, 1280, 720);
  require(search_press.captured && workspace.wants_text_input(),
          "Search click did not capture and focus text input.");
  send(workspace, InputEventType::TextEntered, center(layout.search), 1280, 720,
       {}, 0, "warp");
  require(
      workspace.query().search == "warp" && !workspace.take_refresh_request(),
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
  const auto select_command =
      workspace.handle({InputEventType::LeftPressed, center(*card)}, 1280, 720);
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
  require(workspace.selected_id() ==
              std::optional<std::string>{"known-candidate"},
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
              !rendered_text_contains(candidate_render, "Known Active Program"),
          "Filtered inspector exposed a hidden node or wrong planned cost.");
  send(workspace, InputEventType::LeftPressed, center(layout.search));
  for (std::size_t index = 0; index < std::string_view{"candidate"}.size();
       ++index)
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

  const Point graph_blank{layout.graph.x + 12,
                          layout.graph.y + layout.graph.height - 12};
  send(workspace, InputEventType::LeftPressed, graph_blank);
  const auto before_drag = workspace.card_bounds("known-active", 1280, 720);
  require(before_drag.has_value(), "Edge-pan setup could not find the selected card.");
  const Point edge_delta{layout.graph.x - before_drag->x -
                             before_drag->width + 12.f,
                         0.f};
  send(workspace, InputEventType::PointerMove,
       {graph_blank.x + edge_delta.x, graph_blank.y}, 1280, 720, edge_delta);
  send(workspace, InputEventType::LeftReleased,
       {graph_blank.x + edge_delta.x, graph_blank.y});
  const auto after_drag = workspace.card_bounds("known-active", 1280, 720);
  require(before_drag && after_drag &&
              std::abs((after_drag->x - before_drag->x) - edge_delta.x) < .01f &&
              std::abs((after_drag->y - before_drag->y) - edge_delta.y) < .01f,
          "Graph drag was not captured in drawable coordinates.");
  require(after_drag->x < layout.graph.x &&
              after_drag->x + after_drag->width > layout.graph.x,
          "Edge-pan setup did not leave a partially visible card.");
  const Point clipped_hit{layout.graph.x + 2.f,
                          after_drag->y + after_drag->height * .5f};
  const auto clipped_select =
      workspace.handle({InputEventType::LeftPressed, clipped_hit}, 1280, 720);
  require(clipped_select.kind == WorkspaceCommandKind::Select &&
              clipped_select.node_id == "known-active",
          "Visible clipped portion of a card was not clickable.");
  const Point zoom_anchor{layout.graph.x + layout.graph.width * .63f,
                          layout.graph.y + layout.graph.height * .37f};
  const auto world_x_before = (after_drag->x - zoom_anchor.x) / after_drag->width;
  const auto world_y_before = (after_drag->y - zoom_anchor.y) / after_drag->height;
  send(workspace, InputEventType::Wheel, zoom_anchor, 1280, 720, {}, 1);
  const auto after_scroll = workspace.card_bounds("known-active", 1280, 720);
  require(after_scroll && after_drag && after_scroll->width > after_drag->width &&
              std::abs((after_scroll->x - zoom_anchor.x) / after_scroll->width - world_x_before) < .001f &&
              std::abs((after_scroll->y - zoom_anchor.y) / after_scroll->height - world_y_before) < .001f,
          "Graph zoom did not preserve the pointer's world anchor.");

  workspace.set_notice(
      "Program started with a deliberately long visible status message that "
      "must remain above the action without covering it. Continued operational "
      "review confirms that the authoritative program state remains "
      "available.\n"
      "LAST NOTICE LINE",
      true);
  measured_inspector_blocks.clear();
  DrawList rendered;
  workspace.render(rendered, 1280, 720);
  require(
      rendered_text_contains(rendered, "Known Active Program") &&
          rendered_text_contains(rendered, "Visible Capability") &&
          rendered_text_contains(rendered, "$5B UED treasury") &&
          rendered_text_contains(rendered, "Authorization $1.2B UED") &&
          rendered_text_contains(rendered, "Operations Under $0.01 UED/day") &&
          rendered_text_contains(rendered, "Reserve to start $30.41M UED") &&
          rendered_text_contains(rendered, "Progress 42%"),
      "Research inspector omitted authoritative visible details.");
  require(std::ranges::any_of(measured_inspector_blocks, [](const auto &block) { return block.contains("WHAT IT DOES"); }) &&
              std::ranges::any_of(measured_inspector_blocks, [](const auto &block) { return block.contains("BENEFITS"); }) &&
              std::ranges::any_of(measured_inspector_blocks, [](const auto &block) { return block.contains("COST & TIME"); }) &&
              std::ranges::any_of(measured_inspector_blocks, [](const auto &block) { return block.contains("PROGRAM NOTICE"); }),
          "Inspector sections were not independently font-measured.");
  require(!rendered_text_contains(rendered, "internal-capability-id") &&
              !rendered_text_contains(rendered, "SECRET FUTURE") &&
              !rendered_text_contains(rendered, " cr") &&
              !rendered_text_contains(rendered, "CANCEL"),
          "Research UI disclosed an internal, unknown, or unsupported action.");
  const auto *cost = find_overlay_text(rendered, "COST & TIME");
  const auto *capabilities = find_overlay_text(rendered, "KNOWN CAPABILITIES");
  require(cost && capabilities && cost->clip && capabilities->clip &&
              !overlaps(*cost->clip, layout.feedback) &&
              !overlaps(*capabilities->clip, layout.feedback),
          "Initial 720p inspector sections escaped their scroll viewport.");
  const auto graph_before_inspector_wheel =
      workspace.card_bounds("known-active", 1280, 720);
  send(workspace, InputEventType::Wheel, center(layout.inspector), 1280, 720,
       {}, -100.f);
  DrawList scrolled_to_end;
  workspace.render(scrolled_to_end, 1280, 720);
  const auto graph_after_inspector_wheel =
      workspace.card_bounds("known-active", 1280, 720);
  require(graph_before_inspector_wheel && graph_after_inspector_wheel &&
              graph_before_inspector_wheel->x ==
                  graph_after_inspector_wheel->x &&
              graph_before_inspector_wheel->y == graph_after_inspector_wheel->y,
          "Inspector wheel input panned the research graph.");
  const auto *blockers =
      find_overlay_text(scrolled_to_end, "REQUIREMENTS / STATUS");
  const auto *notice = find_overlay_text(scrolled_to_end, "LAST NOTICE LINE");
  require(
      blockers && notice && blockers->clip && notice->clip &&
          notice->value.contains("LAST NOTICE LINE") &&
          notice->at.y + measured_text(*notice).height <=
              notice->clip->y + notice->clip->height + .01f &&
          !overlaps(*notice->clip, layout.feedback) &&
          !overlaps(layout.feedback, layout.action),
      "Inspector overscroll did not expose the complete final notice line.");
  const auto notice_at_end = notice->at.y;
  send(workspace, InputEventType::Wheel, center(layout.inspector), 1280, 720,
       {}, 1.f);
  DrawList reverse_scrolled;
  workspace.render(reverse_scrolled, 1280, 720);
  const auto *reverse_notice =
      find_overlay_text(reverse_scrolled, "LAST NOTICE LINE");
  require(
      reverse_notice && reverse_notice->at.y > notice_at_end,
      "Reverse inspector wheel did not recover immediately from overscroll.");
  const auto pinned_action = workspace.handle(
      {InputEventType::LeftPressed, center(layout.action)}, 1280, 720);
  require(pinned_action.kind == WorkspaceCommandKind::Execute &&
              pinned_action.intent == NativeResearchIntent::Pause,
          "Scrolling displaced or blocked the pinned research action.");

  const auto candidate_card =
      workspace.card_bounds("known-candidate", 1280, 720);
  require(candidate_card && overlaps(layout.graph, *candidate_card),
          "Selection-reset test could not reach the candidate card.");
  const UiRect candidate_visible{
      std::max(layout.graph.x, candidate_card->x),
      std::max(layout.graph.y, candidate_card->y),
      std::min(layout.graph.x + layout.graph.width,
               candidate_card->x + candidate_card->width) -
          std::max(layout.graph.x, candidate_card->x),
      std::min(layout.graph.y + layout.graph.height,
               candidate_card->y + candidate_card->height) -
          std::max(layout.graph.y, candidate_card->y)};
  require(candidate_visible.width > 0.f && candidate_visible.height > 0.f,
          "Candidate card has no clickable visible intersection.");
  const auto candidate_select = workspace.handle(
      {InputEventType::LeftPressed, center(candidate_visible)}, 1280, 720);
  require(candidate_select.kind == WorkspaceCommandKind::Select &&
              candidate_select.node_id == "known-candidate",
          "Visible candidate card click did not select the candidate.");
  DrawList selection_reset;
  workspace.render(selection_reset, 1280, 720);
  const auto *initial_purpose = find_overlay_text(rendered, "WHAT IT DOES");
  const auto *candidate_purpose = find_overlay_text(selection_reset, "WHAT IT DOES");
  require(candidate_purpose && initial_purpose &&
              std::abs(candidate_purpose->at.y - initial_purpose->at.y) < .01f,
          "Selecting another program did not reset inspector scroll.");

  auto active_refresh = sample_window();
  active_refresh.selected_node_id = "known-active";
  workspace.set_window(std::move(active_refresh));
  DrawList active_top;
  workspace.render(active_top, 1280, 720);
  send(workspace, InputEventType::Wheel, center(layout.inspector), 1280, 720,
       {}, -100.f);
  DrawList active_end;
  workspace.render(active_end, 1280, 720);
  const auto *notice_before_refresh =
      find_overlay_text(active_end, "LAST NOTICE LINE");
  require(notice_before_refresh,
          "Routine-refresh test could not reach the final inspector detail.");
  const auto notice_before_refresh_y = notice_before_refresh->at.y;

  auto revised = sample_window();
  revised.selected_node_id = "known-active";
  revised.research_revision++;
  revised.nodes.front().total_progress = .47;
  revised.nodes.front().assigned_effective_labs = 7;
  workspace.set_window(std::move(revised));
  DrawList preserved_refresh;
  workspace.render(preserved_refresh, 1280, 720);
  const auto *notice_after_refresh =
      find_overlay_text(preserved_refresh, "LAST NOTICE LINE");
  require(notice_after_refresh &&
              std::abs(notice_after_refresh->at.y - notice_before_refresh_y) <
                  .01f &&
              notice_after_refresh->at.y +
                      measured_text(*notice_after_refresh).height <=
                  notice_after_refresh->clip->y +
                      notice_after_refresh->clip->height + .01f,
          "Routine research progress refresh snapped the inspector to top.");

  auto compact = sample_window();
  compact.selected_node_id = "known-active";
  compact.research_revision += 2;
  compact.nodes.front().cost.reset();
  compact.nodes.front().known_capabilities.clear();
  compact.nodes.front().blockers.clear();
  workspace.set_window(std::move(compact));
  DrawList compact_render;
  workspace.render(compact_render, 1280, 720);
  const auto *compact_notice =
      find_overlay_text(compact_render, "LAST NOTICE LINE");
  require(compact_notice && compact_notice->clip &&
              compact_notice->at.y + measured_text(*compact_notice).height <=
                  compact_notice->clip->y + compact_notice->clip->height + .01f,
          "Shrinking refreshed content retained an unreachable overscroll.");

  workspace.set_notice({}, false);
  auto shortened = sample_window();
  shortened.selected_node_id = "known-active";
  shortened.research_revision += 3;
  auto &short_node = shortened.nodes.front();
  short_node.cost.reset();
  short_node.known_capabilities.clear();
  short_node.blockers.clear();
  short_node.primary_action.enabled = false;
  short_node.primary_action.reason =
      "The selected program cannot proceed while its independently reported "
      "facility, staffing, treasury, and prerequisite conditions remain "
      "unavailable. Review the authoritative controller details before "
      "attempting this action again. The current observer projection also "
      "reports that laboratory allocation is below the required threshold, "
      "that the necessary prototype site has not been commissioned, and that "
      "the reserved operating funds are not yet available. These conditions "
      "are supplied by the controller and must remain readable without "
      "abbreviation. Continue reviewing every prerequisite before changing "
      "the selected program or committing treasury resources.\n"
      "LAST ACTION REASON LINE";
  workspace.set_window(std::move(shortened));
  DrawList shortened_first;
  workspace.render(shortened_first, 1280, 720);
  send(workspace, InputEventType::Wheel, center(layout.inspector), 1280, 720,
       {}, -100.f);
  DrawList shortened_end;
  workspace.render(shortened_end, 1280, 720);
  const auto *reason_end =
      find_overlay_text(shortened_end, "LAST ACTION REASON LINE");
  require(reason_end && reason_end->clip &&
              reason_end->at.y + measured_text(*reason_end).height <=
                  reason_end->clip->y + reason_end->clip->height + .01f &&
              rendered_text_contains(shortened_end,
                                     "Action unavailable. Scroll for details."),
          "Shrinking content did not clamp scroll or expose the full action "
          "reason.");

  auto expanded = sample_window();
  expanded.selected_node_id = "known-active";
  expanded.research_revision += 4;
  workspace.set_window(std::move(expanded));
  DrawList expanded_render;
  workspace.render(expanded_render, 1280, 720);

  send(workspace, InputEventType::Wheel, center(layout.inspector), 1280, 720,
       {}, -100.f);
  DrawList revised_end;
  workspace.render(revised_end, 1280, 720);
  DrawList resized;
  workspace.render(resized, 1920, 1080);
  const auto resized_layout =
      ResearchWorkspaceLayout::for_viewport(1920, 1080, 2);
  const auto *resized_purpose = find_overlay_text(resized, "WHAT IT DOES");
  const auto resized_portrait = std::clamp(68.f * resized_layout.scale, 68.f,
                                           96.f * resized_layout.scale);
  const auto resized_detail_top = resized_layout.inspector.y +
      14.f * resized_layout.scale + 24.f * resized_layout.scale +
      std::max(57.f * resized_layout.scale,
               resized_portrait + 6.f * resized_layout.scale) +
      27.f * resized_layout.scale + 17.f * resized_layout.scale +
      28.f * resized_layout.scale;
  require(resized_purpose &&
              std::abs(resized_purpose->at.y - resized_detail_top) < .01f,
          "Research viewport change did not reset inspector scroll.");

  send(workspace, InputEventType::Wheel, center(resized_layout.inspector), 1920,
       1080, {}, -100.f);
  DrawList resized_end;
  workspace.render(resized_end, 1920, 1080);
  auto generation = sample_window();
  generation.campaign_generation = 8;
  generation.research_revision += 2;
  workspace.set_window(std::move(generation));
  DrawList generation_reset;
  workspace.render(generation_reset, 1920, 1080);
  const auto *generation_purpose =
      find_overlay_text(generation_reset, "WHAT IT DOES");
  require(generation_purpose &&
              std::abs(generation_purpose->at.y - resized_detail_top) < .01f &&
              !rendered_text_contains(generation_reset, "LAST NOTICE LINE"),
          "Campaign generation change retained inspector scroll or notice.");
  workspace.discard_campaign();
  auto replacement = sample_window();
  replacement.campaign_generation = 8;
  workspace.set_window(std::move(replacement));
  DrawList replaced;
  workspace.render(replaced, 1280, 720);
  require(!rendered_text_contains(replaced, "Program started with"),
          "Old campaign notice survived generation replacement.");

  std::cout << "Native research layout, mouse routing, UTF-8 search, graph "
               "capture, costs and secrecy tests passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
