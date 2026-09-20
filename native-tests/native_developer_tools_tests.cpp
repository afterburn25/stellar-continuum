#include "native_developer_tools.hpp"

#include <iostream>
#include <stdexcept>

using namespace stellar::native_developer;
using namespace stellar::native_map;

namespace {

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

[[nodiscard]] InputEvent tap(Point at) {
  return {InputEventType::LeftReleased, at};
}

[[nodiscard]] Point center(UiRect rect) {
  return {rect.x + rect.width * .5f, rect.y + rect.height * .5f};
}

void verify_geometry() {
  for (const auto extent : {std::pair{1280, 720}, std::pair{2560, 1440}}) {
    const auto layout = developer_tools_layout_for(extent.first, extent.second);
    const UiRect viewport{0, 0, static_cast<float>(extent.first),
                          static_cast<float>(extent.second)};
    require(layout.panel.x >= viewport.x &&
                layout.panel.x + layout.panel.width <=
                    viewport.x + viewport.width &&
                layout.panel.y >= viewport.y &&
                layout.panel.y + layout.panel.height <=
                    viewport.y + viewport.height,
            "Tools panel escaped the viewport.");
    for (const auto &tab : layout.tabs)
      require(tab.y >= layout.panel.y &&
                  tab.y + tab.height <= layout.content.y,
              "A tab escaped between header and content.");
    require(layout.content.y + layout.content.height <=
                layout.result_text.y,
            "Content region overlapped the result text.");
    for (const auto &row : layout.command_rows)
      require(row.y >= layout.content.y &&
                  row.y + row.height <=
                      layout.content.y + layout.content.height,
              "A command row escaped the content region.");
  }
}

void verify_tab_switching() {
  constexpr int width = 1280, height = 720;
  NativeDeveloperToolsPanel panel;
  panel.open();
  const auto layout = developer_tools_layout_for(width, height);

  require(panel.active_tab() == DeveloperToolsTab::Commands,
          "The panel did not start on Commands.");

  // Diagnostics tab.
  const auto diagnostics =
      panel.handle(tap(center(layout.tabs[1])), width, height);
  require(diagnostics.captured &&
              diagnostics.kind == DeveloperToolsCommandKind::None,
          "The DIAGNOSTICS tab click did not switch tabs.");
  require(panel.active_tab() == DeveloperToolsTab::Diagnostics,
          "The active tab did not become Diagnostics.");

  // Command rows must not dispatch while another tab is active.
  const auto stray =
      panel.handle(tap(center(layout.command_rows[0])), width, height);
  require(stray.kind == DeveloperToolsCommandKind::None,
          "A command row fired while Diagnostics was active.");

  // Saves tab, then back to Commands.
  (void)panel.handle(tap(center(layout.tabs[2])), width, height);
  require(panel.active_tab() == DeveloperToolsTab::Saves,
          "The active tab did not become Saves.");
  (void)panel.handle(tap(center(layout.tabs[0])), width, height);
  require(panel.active_tab() == DeveloperToolsTab::Commands,
          "The active tab did not return to Commands.");

  // Commands dispatch still works on the Commands tab.
  const auto run =
      panel.handle(tap(center(layout.command_rows[0])), width, height);
  require(run.kind == DeveloperToolsCommandKind::Run &&
              !run.command_id.empty(),
          "The Commands tab did not dispatch a Run command.");
}

void verify_render_all_tabs() {
  constexpr int width = 1280, height = 720;
  NativeDeveloperToolsPanel panel;
  panel.open();
  NativeDeveloperToolsView view;
  view.developer = true;
  view.tools_used = true;
  view.result = "resources granted";
  view.result_accepted = true;
  view.diagnostics = {"-- Profiler", "frame mean 4.2 ms over 60 retained",
                      "-- Memory", "tracked 12.0 MiB current",
                      "-- Campaign", "simulation day 42.2"};
  view.save_slots = {
      {"current", "2044-05-06T07:08:09+00:00  day 42.2  verified", 0},
      {".bak", "2044-05-06T07:00:00+00:00  day 42.1  verified", 1},
      {".bak.2", "2044-05-05T07:00:00+00:00  day 41.9  CORRUPT", 3},
  };

  DrawList out;
  for (int tab = 0; tab < developer_tools_tab_count; ++tab) {
    const auto tabs_layout = developer_tools_layout_for(width, height);
    (void)panel.handle(tap(center(tabs_layout.tabs[tab])), width, height);
    out.overlay.clear();
    panel.render(out, view, width, height, nullptr);
    require(!out.overlay.empty(), "A tools tab rendered nothing.");
  }
}

}  // namespace

int main() {
  try {
    verify_geometry();
    verify_tab_switching();
    verify_render_all_tabs();
    std::cout << "Developer tools panel tabs passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Native developer tools test failed: " << error.what() << '\n';
    return 1;
  }
}
