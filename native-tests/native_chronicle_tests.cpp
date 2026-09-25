#include "native_chronicle.hpp"
#include "native_ui_theme.hpp"

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

  const auto bounded = snapshot(history, 1, {}, 2);
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
  const auto wars = snapshot(history, 1, {.category_prefix = "war."});
  require(wars.total == 1 && wars.entries.size() == 1 &&
              wars.entries.front().summary == "Fleet engaged",
          "Domain filter kept foreign categories");
  const auto colonies = snapshot(history, 1, {.category_prefix = "colony."});
  require(colonies.total == 1 &&
              colonies.entries.front().summary == "Colony established",
          "Colony filter missed its domain");
  const auto empty = snapshot(history, 1, {.category_prefix = "research."});
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
  const auto routine = snapshot(history, 1, {.min_significance = 0.3});
  require(routine.total == 2, "Routine floor kept trivia");
  const auto major = snapshot(history, 1, {.min_significance = 0.7});
  require(major.total == 1 &&
              major.entries.front().summary == "war.engagement_started",
          "Major floor kept routine events");
  // Domain + significance compose.
  const auto major_war = snapshot(history, 1, {.category_prefix = "war.", .min_significance = 0.5});
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
  const auto mine = snapshot(history, 1, {.actor = 1});
  require(mine.total == 2, "Actor filter kept non-actor events");
  const auto theirs = snapshot(history, 1, {.actor = 7});
  require(theirs.total == 2 &&
              theirs.entries.front().summary == "war.battle",
          "Foreign actor filter missed joint + solo events");
  const auto nobody = snapshot(history, 1, {.actor = 99});
  require(nobody.total == 0, "Unknown actor produced entries");
  // Actor filter composes with domain + significance.
  const auto my_colonies =
      snapshot(history, 1, {.category_prefix = "colony.", .actor = 1});
  require(my_colonies.total == 1, "Actor filter did not compose");
  const auto their_wars = snapshot(history, 1, {.category_prefix = "war.", .actor = 7});
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
                       std::vector<std::uint64_t> actors,
                       std::vector<std::uint64_t> visible) {
    engine::HistoryEvent event;
    event.at_day = day;
    event.summary = category;
    event.category = std::move(category);
    event.location = location;
    event.actors = std::move(actors);
    event.visible_to = std::move(visible);
    history.record(std::move(event));
  };
  add(400., "exploration.system_surveyed", 9, {1}, {1});
  add(410., "war.engagement_started", 0, {7, 9}, {1}); // two foreign
  add(420., "war.fleet_destroyed", 4, {7}, {1, 7});    // newest first

  const auto snap = snapshot(history, 1);
  require(snap.entries[0].system_id == 4 &&
              snap.entries[1].system_id == 0 &&
              snap.entries[2].system_id == 9,
          "Snapshot dropped event locations");
  require(snap.entries[0].contact_id == 7 &&
              snap.entries[1].contact_id == 0 &&
              snap.entries[2].contact_id == 0,
          "Contact id wrong: single foreign actor only");

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
  // The first card's DIP action (right edge of the message area)
  // routes the single foreign actor as a contact.
  press.position = {830.f, 152.f};
  release.position = press.position;
  require(view.handle(press, 1280, 800), "DIP press not captured");
  require(view.handle(release, 1280, 800), "DIP release not captured");
  const auto contact = view.contact_navigation();
  require(contact && *contact == 7,
          "DIP action did not route the foreign actor");
  require(!view.contact_navigation(), "Contact nav did not clear");
}

void tag_focus() {
  engine::EventHistory history;
  const auto add = [&](double day, std::string category,
                       std::vector<std::string> tags) {
    engine::HistoryEvent event;
    event.at_day = day;
    event.summary = category;
    event.category = std::move(category);
    event.tags = std::move(tags);
    event.visible_to = {1};
    history.record(std::move(event));
  };
  add(400., "war.battle", {"fleet:12", "system:5"});
  add(410., "colony.founded", {"colony:9"});
  add(420., "war.engagement_started", {"fleet:12"});
  add(430., "exploration.system_surveyed", {"system:5"}); // newest

  // The snapshot's tag axis is HistoryQuery::tag semantics — exact
  // match on the recorded reference.
  auto snap = snapshot(history, 1, {.tag = "fleet:12"});
  require(snap.total == 2 && snap.entries.size() == 2 &&
              snap.entries[0].summary == "war.engagement_started",
          "Tag filter did not isolate fleet:12 events");
  snap = snapshot(history, 1, {.tag = "system:5"});
  require(snap.total == 2, "Tag filter missed system:5 events");
  snap = snapshot(history, 1, {.category_prefix = "war.", .tag = "fleet:12"});
  require(snap.total == 2, "Tag filter did not compose with domain");
  require(snapshot(history, 1).entries[0].tags.size() == 1 &&
              snapshot(history, 1).entries[0].tags[0] == "system:5",
          "Snapshot dropped recorded tags");

  // Clicking the newest card's tag chip focuses the browser on it;
  // re-clicking the focused chip toggles it off.
  NativeChronicleView view;
  native_map::InputEvent press;
  press.type = native_map::InputEventType::LeftPressed;
  native_map::InputEvent release = press;
  release.type = native_map::InputEventType::LeftReleased;
  view.open(history, 1);
  press.position = {450.f, 175.f}; // first chip of the newest card
  release.position = press.position;
  require(view.handle(press, 1280, 800), "Chip press not captured");
  require(view.handle(release, 1280, 800), "Chip release not captured");
  require(view.tag_filter() == "system:5",
          "Chip click did not set the tag focus");
  require(view.current().entries.size() == 2,
          "Focused snapshot not restricted to the tag");
  require(view.handle(press, 1280, 800), "Focused chip press missed");
  require(view.handle(release, 1280, 800),
          "Focused chip release missed");
  require(view.tag_filter().empty(),
          "Re-clicking the focused chip did not clear it");
  require(view.current().entries.size() == 4,
          "Unfocused snapshot did not restore all entries");

  // Focus again, then clear via the X focus button in the intro row.
  require(view.handle(press, 1280, 800) &&
              view.handle(release, 1280, 800),
          "Second chip click missed");
  require(view.tag_filter() == "system:5", "Focus not re-set");
  press.position = {460.f, 110.f}; // focus button (intro row left)
  release.position = press.position;
  require(view.handle(press, 1280, 800), "Focus button press missed");
  require(view.handle(release, 1280, 800),
          "Focus button release missed");
  require(view.tag_filter().empty() &&
              view.current().entries.size() == 4,
          "Focus button did not clear the tag focus");
}

void recency_filtering() {
  engine::EventHistory history;
  const auto add = [&](double day, std::string category) {
    engine::HistoryEvent event;
    event.at_day = day;
    event.summary = category;
    event.category = std::move(category);
    event.visible_to = {1};
    history.record(std::move(event));
  };
  add(400., "war.battle");
  add(700., "colony.founded");
  add(790., "exploration.system_surveyed");
  add(799., "war.engagement_started"); // newest; campaign day is 800

  // The snapshot's since_day rides feed()'s own bound.
  auto snap =
      snapshot(history, 1, {.since_day = 770.});
  require(snap.total == 2 && snap.entries.size() == 2,
          "since_day did not bound the snapshot");
  snap = snapshot(history, 1, {.since_day = 435.});
  require(snap.total == 3, "since_day missed mid-window events");

  // The view's TIME cycle composes with the live day source.
  NativeChronicleView view;
  view.set_campaign_day_source([] { return 800.; });
  view.open(history, 1);
  require(view.current().entries.size() == 4 &&
              view.recency_window() == 0.0,
          "Open should start with all history");
  view.cycle_recency(); // 30d
  require(view.recency_window() == 30.0 &&
              view.current().entries.size() == 2,
          "30-day window kept wrong entries");
  view.cycle_recency(); // 1y
  require(view.recency_window() == 365.0 &&
              view.current().entries.size() == 3,
          "1-year window kept wrong entries");
  view.cycle_recency(); // 10y
  require(view.recency_window() == 3650.0 &&
              view.current().entries.size() == 4,
          "10-year window dropped entries");
  view.cycle_recency(); // wrap to all
  require(view.recency_window() == 0.0 &&
              view.current().entries.size() == 4,
          "Recency cycle did not wrap to all");

  // Without a day source the window is inert — full history stays.
  view.set_campaign_day_source(nullptr);
  view.cycle_recency();
  require(view.current().entries.size() == 4,
          "Missing day source should leave the window inert");

  // Time paging: a bounded window shifts backward/forward by its own
  // width — since_day + before_day form the closed range.
  snap = snapshot(history, 1, {.since_day = 700., .before_day = 790.});
  require(snap.total == 2,
          "before_day did not bound the window (inclusive)");
  snap = snapshot(history, 1, {.before_day = 700.});
  require(snap.total == 2 && snap.entries.front().category ==
                                "Colony",
          "upper-bound-only window kept wrong entries");

  // A boundary-day entry: day 435 is both the newest window's lower
  // edge and the first older page's upper edge — both bounds are
  // inclusive, so it must tile onto the newer page only.
  add(435., "exploration.system_surveyed");

  // The view sat at a (sourceless) 30d window — one cycle lands on
  // the 1y window [435, 800] at page 0.
  view.set_campaign_day_source([] { return 800.; });
  view.cycle_recency(); // 1y
  require(view.recency_window() == 365.0 && view.window_page() == 0 &&
              view.current().entries.size() == 4,
          "Window did not restart at the present edge");
  view.page_newer(); // clamped at page 0
  require(view.window_page() == 0, "page_newer escaped page 0");
  view.page_older(); // [70, 435) — the battle at day 400 only; the
                     // boundary entry at 435 stays on the newer page
  require(view.window_page() == 1 &&
              view.current().entries.size() == 1 &&
              view.current().entries.front().category == "Combat",
          "First page back kept wrong entries");
  view.page_older(); // [-295, 70] — empty
  require(view.window_page() == 2 &&
              view.current().entries.empty(),
          "Deep page kept phantom entries");
  view.page_newer();
  require(view.window_page() == 1 &&
              view.current().entries.size() == 1,
          "page_newer did not step forward");
  view.cycle_recency(); // 10y — page resets to 0
  require(view.window_page() == 0 &&
              view.current().entries.size() == 5,
          "Window cycle did not reset the page");
  // Paging is inert without a bounded window.
  view.cycle_recency(); // all
  view.page_older();
  require(view.window_page() == 0 &&
              view.current().entries.size() == 5,
          "Paging the all-history window did anything");
}

void search_filtering() {
  engine::EventHistory history;
  const auto add = [&](double day, std::string category,
                       std::string summary) {
    engine::HistoryEvent event;
    event.at_day = day;
    event.category = std::move(category);
    event.summary = std::move(summary);
    event.visible_to = {1};
    history.record(std::move(event));
  };
  add(400., "war.battle", "Fleet action at Proxima");
  add(410., "colony.founded", "Colony established on Terra");
  add(420., "war.engagement_started", "Raiders destroyed convoy");
  {
    engine::HistoryEvent tagged;
    tagged.at_day = 425.;
    tagged.category = "war.battle";
    tagged.summary = "Ambush at the rim";
    tagged.tags = {"fleet:12"};
    tagged.visible_to = {1};
    history.record(std::move(tagged));
  }

  // Case-insensitive substring over summary OR category id.
  require(snapshot(history, 1, {.search = "terra"}).total == 1,
          "Summary substring search missed");
  require(snapshot(history, 1, {.search = "WAR."}).total == 3,
          "Category substring search missed");
  require(snapshot(history, 1, {.search = "FLEET:12"}).total == 1,
          "Tag substring search missed");

  // The header field: click focuses it, text filters live, backspace
  // pops a codepoint, Escape unfocuses before closing.
  NativeChronicleView view;
  native_map::InputEvent press;
  press.type = native_map::InputEventType::LeftPressed;
  native_map::InputEvent release = press;
  release.type = native_map::InputEventType::LeftReleased;
  view.open(history, 1);
  require(!view.wants_text_input(), "Unfocused view wants text input");
  press.position = {600.f, 83.f}; // search box in the header
  release.position = press.position;
  require(view.handle(press, 1280, 800), "Search press not captured");
  require(view.handle(release, 1280, 800),
          "Search release not captured");
  require(view.wants_text_input(), "Search field did not focus");
  native_map::InputEvent text;
  text.type = native_map::InputEventType::TextEntered;
  text.text = "colony";
  require(view.handle(text, 1280, 800), "Text input not consumed");
  require(view.search() == "colony" &&
              view.current().entries.size() == 1 &&
              view.current().entries[0].summary ==
                  "Colony established on Terra",
          "Live search did not filter the snapshot");
  native_map::InputEvent backspace;
  backspace.type = native_map::InputEventType::BackspacePressed;
  require(view.handle(backspace, 1280, 800),
          "Backspace not consumed");
  require(view.search() == "colon" &&
              view.current().entries.size() == 1,
          "Backspace did not edit the query");
  native_map::InputEvent escape;
  escape.type = native_map::InputEventType::EscapePressed;
  require(view.handle(escape, 1280, 800), "Escape not consumed");
  require(!view.wants_text_input() && view.visible(),
          "Escape should unfocus search, not close the view");
  require(view.handle(escape, 1280, 800), "Second Escape lost");
  require(!view.visible(), "Second Escape did not close the view");
}

void keyboard_focus() {
  // Keyboard-focus contract: Tab/arrows ring every actionable control
  // in visual order, Home/End jump, and Return/Space replay the
  // press/release pair through the same dispatch pointer input takes.
  engine::EventHistory history;
  const auto add = [&](double day, std::string category,
                       std::uint64_t location,
                       std::vector<std::uint64_t> actors,
                       std::vector<std::string> tags) {
    engine::HistoryEvent event;
    event.at_day = day;
    event.category = std::move(category);
    event.summary = category;
    event.location = location;
    event.actors = std::move(actors);
    event.tags = std::move(tags);
    event.visible_to = {1};
    history.record(std::move(event));
  };
  add(400., "exploration.system_surveyed", 9, {}, {});
  add(410., "war.fleet_destroyed", 4, {7}, {"fleet:12"}); // newest

  NativeChronicleView view;
  view.open(history, 1);
  const auto key = [&](std::uint32_t code) {
    native_map::InputEvent event;
    event.type = native_map::InputEventType::KeyPressed;
    event.key = code;
    return view.handle(event, 1280, 800);
  };
  constexpr std::uint32_t kTab = 9u, kReturn = 13u;
  constexpr std::uint32_t kLeft = 0x40000050u, kDown = 0x40000051u;
  constexpr std::uint32_t kHome = 0x4000004au, kEnd = 0x4000004du;

  require(view.focus() < 0, "Focus ring present before any key");
  require(view.focused_label(1280, 800).empty(),
          "Unfocused view returned a label");
  // Focusables in (y,x) order: search/refresh/close, then the intro
  // cyclers time/actor/significance/domain, then card surfaces —
  // the newest card contributes its body, DIP action and tag chip,
  // the older located card its body.
  require(key(kTab) && view.focus() == 0, "Tab did not focus first control");
  require(view.focused_label(1280, 800) == "Refresh",
          "Focused label did not name the first control");
  require(key(kDown) && view.focus() == 1, "Down did not advance the ring");
  require(key(kLeft) && view.focus() == 0, "Left did not walk back");
  require(key(kEnd) && view.focus() == 10, "End did not land on the last row");
  require(!view.focused_label(1280, 800).empty(),
          "Card focus produced an empty label");
  // Activating a located card navigates to its system.
  require(key(kReturn), "Card activation not consumed");
  const auto nav = view.navigation();
  require(nav && *nav == 9, "Card activation did not navigate");
  // Back to the domain cycler — activation cycles the filter.
  require(key(kHome) && view.focus() == 0, "Home did not return to the head");
  for (int i = 0; i < 6; ++i) (void)key(kDown);
  require(view.focus() == 6, "Ring did not reach the domain cycler");
  require(key(kReturn) && view.domain_filter() == "construction.",
          "Domain activation did not cycle the filter");
  require(view.focus() == 6, "Activation did not keep the ring");
  // The ring renders as a drawn stroke over the focused control.
  {
    native_map::DrawList out;
    view.render(out, 1280, 800);
    bool ring = false;
    for (const auto &command : out.overlay)
      if (const auto *stroke =
              std::get_if<native_map::StrokedRectangle>(&command))
        if (stroke->color.r == native_ui::color::focus.r &&
            stroke->color.g == native_ui::color::focus.g)
          ring = true;
    require(ring, "Focused control rendered no ring");
  }
  // Pointer presses reset the ring; open starts clean.
  native_map::InputEvent press;
  press.type = native_map::InputEventType::LeftPressed;
  press.position = {640.f, 400.f};
  require(view.handle(press, 1280, 800) && view.focus() < 0,
          "Pointer press did not reset the ring");
  // The search field owns the keyboard while editing — arrows do not
  // move the ring, and Tab commits out of edit mode.
  press.position = {600.f, 83.f};
  native_map::InputEvent release = press;
  release.type = native_map::InputEventType::LeftReleased;
  require(view.handle(press, 1280, 800) && view.handle(release, 1280, 800),
          "Search field did not focus");
  require(view.wants_text_input(), "Search activation did not edit");
  require(key(kDown) && view.focus() < 0,
          "Editing search leaked a key to the ring");
  require(key(kTab) && !view.wants_text_input(),
          "Tab did not commit out of the search field");
  // Escape closes regardless of the ring.
  require(key(kTab) && view.focus() == 0, "Ring did not restart");
  native_map::InputEvent escape;
  escape.type = native_map::InputEventType::EscapePressed;
  require(view.handle(escape, 1280, 800) && !view.visible(),
          "Escape did not close with the ring active");
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
    tag_focus();
    recency_filtering();
    search_filtering();
    keyboard_focus();
    view_lifecycle();
    render_smoke();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
