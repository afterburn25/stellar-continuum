// Stellar Engine Editor — native C++23 standalone editor host.
//
// Successor to the 0.1.9-era WPF editor (PR #326, work/stellar-engine-editor,
// never merged). This host links the shared engine and core libraries
// directly instead of driving a pinned runtime as a hidden child process.
// The galaxy tool generates through the same authoritative
// generate_stellar_catalog path the game uses — no duplicated generation.

#include "stellar/build_version.hpp"

#include "editor_project.hpp"

#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/engine/atomic_file_write.hpp>
#include <stellar/engine/foundation.hpp>
#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/profiler.hpp>
#include <stellar/engine/runtime_diagnostics.hpp>
#include <stellar/engine/runtime_paths.hpp>
#include <stellar/engine/ui_viewmodels.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using namespace stellar::native_map;
namespace engine = stellar::engine;
namespace core = stellar::core;
namespace edproj = stellar::editor;

constexpr Color ink{210, 230, 244, 255};
constexpr Color muted{122, 170, 190, 255};
constexpr Color accent{86, 196, 255, 255};
constexpr Color panel_fill{8, 22, 34, 235};
constexpr Color panel_edge{28, 64, 88, 255};
constexpr Color row_hover{16, 40, 56, 255};
constexpr Color row_selected{22, 62, 92, 255};
constexpr Color button_fill{18, 48, 70, 255};

std::filesystem::path find_path(std::initializer_list<const char *> relatives) {
  auto base = engine::executable_directory();
  for (int depth = 0; depth < 4; ++depth) {
    for (const char *relative : relatives) {
      const auto candidate = base / relative;
      if (std::filesystem::exists(candidate)) return candidate;
    }
    if (!base.has_parent_path() || base == base.parent_path()) break;
    base = base.parent_path();
  }
  return *relatives.begin();
}

std::string_view class_name(core::StellarClass value) {
  switch (value) {
  case core::StellarClass::MRedDwarf: return "M red dwarf";
  case core::StellarClass::KOrangeDwarf: return "K orange dwarf";
  case core::StellarClass::GYellowDwarf: return "G yellow dwarf";
  case core::StellarClass::FYellowWhiteDwarf: return "F yellow-white dwarf";
  case core::StellarClass::AWhiteStar: return "A white star";
  case core::StellarClass::HotBlueStar: return "hot blue star";
  case core::StellarClass::Giant: return "giant";
  case core::StellarClass::WhiteDwarf: return "white dwarf";
  case core::StellarClass::NeutronStar: return "neutron star";
  case core::StellarClass::BlackHole: return "black hole";
  case core::StellarClass::Protostar: return "protostar";
  case core::StellarClass::Pulsar: return "pulsar";
  }
  return "unknown";
}

Color class_color(core::StellarClass value) {
  switch (value) {
  case core::StellarClass::MRedDwarf: return {214, 96, 74};
  case core::StellarClass::KOrangeDwarf: return {232, 160, 92};
  case core::StellarClass::GYellowDwarf: return {244, 214, 122};
  case core::StellarClass::FYellowWhiteDwarf: return {250, 238, 180};
  case core::StellarClass::AWhiteStar: return {228, 236, 252};
  case core::StellarClass::HotBlueStar: return {140, 180, 255};
  case core::StellarClass::Giant: return {224, 118, 84};
  case core::StellarClass::WhiteDwarf: return {214, 226, 240};
  case core::StellarClass::NeutronStar: return {120, 235, 235};
  case core::StellarClass::BlackHole: return {120, 90, 160};
  case core::StellarClass::Protostar: return {235, 178, 110};
  case core::StellarClass::Pulsar: return {190, 140, 250};
  }
  return muted;
}

float class_radius(core::StellarClass value) {
  switch (value) {
  case core::StellarClass::Giant: return 5.5f;
  case core::StellarClass::BlackHole: return 4.5f;
  case core::StellarClass::HotBlueStar: return 4.f;
  case core::StellarClass::Pulsar:
  case core::StellarClass::NeutronStar: return 3.5f;
  default: return 3.f;
  }
}

std::string_view archetype_name(core::StarArchetype value) {
  switch (value) {
  case core::StarArchetype::Standard: return "standard";
  case core::StarArchetype::ResourceRich: return "resource rich";
  case core::StarArchetype::HabitableRich: return "habitable rich";
  case core::StarArchetype::BarrenFrontier: return "barren frontier";
  case core::StarArchetype::Nebula: return "nebula";
  case core::StarArchetype::NeutronPulsar: return "neutron/pulsar";
  case core::StarArchetype::BlackHole: return "black hole";
  case core::StarArchetype::AncientRuin: return "ancient ruin";
  case core::StarArchetype::Dangerous: return "dangerous";
  case core::StarArchetype::Legendary: return "legendary";
  }
  return "unknown";
}

enum class Field { None, Name, Note, Search };

struct Editor {
  std::vector<core::CatalogStar> catalog;
  std::vector<core::StellarSystem> systems;
  std::int64_t seed{8374837};
  int system_count{500};
  double generate_ms{};
  std::string status{"ready"};

  // Camera: world coordinates -> drawable pixels.
  Point camera{0, 0};
  float pixels_per_unit{4.f};
  Point drag_origin{};
  Point camera_origin{};
  bool dragging{};

  std::size_t selected{static_cast<std::size_t>(-1)};
  engine::VirtualizedList system_list;
  std::vector<std::size_t> filtered;

  // Annotation layer + text editing state.
  std::unordered_map<int, edproj::SystemEdit> edits;
  Field editing{Field::None};
  std::string edit_buffer, search;

  // Toolbar + panel hit regions, rebuilt each frame.
  std::vector<UiRect> hits;
  std::vector<int> hit_sizes;
  UiRect hit_regen{}, hit_seed{}, hit_name{}, hit_note{}, hit_bookmark{},
      hit_save{}, hit_load{}, hit_search{};
  UiRect viewport{}, inspector{}, list_rect{}, rows_rect{};
  std::filesystem::path project_path;
  float pointer_x{}, pointer_y{};

  std::shared_ptr<const RgbaImage> emblem;
};

std::string display_name(const Editor &ed, const core::StellarSystem &sys) {
  if (const auto it = ed.edits.find(sys.id);
      it != ed.edits.end() && !it->second.name.empty())
    return it->second.name;
  return sys.name;
}

bool matches(const Editor &ed, const core::StellarSystem &sys) {
  if (ed.search.empty()) return true;
  const auto lower = [](std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    return text;
  };
  const auto needle = lower(ed.search);
  return lower(sys.name).find(needle) != std::string::npos ||
         lower(display_name(ed, sys)).find(needle) != std::string::npos;
}

void rebuild_filter(Editor &ed) {
  ed.filtered.clear();
  for (std::size_t i = 0; i < ed.systems.size(); ++i)
    if (matches(ed, ed.systems[i])) ed.filtered.push_back(i);
  ed.system_list.row_count = ed.filtered.size();
}

Point world_to_screen(const Editor &ed, float x, float y) {
  const auto &v = ed.viewport;
  return {v.x + v.width * .5f + (x - ed.camera.x) * ed.pixels_per_unit,
          v.y + v.height * .5f + (y - ed.camera.y) * ed.pixels_per_unit};
}

void fit_camera(Editor &ed) {
  if (ed.systems.empty()) return;
  float min_x = ed.systems.front().position.x,
        max_x = min_x, min_y = ed.systems.front().position.y, max_y = min_y;
  for (const auto &s : ed.systems) {
    min_x = std::min(min_x, s.position.x);
    max_x = std::max(max_x, s.position.x);
    min_y = std::min(min_y, s.position.y);
    max_y = std::max(max_y, s.position.y);
  }
  ed.camera = {(min_x + max_x) * .5f, (min_y + max_y) * .5f};
  const float span_x = std::max(1.f, max_x - min_x);
  const float span_y = std::max(1.f, max_y - min_y);
  ed.pixels_per_unit =
      std::min(ed.viewport.width / span_x, ed.viewport.height / span_y) * .9f;
}

void line(DrawList &out, float x, float &y, std::string label,
          std::string value, int font = 13) {
  out.text.push_back(Text{{x, y}, std::move(label), muted, font});
  out.text.push_back(Text{{x + 130.f, y}, std::move(value), ink, font});
  y += font + 8.f;
}

void field_box(DrawList &out, UiRect r, const std::string &value, bool active,
               const std::string &hint, int font) {
  out.overlay.push_back(
      FilledRectangle{r, active ? row_selected : Color{10, 24, 36, 255}});
  out.overlay.push_back(StrokedRectangle{r, active ? accent : panel_edge});
  out.text.push_back(Text{{r.x + 8, r.y + (r.height - font) * .5f - 2},
                          value.empty() ? hint : value,
                          value.empty() ? muted : ink, font, 0, r});
}

void small_button(DrawList &out, UiRect r, const std::string &label, bool active,
                  int font) {
  out.overlay.push_back(
      FilledRectangle{r, active ? row_selected : button_fill});
  out.overlay.push_back(StrokedRectangle{r, panel_edge});
  out.text.push_back(Text{{r.x, r.y + (r.height - font) * .5f - 2}, label, ink,
                          font, r.width, r, TextAlign::Center});
}

void render_inspector(DrawList &out, Editor &ed, float s) {
  const auto &r = ed.inspector;
  out.overlay.push_back(FilledRectangle{r, {6, 16, 26, 255}});
  out.overlay.push_back(StrokedRectangle{r, panel_edge});
  float x = r.x + 14 * s, y = r.y + 12 * s;
  const int font = static_cast<int>(13 * s);
  out.text.push_back(Text{{x, y}, "SYSTEM", accent, font + 2, 0, std::nullopt,
                          TextAlign::Left, FontFace::Heading});
  y += (font + 14) * s;

  // Project actions docked at the inspector bottom (drawn unconditionally).
  const float bw = (r.width - 34 * s) * .5f;
  ed.hit_save = {x, r.y + r.height - (font + 20) * s, bw, (font + 12) * s};
  ed.hit_load = {x + bw + 6 * s, ed.hit_save.y, bw, ed.hit_save.height};
  small_button(out, ed.hit_save, "SAVE PROJECT", false, font);
  small_button(out, ed.hit_load, "LOAD PROJECT", false, font);

  if (ed.selected >= ed.systems.size()) {
    out.text.push_back(Text{{x, y}, "No system selected", muted, font});
    y += font + 10 * s;
    ed.hit_name = ed.hit_note = ed.hit_bookmark = {};
    return;
  }
  const auto &sys = ed.systems[ed.selected];

  // Editable display name.
  out.text.push_back(Text{{x, y}, "display name", muted, font - 1});
  y += font + 4;
  ed.hit_name = {x, y, r.width - 28 * s, (font + 12) * s};
  const auto name_value =
      ed.editing == Field::Name ? ed.edit_buffer : display_name(ed, sys);
  field_box(out, ed.hit_name, name_value, ed.editing == Field::Name,
            sys.name, font);
  y += ed.hit_name.height + 8 * s;

  // Editable note.
  out.text.push_back(Text{{x, y}, "note", muted, font - 1});
  y += font + 4;
  ed.hit_note = {x, y, r.width - 28 * s, (font + 12) * s};
  const auto edit_it = ed.edits.find(sys.id);
  const edproj::SystemEdit empty_edit{};
  const auto &stored =
      edit_it != ed.edits.end() ? edit_it->second : empty_edit;
  const auto note_value =
      ed.editing == Field::Note ? ed.edit_buffer : stored.note;
  field_box(out, ed.hit_note, note_value, ed.editing == Field::Note,
            "add a note...", font);
  y += ed.hit_note.height + 8 * s;

  // Bookmark toggle.
  ed.hit_bookmark = {x, y, r.width - 28 * s, (font + 12) * s};
  small_button(out, ed.hit_bookmark,
               stored.bookmarked ? "BOOKMARKED" : "BOOKMARK",
               stored.bookmarked, font);
  y += ed.hit_bookmark.height + 12 * s;

  line(out, x, y, "name", sys.name, font);
  line(out, x, y, "id", std::to_string(sys.id), font);
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.1f, %.1f", sys.position.x,
                sys.position.y);
  line(out, x, y, "position", buffer, font);
  if (sys.primary)
    line(out, x, y, "primary", std::string(class_name(*sys.primary)), font);
  if (sys.secondary)
    line(out, x, y, "secondary",
         std::string(class_name(*sys.secondary)), font);
  if (sys.tertiary)
    line(out, x, y, "tertiary", std::string(class_name(*sys.tertiary)), font);
  line(out, x, y, "archetype", std::string(archetype_name(sys.archetype)),
       font);
  std::string flags;
  if (sys.has_habitable_world) flags += "habitable ";
  if (sys.has_anomaly) flags += "anomaly ";
  if (sys.has_rare_resource) flags += "rare-resource ";
  if (sys.has_pre_warp_civilization) flags += "pre-warp-civ";
  line(out, x, y, "traits", flags.empty() ? "none" : flags, font);
}

void render_system_list(DrawList &out, Editor &ed, float s) {
  const auto &r = ed.list_rect;
  out.overlay.push_back(FilledRectangle{r, {6, 16, 26, 255}});
  out.overlay.push_back(StrokedRectangle{r, panel_edge});
  float x = r.x + 14 * s;
  float y = r.y + 10 * s;
  const int font = static_cast<int>(12 * s);
  out.text.push_back(Text{{x, y}, "SYSTEMS", accent, font + 1, 0, std::nullopt,
                          TextAlign::Left, FontFace::Heading});
  y += (font + 12) * s;

  ed.hit_search = {x, y, r.width - 28 * s, (font + 12) * s};
  field_box(out, ed.hit_search, ed.search, ed.editing == Field::Search,
            "search systems...", font);
  y += ed.hit_search.height + 8 * s;

  const UiRect list{x, y, r.width - 28 * s, r.y + r.height - y - 10 * s};
  ed.rows_rect = list;
  out.overlay.push_back(FilledRectangle{list, {5, 13, 22, 255}});
  out.overlay.push_back(StrokedRectangle{list, panel_edge});
  ed.system_list.viewport_height = list.height;
  ed.system_list.row_height = 20.f;
  const auto range = ed.system_list.visible_range();
  float ry = list.y - ed.system_list.scroll_offset +
             range.first * ed.system_list.row_height;
  for (std::size_t i = range.first; i < range.last;
       ++i, ry += ed.system_list.row_height) {
    const auto sys_index = ed.filtered[i];
    const auto &sys = ed.systems[sys_index];
    const UiRect row{list.x, ry, list.width, ed.system_list.row_height};
    if (sys_index == ed.selected)
      out.overlay.push_back(FilledRectangle{row, row_selected});
    else if (row.contains(Point{ed.pointer_x, ed.pointer_y}))
      out.overlay.push_back(FilledRectangle{row, row_hover});
    const auto marker =
        ed.edits.contains(sys.id) && ed.edits[sys.id].bookmarked ? "* " : "";
    out.text.push_back(
        Text{{row.x + 8 * s, row.y + 3 * s},
             marker + display_name(ed, sys), ink, font, 0, list});
    if (sys.primary)
      out.text.push_back(
          Text{{row.x + row.width - 86 * s, row.y + 3 * s},
               std::string(class_name(*sys.primary)), muted, font, 84 * s,
               list});
  }
  if (ed.system_list.max_scroll() > 0) {
    const float track = list.height;
    const float thumb = std::max(
        20.f, track * track / (track + ed.system_list.max_scroll()));
    const float t = ed.system_list.scroll_offset / ed.system_list.max_scroll();
    out.overlay.push_back(
        FilledRectangle{{list.x + list.width - 5.f,
                         list.y + t * (track - thumb), 4.f, thumb},
                        accent});
  }
}

void save_project(Editor &ed) {
  try {
    const edproj::EditorProject project{ed.seed, ed.system_count, ed.edits};
    const auto text = edproj::serialize_project(project);
    engine::write_file_atomically(ed.project_path,
                                  std::as_bytes(std::span(text)));
    ed.status = "project saved - " +
                std::to_string(ed.edits.size()) + " annotations";
  } catch (const std::exception &error) {
    ed.status = std::string("save failed: ") + error.what();
  }
}

void load_project(Editor &ed) {
  try {
    std::ifstream in(ed.project_path, std::ios::binary);
    if (!in) {
      ed.status = "no project file at " + ed.project_path.filename().string();
      return;
    }
    const std::string text{std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>()};
    auto project = edproj::parse_project(text);
    // Swap only on full success: a malformed file leaves existing work intact.
    ed.edits = std::move(project.edits);
    rebuild_filter(ed);
    ed.status = "project loaded - " + std::to_string(ed.edits.size()) +
                " annotations";
  } catch (const std::exception &error) {
    ed.status = std::string("load failed: ") + error.what();
  }
}

void render_viewport(DrawList &out, const Editor &ed, float s) {
  const auto &v = ed.viewport;
  // Circles draw in the legacy layer under the overlay panels, so the dark
  // window fill doubles as the viewport background; only chrome sits in the
  // overlay here.
  for (const auto &sys : ed.systems) {
    const auto p = world_to_screen(ed, sys.position.x, sys.position.y);
    if (!v.contains(p)) continue;
    const auto color = sys.primary ? class_color(*sys.primary) : muted;
    out.circles.push_back(
        Circle{p, class_radius(sys.primary.value_or(
                               core::StellarClass::GYellowDwarf)) *
                      std::clamp(ed.pixels_per_unit * .25f, .5f, 3.f),
               color});
  }
  if (ed.selected < ed.systems.size()) {
    const auto &sys = ed.systems[ed.selected];
    const auto p = world_to_screen(ed, sys.position.x, sys.position.y);
    out.overlay.push_back(StrokedRectangle{
        {p.x - 10, p.y - 10, 20, 20}, accent});
    out.text.push_back(
        Text{{p.x + 14, p.y - 8}, display_name(ed, sys), accent,
             static_cast<int>(13 * s), 0, v});
  }
  if (ed.pixels_per_unit > 6.f)
    for (const auto &sys : ed.systems) {
      const auto p = world_to_screen(ed, sys.position.x, sys.position.y);
      if (!v.contains(p)) continue;
      out.text.push_back(Text{{p.x + 7, p.y - 6}, display_name(ed, sys),
                              {150, 180, 195, 220}, static_cast<int>(11 * s), 0,
                              v});
    }
  out.overlay.push_back(StrokedRectangle{v, panel_edge});
}

std::vector<core::StellarSystem> generate(std::int64_t seed, int count,
                                          const std::vector<core::CatalogStar> &catalog) {
  return core::generate_stellar_catalog(seed, count, catalog);
}

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
#else
int main(int argc, char **argv) {
#endif
  (void)argc;
  (void)argv;
  engine::RuntimeDiagnostics diagnostics("engine-editor",
                                         STELLAR_ENGINE_VERSION);
  try {
    engine::RuntimeDiagnostics::context("stellar engine editor");
    engine::JobSystem jobs;
    auto &profiler = engine::Profiler::instance();
    profiler.set_enabled(true);

    Editor ed;
    ed.project_path =
        engine::executable_directory() / "projects" / "editor-project.json";
    const auto catalog_path = find_path(
        {"Data/astronomy/hyg-nearby-500-v1.json",
         "data/astronomy/hyg-nearby-500-v1.json"});
    ed.catalog = core::load_nearby_catalog(catalog_path);
    ed.status = "catalog: " + std::to_string(ed.catalog.size()) + " stars";

    Window window("Stellar Engine Editor", 1600, 950, false,
                  find_path({"assets/visual/fonts/Rajdhani-SemiBold.ttf"}));
    window.set_auto_frame_cap();
    try {
      ed.emblem = decode_rgba_image(
          find_path({"assets/visual/branding/stellar-continuum-icon-v1.png"}),
          256);
    } catch (const std::exception &) {
    }

    std::atomic<bool> generating{false};
    std::atomic<std::int64_t> pending_seed{ed.seed};
    std::atomic<int> pending_count{ed.system_count};
    std::mutex result_mutex;
    std::vector<core::StellarSystem> result;
    std::int64_t result_seed{};
    int result_count{};
    double result_ms{};

    auto submit_generate = [&] {
      if (generating.exchange(true)) return;
      ed.status = "generating...";
      const auto seed = pending_seed.load();
      const int count = pending_count.load();
      auto catalog = ed.catalog;
      (void)jobs.submit("editor.generate", engine::JobPriority::Normal, {},
                        [&, seed, count, catalog = std::move(catalog)] {
                          const auto begin = std::chrono::steady_clock::now();
                          auto systems = generate(seed, count, catalog);
                          const auto elapsed =
                              std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - begin)
                                  .count();
                          std::lock_guard lock(result_mutex);
                          result = std::move(systems);
                          result_seed = seed;
                          result_count = count;
                          result_ms = elapsed;
                          generating.store(false);
                        });
    };
    submit_generate();

    for (;;) {
      const auto snapshot = window.poll();
      if (snapshot.quit_requested) break;

      {
        std::lock_guard lock(result_mutex);
        if (!result.empty()) {
          ed.systems = std::move(result);
          result.clear();
          ed.seed = result_seed;
          ed.system_count = result_count;
          ed.generate_ms = result_ms;
          ed.selected = static_cast<std::size_t>(-1);
          // Annotations survive regeneration: ids are deterministic for the
          // same seed+count, and a new seed simply orphans old edits.
          rebuild_filter(ed);
          ed.system_list.scroll_to(0);
          fit_camera(ed);
          ed.status = "ready - " + std::to_string(ed.systems.size()) +
                      " systems in " + std::to_string(ed.generate_ms) + " ms";
        }
      }

      ed.pointer_x = snapshot.pointer.x;
      ed.pointer_y = snapshot.pointer.y;
      for (const auto &event : snapshot.events) {
        if (event.type == InputEventType::EscapePressed) {
          if (ed.editing != Field::None) {
            ed.editing = Field::None;
            window.set_text_input(false);
          } else {
            return 0;
          }
          continue;
        }
        // Text editing takes precedence while a field has focus.
        if (ed.editing != Field::None) {
          if (event.type == InputEventType::TextEntered) {
            if (ed.edit_buffer.size() < 120) ed.edit_buffer += event.text;
            if (ed.editing == Field::Search) {
              ed.search = ed.edit_buffer;
              rebuild_filter(ed);
            }
            continue;
          }
          if (event.type == InputEventType::BackspacePressed) {
            if (!ed.edit_buffer.empty()) ed.edit_buffer.pop_back();
            if (ed.editing == Field::Search) {
              ed.search = ed.edit_buffer;
              rebuild_filter(ed);
            }
            continue;
          }
          if (event.type == InputEventType::KeyPressed && event.key == '\r') {
            // Enter commits.
            if (ed.editing == Field::Search) {
              ed.search = ed.edit_buffer;
              rebuild_filter(ed);
            } else if (ed.selected < ed.systems.size()) {
              auto &edit = ed.edits[ed.systems[ed.selected].id];
              if (ed.editing == Field::Name) edit.name = ed.edit_buffer;
              if (ed.editing == Field::Note) edit.note = ed.edit_buffer;
            }
            ed.editing = Field::None;
            window.set_text_input(false);
            continue;
          }
        }
        if (event.type == InputEventType::LeftPressed) {
          // Commit whatever field was being edited before handling the click,
          // so switching fields never silently drops the buffer.
          if (ed.editing != Field::None) {
            if (ed.editing == Field::Search) {
              ed.search = ed.edit_buffer;
              rebuild_filter(ed);
            } else if (ed.selected < ed.systems.size()) {
              auto &edit = ed.edits[ed.systems[ed.selected].id];
              if (ed.editing == Field::Name) edit.name = ed.edit_buffer;
              if (ed.editing == Field::Note) edit.note = ed.edit_buffer;
            }
            ed.editing = Field::None;
            window.set_text_input(false);
          }
          if (ed.hit_regen.contains(event.position)) {
            submit_generate();
          } else if (ed.hit_seed.contains(event.position)) {
            pending_seed.store(std::random_device{}());
            submit_generate();
          } else if (ed.hit_save.contains(event.position)) {
            save_project(ed);
          } else if (ed.hit_load.contains(event.position)) {
            load_project(ed);
          } else if (ed.hit_bookmark.contains(event.position) &&
                     ed.selected < ed.systems.size()) {
            auto &edit = ed.edits[ed.systems[ed.selected].id];
            edit.bookmarked = !edit.bookmarked;
          } else if (ed.hit_name.contains(event.position) &&
                     ed.selected < ed.systems.size()) {
            ed.editing = Field::Name;
            ed.edit_buffer =
                ed.edits[ed.systems[ed.selected].id].name;
            window.set_text_input(true);
          } else if (ed.hit_note.contains(event.position) &&
                     ed.selected < ed.systems.size()) {
            ed.editing = Field::Note;
            ed.edit_buffer =
                ed.edits[ed.systems[ed.selected].id].note;
            window.set_text_input(true);
          } else if (ed.hit_search.contains(event.position)) {
            ed.editing = Field::Search;
            ed.edit_buffer = ed.search;
            window.set_text_input(true);
          } else {
            for (std::size_t i = 0; i < ed.hits.size(); ++i)
              if (ed.hits[i].contains(event.position)) {
                pending_count.store(ed.hit_sizes[i]);
                submit_generate();
              }
            if (ed.viewport.contains(event.position)) {
              ed.dragging = true;
              ed.drag_origin = event.position;
              ed.camera_origin = ed.camera;
            }
          }
        }
        if (event.type == InputEventType::LeftReleased && ed.dragging) {
          ed.dragging = false;
          const float moved =
              std::abs(event.position.x - ed.drag_origin.x) +
              std::abs(event.position.y - ed.drag_origin.y);
          if (moved < 6.f && ed.viewport.contains(event.position)) {
            // Click: nearest system within a bounded pick radius.
            float best = 14.f;
            std::size_t best_index = ed.systems.size();
            for (std::size_t i = 0; i < ed.systems.size(); ++i) {
              const auto p = world_to_screen(ed, ed.systems[i].position.x,
                                             ed.systems[i].position.y);
              const float d = std::hypot(p.x - event.position.x,
                                         p.y - event.position.y);
              if (d < best) {
                best = d;
                best_index = i;
              }
            }
            ed.selected = best_index;
          }
        }
        // List row clicks select and center; wheel scrolls the list.
        if (event.type == InputEventType::LeftReleased &&
            ed.rows_rect.contains(event.position)) {
          const float local = event.position.y - ed.rows_rect.y +
                              ed.system_list.scroll_offset;
          const auto row = static_cast<std::size_t>(std::max(
              0.f, std::floor(local / ed.system_list.row_height)));
          if (row < ed.filtered.size()) {
            ed.selected = ed.filtered[row];
            ed.system_list.ensure_visible(row);
          }
        }
        if (event.type == InputEventType::Wheel &&
            ed.list_rect.contains(event.position))
          ed.system_list.scroll_to(ed.system_list.scroll_offset -
                                   event.wheel_y * 40.f);
        if (event.type == InputEventType::PointerMove && ed.dragging)
          ed.camera = {ed.camera_origin.x -
                           (event.position.x - ed.drag_origin.x) /
                               ed.pixels_per_unit,
                       ed.camera_origin.y -
                           (event.position.y - ed.drag_origin.y) /
                               ed.pixels_per_unit};
        if (event.type == InputEventType::Wheel &&
            ed.viewport.contains(event.position)) {
          const float before_x =
              ed.camera.x + (event.position.x - ed.viewport.x -
                             ed.viewport.width * .5f) /
                                ed.pixels_per_unit;
          const float before_y =
              ed.camera.y + (event.position.y - ed.viewport.y -
                             ed.viewport.height * .5f) /
                                ed.pixels_per_unit;
          ed.pixels_per_unit = std::clamp(
              ed.pixels_per_unit * (event.wheel_y > 0 ? 1.18f : 0.85f), 0.02f,
              400.f);
          ed.camera = {before_x - (event.position.x - ed.viewport.x -
                                   ed.viewport.width * .5f) /
                                      ed.pixels_per_unit,
                       before_y - (event.position.y - ed.viewport.y -
                                   ed.viewport.height * .5f) /
                                      ed.pixels_per_unit};
        }
      }
      if (!snapshot.renderable()) continue;

      const float w = static_cast<float>(snapshot.drawable_width);
      const float h = static_cast<float>(snapshot.drawable_height);
      const float s = std::clamp(h / 950.f, 0.8f, 2.0f);

      DrawList draw;
      // The SDL clear already fills the window dark; legacy circles/text draw
      // first, then the world layer, then overlay chrome on top.

      // Toolbar.
      const UiRect bar{16 * s, 14 * s, w - 32 * s, 52 * s};
      draw.overlay.push_back(FilledRectangle{bar, panel_fill});
      draw.overlay.push_back(StrokedRectangle{bar, panel_edge});
      draw.text.push_back(
          Text{{bar.x + 14 * s, bar.y + 14 * s}, "STELLAR ENGINE EDITOR", ink,
               static_cast<int>(20 * s), 0, std::nullopt, TextAlign::Left,
               FontFace::Heading});
      if (ed.emblem)
        draw.overlay.push_back(
            Image{ed.emblem, {bar.x + bar.width - 12 * s - 38 * s,
                              bar.y + 7 * s, 38 * s, 38 * s}});

      float bx = bar.x + 300 * s;
      ed.hits.clear();
      ed.hit_sizes.clear();
      for (const int count : {250, 500, 1000, 2500}) {
        const UiRect button{bx, bar.y + 10 * s, 76 * s, 32 * s};
        ed.hits.push_back(button);
        ed.hit_sizes.push_back(count);
        const bool active = pending_count.load() == count;
        draw.overlay.push_back(FilledRectangle{
            button, active ? row_selected : button_fill});
        draw.overlay.push_back(StrokedRectangle{button, panel_edge});
        draw.text.push_back(
            Text{{button.x + 0.f, button.y + 9 * s}, std::to_string(count),
                 active ? ink : muted, static_cast<int>(13 * s), button.width,
                 button, TextAlign::Center});
        bx += 84 * s;
      }
      ed.hit_regen = {bx, bar.y + 10 * s, 110 * s, 32 * s};
      draw.overlay.push_back(FilledRectangle{ed.hit_regen, button_fill});
      draw.overlay.push_back(StrokedRectangle{ed.hit_regen, panel_edge});
      draw.text.push_back(
          Text{{ed.hit_regen.x, ed.hit_regen.y + 9 * s}, "REGENERATE", ink,
               static_cast<int>(13 * s), ed.hit_regen.width, ed.hit_regen,
               TextAlign::Center});
      bx += 118 * s;
      ed.hit_seed = {bx, bar.y + 10 * s, 100 * s, 32 * s};
      draw.overlay.push_back(FilledRectangle{ed.hit_seed, button_fill});
      draw.overlay.push_back(StrokedRectangle{ed.hit_seed, panel_edge});
      draw.text.push_back(
          Text{{ed.hit_seed.x, ed.hit_seed.y + 9 * s}, "NEW SEED", ink,
               static_cast<int>(13 * s), ed.hit_seed.width, ed.hit_seed,
               TextAlign::Center});
      bx += 112 * s;
      draw.text.push_back(
          Text{{bx, bar.y + 18 * s}, "seed " + std::to_string(ed.seed), muted,
               static_cast<int>(13 * s)});

      // Workspace: viewport + right column split into inspector and the
      // searchable systems list.
      const float column_w = 304 * s;
      const float column_x = w - column_w - 16 * s;
      const float column_top = bar.y + bar.height + 10 * s;
      const float column_h = h - column_top - 16 * s;
      ed.inspector = {column_x, column_top, column_w, column_h * .55f};
      ed.list_rect = {column_x, column_top + ed.inspector.height + 8 * s,
                      column_w,
                      column_h - ed.inspector.height - 8 * s};
      ed.viewport = {16 * s, column_top, column_x - 32 * s, column_h};

      profiler.begin_frame();
      const auto frame_scope = profiler.span("editor.frame", "frame");
      render_viewport(draw, ed, s);
      render_inspector(draw, ed, s);
      render_system_list(draw, ed, s);
      draw.text.push_back(
          Text{{ed.viewport.x + 6 * s, ed.viewport.y + ed.viewport.height - 22 * s},
               ed.status + "  |  drag to pan, wheel to zoom, click to select",
               muted, static_cast<int>(12 * s), 0, ed.viewport});
      profiler.set_gauge("editor.systems",
                         static_cast<double>(ed.systems.size()));
      (void)profiler.end_frame();

      if (generating.load()) {
        draw.overlay.push_back(
            Text{{ed.viewport.x + ed.viewport.width * .5f - 80,
                  ed.viewport.y + ed.viewport.height * .5f},
                 "GENERATING...", accent, static_cast<int>(18 * s)});
      }

      window.draw(draw);
      // Swap the seed shown in the toolbar only once generation committed.
      ed.seed = pending_seed.load();
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Stellar Engine Editor " STELLAR_ENGINE_VERSION " error: "
              << error.what() << '\n';
    return 1;
  }
}
