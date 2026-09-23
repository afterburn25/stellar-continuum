#include "native_chronicle.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

using namespace stellar;
using namespace stellar::native_chronicle;

namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

engine::EventHistory make_history() {
  engine::EventHistory history;
  const auto add = [&](double day, std::string category, std::string summary,
                       std::vector<std::uint64_t> visible = {}) {
    engine::HistoryEvent event;
    event.at_day = day;
    event.category = std::move(category);
    event.summary = std::move(summary);
    event.visible_to = std::move(visible);
    history.record(std::move(event));
  };
  add(400., "exploration.system_surveyed", "System survey completed");
  add(410., "war.battle", "FOREIGN BATTLE REPORT", {7});
  add(420., "colony.founded", "Colony established", {1});
  add(430., "custom.record", "Uncategorized record", {1});
  return history;
}

void snapshot_projection() {
  const auto history = make_history();
  const auto snap = snapshot(history, 1);
  require(snap.total == 3, "Observer-invisible chronicle entry leaked");
  require(snap.entries.size() == 3, "Visible entries lost");
  // Newest first.
  require(snap.entries.front().summary == "Uncategorized record" &&
              snap.entries.back().summary == "System survey completed",
          "Chronicle order broken");
  require(snap.entries.front().category == "custom.record",
          "Unmapped category lost its stable id");
  require(snap.entries[1].category == "Colony", "Category label not mapped");
  require(!snap.entries.front().date.empty() &&
              snap.entries.front().date != snap.entries.back().date,
          "Recorded event dates not preserved");

  const auto foreign = snapshot(history, 7);
  require(foreign.total == 2 &&
              foreign.entries.front().summary == "FOREIGN BATTLE REPORT",
          "Foreign observer saw wrong audience");

  const auto bounded = snapshot(history, 1, 2);
  require(bounded.total == 3 && bounded.entries.size() == 2 &&
              bounded.entries.back().summary == "Colony established",
          "Snapshot cap kept wrong entries");
}

void domain_filtering() {
  auto history = make_history();
  engine::HistoryEvent war;
  war.at_day = 440.;
  war.category = "war.engagement_started";
  war.summary = "Fleet engaged";
  war.visible_to = {1};
  history.record(std::move(war));
  const auto wars = snapshot(history, 1, 4000, "war.");
  require(wars.total == 1 && wars.entries.size() == 1 &&
              wars.entries.front().summary == "Fleet engaged",
          "Domain filter kept foreign categories");
  const auto colonies = snapshot(history, 1, 4000, "colony.");
  require(colonies.total == 1 &&
              colonies.entries.front().summary == "Colony established",
          "Colony filter missed its domain");
  const auto empty = snapshot(history, 1, 4000, "research.");
  require(empty.total == 0 && empty.entries.empty(),
          "Empty domain reported entries");

  NativeChronicleView view;
  view.open(history, 1);
  require(view.domain_filter().empty(), "Open did not reset domain");
  view.cycle_domain();
  require(view.domain_filter() == "construction." &&
              view.current().entries.empty(),
          "Cycle did not apply first domain");
  while (!view.domain_filter().empty()) view.cycle_domain();
  require(view.current().entries.size() == 4,
          "Full cycle did not restore unfiltered view");
}

void significance_filtering() {
  engine::EventHistory history;
  const auto add = [&](double day, std::string category, double sig) {
    engine::HistoryEvent event;
    event.at_day = day;
    event.summary = category;
    event.category = std::move(category);
    event.significance = sig;
    event.visible_to = {1};
    history.record(std::move(event));
  };
  add(400., "war.damage_applied", 0.1);       // trivia
  add(410., "exploration.sensor_contact", 0.35);
  add(420., "war.engagement_started", 0.7);   // major
  const auto all = snapshot(history, 1);
  require(all.total == 3, "Unfiltered snapshot dropped events");
  const auto routine = snapshot(history, 1, 4000, {}, 0.3);
  require(routine.total == 2, "Routine floor kept trivia");
  const auto major = snapshot(history, 1, 4000, {}, 0.7);
  require(major.total == 1 &&
              major.entries.front().summary == "war.engagement_started",
          "Major floor kept routine events");
  // Domain + significance compose.
  const auto major_war = snapshot(history, 1, 4000, "war.", 0.5);
  require(major_war.total == 1, "Combined filters did not compose");

  NativeChronicleView view;
  view.open(history, 1);
  require(view.significance_floor() == 0.0, "Open kept a stale floor");
  view.cycle_significance();
  require(view.significance_floor() > 0.29 &&
              view.current().entries.size() == 2,
          "Floor cycle did not filter");
  view.cycle_significance();
  view.cycle_significance();
  view.cycle_significance();
  require(view.significance_floor() == 0.0 &&
              view.current().entries.size() == 3,
          "Floor cycle did not wrap to unfiltered");
}

void view_lifecycle() {
  auto history = make_history();
  NativeChronicleView view;
  require(!view.visible(), "View visible before open");
  view.open(history, 1);
  require(view.visible() && view.current().entries.size() == 3,
          "Open did not materialize snapshot");

  // New records do not appear until refresh.
  engine::HistoryEvent extra;
  extra.at_day = 440.;
  extra.category = "research.legacy";
  extra.summary = "Research completed";
  extra.visible_to = {1};
  history.record(std::move(extra));
  require(view.current().entries.size() == 3,
          "Live history mutated an open snapshot");
  view.refresh();
  require(view.current().entries.size() == 4 &&
              view.current().entries.front().summary == "Research completed",
          "Refresh did not re-pull the chronicle");

  native_map::InputEvent escape;
  escape.type = native_map::InputEventType::EscapePressed;
  require(view.handle(escape, 1280, 800) && !view.visible(),
          "Escape did not close the view");

  // Outside click is not consumed; inside press is.
  view.open(history, 1);
  native_map::InputEvent press;
  press.type = native_map::InputEventType::LeftPressed;
  press.position = {5.f, 5.f};
  require(!view.handle(press, 1280, 800), "Outside press captured");
  press.position = {640.f, 400.f};
  require(view.handle(press, 1280, 800), "Panel press not captured");
  view.close();
}

void actor_filtering() {
  engine::EventHistory history;
  const auto add = [&](double day, std::string category,
                       std::vector<std::uint64_t> actors,
                       std::vector<std::uint64_t> visible) {
    engine::HistoryEvent event;
    event.at_day = day;
    event.summary = category;
    event.category = std::move(category);
    event.actors = std::move(actors);
    event.visible_to = std::move(visible);
    history.record(std::move(event));
  };
  // Intel: civ 7's battle in a system civ 1 knows — visible to 1 but
  // 1 is not an actor.
  add(400., "war.engagement_started", {7}, {1, 7});
  // Own: civ 1 founds a colony.
  add(410., "colony.founded", {1}, {1});
  // Joint: both civs involved (a treaty or engagement between them).
  add(415., "war.battle", {1, 7}, {1, 7});
  // Public record with no actors — visible to everyone, involves no civ.
  add(420., "exploration.system_surveyed", {}, {});

  const auto intel = snapshot(history, 1);
  require(intel.total == 4, "Observer lost visible intel");
  const auto mine = snapshot(history, 1, 4000, {}, 0.0, 1);
  require(mine.total == 2, "Actor filter kept non-actor events");
  const auto theirs = snapshot(history, 1, 4000, {}, 0.0, 7);
  require(theirs.total == 2 &&
              theirs.entries.front().summary == "war.battle",
          "Foreign actor filter missed joint + solo events");
  const auto nobody = snapshot(history, 1, 4000, {}, 0.0, 99);
  require(nobody.total == 0, "Unknown actor produced entries");
  // Actor filter composes with domain + significance.
  const auto my_colonies =
      snapshot(history, 1, 4000, "colony.", 0.0, 1);
  require(my_colonies.total == 1, "Actor filter did not compose");
  const auto their_wars = snapshot(history, 1, 4000, "war.", 0.0, 7);
  require(their_wars.total == 2, "Actor+domain composition wrong");

  NativeChronicleView view;
  view.open(history, 1);
  require(view.actor_filter() == 0, "Open kept a stale actor filter");
  view.cycle_actor();
  require(view.actor_filter() == 1 && view.current().entries.size() == 2,
          "First cycle did not land on the observer (MINE)");
  view.cycle_actor();
  require(view.actor_filter() == 7 && view.current().entries.size() == 2,
          "Second cycle did not land on the foreign civ");
  view.cycle_actor();
  require(view.actor_filter() == 0 &&
              view.current().entries.size() == 4,
          "Cycle did not wrap back to all intel");
}

void entry_navigation() {
  engine::EventHistory history;
  const auto add = [&](double day, std::string category,
                       std::uint64_t location,
                       std::vector<std::uint64_t> visible) {
    engine::HistoryEvent event;
    event.at_day = day;
    event.summary = category;
    event.category = std::move(category);
    event.location = location;
    event.visible_to = std::move(visible);
    history.record(std::move(event));
  };
  add(400., "exploration.system_surveyed", 9, {1});
  add(410., "war.engagement_started", 0, {1}); // locationless
  add(420., "war.fleet_destroyed", 4, {1});    // newest first

  const auto snap = snapshot(history, 1);
  require(snap.entries[0].system_id == 4 &&
              snap.entries[1].system_id == 0 &&
              snap.entries[2].system_id == 9,
          "Snapshot dropped event locations");

  NativeChronicleView view;
  view.open(history, 1);
  require(!view.navigation(), "Navigation pending before any click");

  // Click inside the first (newest) card — located at system 4. The
  // list viewport starts below the header+intro row; at 1280x800 the
  // first card spans roughly x 419-861, y 124-175.
  native_map::InputEvent press;
  press.type = native_map::InputEventType::LeftPressed;
  press.position = {450.f, 135.f};
  native_map::InputEvent release = press;
  release.type = native_map::InputEventType::LeftReleased;
  require(view.handle(press, 1280, 800), "Card press not captured");
  require(view.handle(release, 1280, 800), "Card release not captured");
  const auto nav = view.navigation();
  require(nav && *nav == 4, "Located entry did not navigate to system");
  require(!view.navigation(), "Navigation did not clear after take");

  // The locationless card navigates nowhere.
  view.close();
  view.open(history, 1);
  // Second card sits directly below the first.
  press.position = {450.f, 200.f};
  release.position = press.position;
  require(view.handle(press, 1280, 800), "Second card press not captured");
  require(view.handle(release, 1280, 800),
          "Second card release not captured");
  require(!view.navigation(), "Locationless entry navigated");
}

void render_smoke() {
  auto history = make_history();
  NativeChronicleView view;
  view.open(history, 1);
  native_map::DrawList out;
  view.render(out, 1280, 800);
  require(!out.overlay.empty(), "Render produced no draw commands");
}
} // namespace

int main() {
  try {
    snapshot_projection();
    domain_filtering();
    significance_filtering();
    actor_filtering();
    entry_navigation();
    view_lifecycle();
    render_smoke();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
