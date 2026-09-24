#include "native_colony_roster.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

namespace {
using namespace stellar::core;
using namespace stellar::native_colony_roster;
using namespace stellar::native_map;

void require(bool value, std::string_view message) {
  if (!value)
    throw std::runtime_error(std::string(message));
}

FreshCampaignState world(int rows = 2) {
  FreshCampaignState value;
  value.player_civilization_id = 7;
  value.civilizations = {{7, "Terra", 1}, {8, "Foreign", 2}};
  value.civilizations.front().is_player = true;
  value.systems = {{1, "Sol", {}}, {2, "Kesh", {}}};
  value.bodies = {{11, 1, {}, 0, "Earth"},
                  {12, 1, {}, 1, "Mars"},
                  {22, 2, {}, 0, "Foreign World"}};
  for (int i = 0; i < rows; ++i) {
    Colony colony;
    colony.id = i + 1;
    colony.civilization_id = 7;
    colony.system_id = 1;
    colony.planetary_body_id = 11 + (i % 2);
    colony.name = "Terra Colony " + std::to_string(i + 1);
    colony.population_millions = 1. + i;
    value.colonies.push_back(std::move(colony));
  }
  value.knowledge.mark_system_fully_surveyed(7, 1);
  return value;
}
Point center(UiRect r) { return {r.x + r.width * .5f, r.y + r.height * .5f}; }

void ownership_and_identity_are_sealed() {
  auto value = world();
  Colony foreign;
  foreign.id = 99;
  foreign.civilization_id = 8;
  foreign.system_id = 2;
  foreign.planetary_body_id = 22;
  foreign.name = "Foreign leak";
  foreign.population_millions = 999.;
  value.colonies.push_back(foreign);
  const auto view = build(value, 4);
  require(view.available && view.rows.size() == 2,
          "foreign colony entered owned roster");
  value.developer_provenance.emplace();
  const auto dev=build(value,4);
  const auto alien=std::ranges::find(dev.rows,99,&Row::colony_id);
  require(dev.developer_inspection&&dev.rows.size()==3&&alien!=dev.rows.end()&&alien->can_open&&alien->population=="999M",
          "Developer roster withheld alien colony or population");
  require(value.knowledge.system_survey_level(7,2)==SystemSurveyLevel::unknown,"Developer roster changed survey state");
  value.developer_provenance.reset();
  value.civilizations.push_back(value.civilizations.front());
  require(!build(value, 4).available, "duplicate player identity was accepted");
  value = world();
  value.colonies.push_back(value.colonies.front());
  require(!build(value, 4).available,
          "duplicate colony id was projected ambiguously");
  value = world();
  value.bodies.push_back(value.bodies.front());
  require(!build(value, 4).available,
          "duplicate body id was projected ambiguously");
  value = world();
  value.systems.push_back(value.systems.front());
  require(!build(value, 4).available,
          "duplicate system id was projected ambiguously");
}

void references_survey_and_numbers_are_bounded() {
  auto value = world();
  value.knowledge = CivilizationKnowledgeState{};
  auto view = build(value, 3);
  require(view.available && view.rows.front().body_name == "Unconfirmed" &&
              !view.rows.front().can_open,
          "unsurveyed world leaked location or opened");
  value = world();
  value.colonies.front().planetary_body_id.reset();
  view = build(value, 3);
  require(view.rows.front().reason.contains("no canonical"),
          "missing body reference lacked reason");
  value = world();
  value.colonies.front().population_millions =
      std::numeric_limits<double>::quiet_NaN();
  view = build(value, 3);
  require(view.rows.front().population == "Unconfirmed",
          "invalid population was formatted as a number");
  value = world();
  value.systems.front().id = -1;
  require(!build(value, 3).available, "negative canonical id was accepted");
}

void responsive_scroll_and_press_gates() {
  for (const auto [width, height] :
       {std::pair{1280, 720}, std::pair{1920, 1080}, std::pair{3840, 2160}}) {
    RosterWorkspace workspace;
    workspace.set_view(build(world(40), 9));
    workspace.open();
    const auto layout = RosterLayout::for_viewport(width, height);
    require(layout.panel.x >= 70.f * layout.scale && layout.list.height > 0 &&
                layout.row_height > 0,
            "roster layout escaped navigation viewport");
    const auto first = workspace.row_button(0, width, height);
    require(layout.list.contains(center(first)),
            "first roster row was not in scroll list");
    require(
        workspace
                .handle(
                    {InputEventType::Wheel, center(layout.list), {}, -100.f},
                    width, height)
                .captured &&
            workspace.scroll_offset() > 0,
        "wheel did not reach roster rows");
    workspace.open();
    const auto second = workspace.row_button(1, width, height);
    (void)workspace.handle({InputEventType::LeftPressed, center(second)}, width,
                           height);
    const auto third = workspace.row_button(2, width, height);
    require(!workspace
                 .handle({InputEventType::LeftReleased, center(third)}, width,
                         height)
                 .open_colony_id,
            "cross-row release opened colony");
    require(!workspace
                 .handle({InputEventType::LeftReleased, center(second)}, width,
                         height)
                 .open_colony_id,
            "unpaired release opened colony");
    (void)workspace.handle({InputEventType::LeftPressed, center(second)}, width,
                           height);
    const auto opened = workspace.handle(
        {InputEventType::LeftReleased, center(second)}, width, height);
    require(opened.open_colony_id && *opened.open_colony_id == 2,
            "matched row click did not open expected colony");
    (void)workspace.handle({InputEventType::LeftPressed, center(second)}, width,
                           height);
    require(!workspace
                 .handle({InputEventType::LeftReleased, center(third)},
                         width + 1, height)
                 .open_colony_id,
            "viewport change retained pressed row");
  }
}

void press_lifecycle_and_clipping_are_strict() {
  constexpr int width = 1280, height = 720;
  RosterWorkspace workspace;
  workspace.set_view(build(world(6), 4));
  workspace.open();
  const auto layout = RosterLayout::for_viewport(width, height);
  const auto first = workspace.row_button(0, width, height);
  (void)workspace.handle({InputEventType::LeftPressed, center(first)}, width,
                         height);
  auto changed = build(world(5), 4);
  std::swap(changed.rows[0], changed.rows[1]);
  workspace.set_view(changed);
  require(
      !workspace
           .handle({InputEventType::LeftReleased, center(first)}, width, height)
           .open_colony_id,
      "row reorder retained pressed selection");
  workspace.set_view(build(world(6), 4));
  (void)workspace.handle({InputEventType::LeftPressed,
                          center(workspace.row_button(0, width, height))},
                         width, height);
  (void)workspace.handle({InputEventType::PointerCancelled}, width, height);
  require(!workspace
               .handle({InputEventType::LeftReleased,
                        center(workspace.row_button(0, width, height))},
                       width, height)
               .open_colony_id,
          "focus cancellation opened a colony");
  (void)workspace.handle({InputEventType::LeftPressed, center(layout.refresh)},
                         width, height);
  const Point outside{layout.refresh.x + layout.refresh.width + 4.f,
                      layout.refresh.y + layout.refresh.height + 4.f};
  require(
      !workspace.handle({InputEventType::LeftReleased, outside}, width, height)
           .refresh,
      "refresh activated after drag outside");
  (void)workspace.handle({InputEventType::Wheel, center(layout.list), {}, -1.f},
                         width, height);
  const auto partial = workspace.row_button(0, width, height);
  const Point inside{partial.x + partial.width * .5f, layout.list.y + .5f};
  (void)workspace.handle({InputEventType::LeftPressed, inside}, width, height);
  const Point clipped_release{inside.x, layout.list.y - .25f};
  require(!workspace
               .handle({InputEventType::LeftReleased, clipped_release}, width,
                       height)
               .open_colony_id,
          "release across list clip opened a colony");
}

void live_refresh_preserves_scroll_but_invalidates_press() {
  constexpr int width = 1280, height = 720;
  RosterWorkspace workspace;
  workspace.set_view(build(world(40), 4));
  workspace.open();
  const auto layout = RosterLayout::for_viewport(width, height);
  (void)workspace.handle(
      {InputEventType::Wheel, center(layout.list), {}, -100.f}, width, height);
  const float scrolled = workspace.scroll_offset();
  require(scrolled > 0.f, "live refresh fixture did not scroll");

  constexpr int visible_index = 39;
  const auto row = workspace.row_button(visible_index, width, height);
  require(layout.list.contains(center(row)),
          "live refresh fixture selected an offscreen row");
  (void)workspace.handle({InputEventType::LeftPressed, center(row)}, width,
                         height);
  const auto opened = workspace.handle(
      {InputEventType::LeftReleased, center(row)}, width, height);
  require(opened.open_colony_id && *opened.open_colony_id == visible_index + 1,
          "visible matched click did not open its colony");
  (void)workspace.handle({InputEventType::LeftPressed, center(row)}, width,
                         height);
  auto refreshed = build(world(40), 4);
  refreshed.rows[visible_index].population = "77M";
  workspace.set_view(std::move(refreshed));
  require(workspace.scroll_offset() == scrolled,
          "same-identity live refresh reset roster scrolling");
  require(
      !workspace
           .handle({InputEventType::LeftReleased, center(row)}, width, height)
           .open_colony_id,
      "live refresh retained a pending colony press");

  workspace.set_notice("old notice");
  workspace.set_view(build(world(40), 5));
  require(workspace.scroll_offset() == 0.f,
          "identity change did not reset roster scrolling");
  workspace.open();
  DrawList draw;
  workspace.render(draw, width, height);
  const bool old_notice =
      std::ranges::any_of(draw.overlay, [](const auto &item) {
        const auto *label = std::get_if<Text>(&item);
        return label && label->value == "old notice";
      });
  require(!old_notice, "identity change retained stale roster notice");
}

void cancellation_and_compact_hover_are_bounded() {
  constexpr int width = 1280, height = 720;
  RosterWorkspace workspace;
  workspace.set_view(build(world(40), 4));
  workspace.open();
  const auto layout = RosterLayout::for_viewport(width, height);
  (void)workspace.handle(
      {InputEventType::Wheel, center(layout.list), {}, -100.f}, width, height);
  const float scrolled = workspace.scroll_offset();
  const auto row = workspace.row_button(39, width, height);
  require(layout.list.contains(center(row)),
          "cancellation fixture selected an offscreen row");
  (void)workspace.handle({InputEventType::LeftPressed, center(row)}, width,
                         height);
  workspace.cancel_pending_input();
  require(workspace.visible() && workspace.scroll_offset() == scrolled,
          "input cancellation closed or reset the roster");
  require(
      !workspace
           .handle({InputEventType::LeftReleased, center(row)}, width, height)
           .open_colony_id,
      "input cancellation retained a pressed row");
  (void)workspace.handle({InputEventType::PointerMove, center(row)}, width,
                         height);
  DrawList draw;
  workspace.render(draw, width, height);
  const bool population_heading =
      std::ranges::any_of(draw.overlay, [](const auto &item) {
        const auto *label = std::get_if<Text>(&item);
        return label && label->value == "POPULATION";
      });
  const bool hover_outline =
      std::ranges::any_of(draw.overlay, [](const auto &item) {
        const auto *outline = std::get_if<StrokedRectangle>(&item);
        return outline && outline->color.r == 82 && outline->color.g == 155;
      });
  require(population_heading && hover_outline,
          "compact roster did not expose population or row hover feedback");
}

void column_sort_orders_rows() {
  constexpr int width = 1920, height = 1080;
  RosterWorkspace workspace;
  workspace.set_view(build(world(40), 4));
  workspace.open();
  const auto layout = RosterLayout::for_viewport(width, height);
  const auto open_row = [&](std::size_t index) {
    const auto box = workspace.row_button(static_cast<int>(index), width, height);
    (void)workspace.handle({InputEventType::LeftPressed, center(box)}, width,
                           height);
    return workspace
        .handle({InputEventType::LeftReleased, center(box)}, width, height)
        .open_colony_id;
  };
  require(open_row(0) && *open_row(0) == 1,
          "default roster order did not start at the first colony");
  const auto click_header = [&](Point position) {
    (void)workspace.handle({InputEventType::LeftPressed, position}, width,
                           height);
    (void)workspace.handle({InputEventType::LeftReleased, position}, width,
                           height);
  };
  const Point name_header{layout.list.x + 8.f * layout.scale + 4.f,
                          layout.list.y - 14.f * layout.scale};
  click_header(name_header);
  const auto ascending_first = open_row(0);
  click_header(name_header);
  const auto descending_first = open_row(0);
  require(ascending_first && descending_first &&
              *ascending_first != *descending_first,
          "column sort did not reorder roster rows");
  const Point population_header{
      layout.list.x + layout.list.width * .79f + 4.f,
      layout.list.y - 14.f * layout.scale};
  click_header(population_header);
  click_header(population_header); // second click: descending
  const auto largest = open_row(0);
  require(largest && *largest == 40,
          "descending population sort did not lead with the largest colony");
  DrawList draw;
  workspace.render(draw, width, height);
  const bool sorted_marker = std::ranges::any_of(draw.overlay, [](const auto &item) {
    const auto *label = std::get_if<Text>(&item);
    return label && label->value == "POPULATION / ACTION v";
  });
  require(sorted_marker, "sorted column header did not show direction");
}

void search_filters_rows() {
  constexpr int width = 1920, height = 1080;
  RosterWorkspace workspace;
  workspace.set_view(build(world(40), 4));
  workspace.open();
  const auto layout = RosterLayout::for_viewport(width, height);
  const auto open_row = [&](std::size_t index) {
    const auto box = workspace.row_button(static_cast<int>(index), width, height);
    (void)workspace.handle({InputEventType::LeftPressed, center(box)}, width,
                           height);
    return workspace
        .handle({InputEventType::LeftReleased, center(box)}, width, height)
        .open_colony_id;
  };
  require(!workspace.wants_text_input(), "unfocused roster requested text input");
  (void)workspace.handle({InputEventType::LeftPressed, center(layout.search)},
                         width, height);
  (void)workspace.handle({InputEventType::LeftReleased, center(layout.search)},
                         width, height);
  require(workspace.wants_text_input(), "search field did not take text focus");
  // "Colony 9" is a contained-needle unique to "Terra Colony 9".
  for (const char ch : std::string_view{"Colony 9"})
    (void)workspace.handle(
        {InputEventType::TextEntered, {}, {}, 0.f, std::string(1, ch)}, width,
        height);
  require(workspace.row_button(0, width, height).height > 0 &&
              workspace.row_button(1, width, height).height == 0,
          "search did not narrow the roster to one row");
  const auto opened = open_row(0);
  require(opened && *opened == 9, "filtered row opened the wrong colony");
  // The filter survives a live refresh, like the column sort does.
  workspace.set_view(build(world(40), 4));
  require(workspace.row_button(1, width, height).height == 0,
          "live refresh dropped the active filter");
  // Escape blurs the field instead of closing the workspace (the row
  // click above dropped focus, so focus the field first).
  (void)workspace.handle({InputEventType::LeftPressed, center(layout.search)},
                         width, height);
  (void)workspace.handle({InputEventType::LeftReleased, center(layout.search)},
                         width, height);
  require(workspace.wants_text_input(), "refocus did not retake text input");
  (void)workspace.handle({InputEventType::EscapePressed}, width, height);
  require(workspace.visible() && !workspace.wants_text_input(),
          "escape closed the workspace instead of blurring search");
  // Refocus and clear the needle — the full list returns.
  (void)workspace.handle({InputEventType::LeftPressed, center(layout.search)},
                         width, height);
  (void)workspace.handle({InputEventType::LeftReleased, center(layout.search)},
                         width, height);
  for (int i = 0; i < 8; ++i)
    (void)workspace.handle({InputEventType::BackspacePressed}, width, height);
  require(workspace.row_button(39, width, height).height > 0,
          "clearing the search did not restore the full roster");
}
} // namespace

int main() {
  try {
    ownership_and_identity_are_sealed();
    references_survey_and_numbers_are_bounded();
    responsive_scroll_and_press_gates();
    press_lifecycle_and_clipping_are_strict();
    live_refresh_preserves_scroll_but_invalidates_press();
    cancellation_and_compact_hover_are_bounded();
    column_sort_orders_rows();
    search_filters_rows();
    std::cout << "native colony roster tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
