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
#include <stellar/core/planet_appearance.hpp>
#include <stellar/core/planetary_adapter.hpp>
#include <stellar/core/planetary_catalog.hpp>
#include <stellar/core/planetary_satellites.hpp>
#include <stellar/core/stellar_orbits.hpp>
#include <stellar/core/stellar_population_profiles.hpp>
#include <stellar/engine/atomic_file_write.hpp>
#include <stellar/engine/foundation.hpp>
#include <stellar/engine/project.hpp>
#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/planetary.hpp>
#include <stellar/engine/profiler.hpp>
#include <stellar/engine/runtime_diagnostics.hpp>
#include <stellar/engine/runtime_paths.hpp>
#include <stellar/engine/ui_viewmodels.hpp>
#include <stellar/engine/undo_history.hpp>

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
#include <numbers>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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

std::string_view body_kind_name(core::PlanetaryBodyKind kind) {
  switch (kind) {
  case core::PlanetaryBodyKind::Planet: return "planet";
  case core::PlanetaryBodyKind::Moon: return "moon";
  case core::PlanetaryBodyKind::DwarfPlanet: return "dwarf planet";
  }
  return "body";
}

std::string_view atmosphere_name(core::PlanetaryAtmosphereRegime regime) {
  switch (regime) {
  case core::PlanetaryAtmosphereRegime::Vacuum: return "vacuum";
  case core::PlanetaryAtmosphereRegime::OxygenNitrogen:
    return "oxygen-nitrogen";
  case core::PlanetaryAtmosphereRegime::OxygenRich: return "oxygen-rich";
  case core::PlanetaryAtmosphereRegime::CarbonDioxideRich:
    return "co2-rich";
  case core::PlanetaryAtmosphereRegime::Reducing: return "reducing";
  case core::PlanetaryAtmosphereRegime::Inert: return "inert";
  case core::PlanetaryAtmosphereRegime::Other: return "other";
  }
  return "unknown";
}

std::string_view solvent_name(core::PlanetarySolventRegime regime) {
  switch (regime) {
  case core::PlanetarySolventRegime::None: return "none";
  case core::PlanetarySolventRegime::Water: return "water";
  case core::PlanetarySolventRegime::Ammonia: return "ammonia";
  case core::PlanetarySolventRegime::Hydrocarbon: return "hydrocarbon";
  case core::PlanetarySolventRegime::Other: return "other";
  }
  return "unknown";
}

enum class WorkspaceView { Galaxy, System, Body };

enum class Field { None, Name, Note, Search, ProjectName };

struct Editor {
  std::vector<core::CatalogStar> catalog;
  std::vector<core::StellarSystem> systems;
  std::vector<core::PlanetaryBody> bodies;
  // system_id -> indices into `bodies`, rebuilt after each generation.
  std::unordered_map<int, std::vector<std::size_t>> bodies_by_system;
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
  std::size_t selected_body{static_cast<std::size_t>(-1)}; // index into bodies
  WorkspaceView view{WorkspaceView::Galaxy};
  // System-view camera: AU coordinates -> drawable pixels; day scrubber
  // drives analytic positions deterministically.
  Point sys_camera{0, 0};
  float sys_ppa{48.f};
  // Body-view camera: kilometres relative to the focus body -> pixels.
  // `focus_body` is the body whose satellite system the view shows — a
  // moon's parent, or the selected body itself when it is not a moon.
  Point body_camera{0, 0};
  float body_ppa{0.002f};
  std::size_t focus_body{static_cast<std::size_t>(-1)};
  double system_days{};
  engine::VirtualizedList system_list;
  std::vector<std::size_t> filtered;

  // Scrollable authoritative detail rows for the selected system.
  engine::VirtualizedList detail_list;
  // A scrollable inspector row; `body_index` tags rows that select a body.
  struct DetailRow {
    std::string label;
    std::string value;
    std::size_t body_index = static_cast<std::size_t>(-1);
  };
  std::vector<DetailRow> detail_rows;
  std::vector<std::pair<UiRect, std::size_t>> detail_body_hits;

  // Annotation layer + text editing state. `edits` keys system ids,
  // `body_edits` keys body ids; history snapshots the whole project shape.
  std::unordered_map<int, edproj::SystemEdit> edits;
  std::unordered_map<int, edproj::SystemEdit> body_edits;
  engine::UndoHistory<edproj::EditorProject> history{64};
  Field editing{Field::None};
  std::string edit_buffer, search, project_name{"untitled"};

  // Toolbar + panel hit regions, rebuilt each frame.
  std::vector<UiRect> hits;
  std::vector<int> hit_sizes;
  UiRect hit_regen{}, hit_seed{}, hit_name{}, hit_note{}, hit_bookmark{},
      hit_anomaly{}, hit_rare{}, hit_prewarp{},
      hit_save{}, hit_load{}, hit_search{}, hit_undo{}, hit_redo{},
      hit_view{}, hit_project_name{}, hit_bkmk_filter{}, hit_file{};
  // FILE dropdown: rects parallel to menu_labels(), rebuilt each frame.
  bool menu_open{};
  UiRect menu_rect{};
  std::vector<UiRect> menu_item_rects;
  UiRect viewport{}, inspector{}, list_rect{}, rows_rect{}, detail_rect{};
  std::filesystem::path projects_dir, project_path;
  // Multi-file projects: a saved directory holds project.json plus an
  // assets/ folder of embedded files (dropped in by the user). The folder
  // scan is the manifest — discovered pngs preview in the inspector.
  bool project_dir_form{};
  std::vector<std::filesystem::path> embedded_assets;
  std::shared_ptr<const RgbaImage> asset_preview;
  std::string asset_preview_name;
  // Project open picker: *.json files under projects_dir, modal overlay.
  std::vector<std::filesystem::path> project_files;
  engine::VirtualizedList picker_list;
  bool picker_open{};
  bool bookmark_only{}; // systems list filters to bookmarked targets
  UiRect picker_rect{}, picker_rows{};
  float pointer_x{}, pointer_y{};

  std::shared_ptr<const RgbaImage> emblem;
};

std::string display_name(const Editor &ed, const core::StellarSystem &sys) {
  if (const auto it = ed.edits.find(sys.id);
      it != ed.edits.end() && !it->second.name.empty())
    return it->second.name;
  return sys.name;
}

std::string display_name(const Editor &ed, const core::PlanetaryBody &body) {
  if (const auto it = ed.body_edits.find(body.id);
      it != ed.body_edits.end() && !it->second.name.empty())
    return it->second.name;
  return body.name;
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
  if (ed.bookmark_only) {
    // Bookmarked systems match; so do systems containing a bookmarked body.
    const auto it = ed.edits.find(sys.id);
    bool bookmarked = it != ed.edits.end() && it->second.bookmarked;
    if (!bookmarked)
      if (const auto bodies = ed.bodies_by_system.find(sys.id);
          bodies != ed.bodies_by_system.end())
        for (const auto body_index : bodies->second)
          if (const auto bit =
                  ed.body_edits.find(ed.bodies[body_index].id);
              bit != ed.body_edits.end() && bit->second.bookmarked)
            bookmarked = true;
    if (!bookmarked) return false;
  }
  if (lower(sys.name).find(needle) != std::string::npos ||
      lower(display_name(ed, sys)).find(needle) != std::string::npos)
    return true;
  // A system also matches when one of its bodies does — searching "Earth"
  // finds Sol.
  if (const auto bodies = ed.bodies_by_system.find(sys.id);
      bodies != ed.bodies_by_system.end())
    for (const auto body_index : bodies->second) {
      const auto &body = ed.bodies[body_index];
      if (lower(body.name).find(needle) != std::string::npos ||
          lower(display_name(ed, body)).find(needle) != std::string::npos)
        return true;
    }
  return false;
}

void rebuild_filter(Editor &ed) {
  ed.filtered.clear();
  for (std::size_t i = 0; i < ed.systems.size(); ++i)
    if (matches(ed, ed.systems[i])) ed.filtered.push_back(i);
  ed.system_list.row_count = ed.filtered.size();
}

void rebuild_detail_rows(Editor &ed);

// The annotation target for the current selection: (id, is_body). In the
// system and body workspaces a selected body annotates its own record;
// otherwise the selected system does.
std::optional<std::pair<int, bool>> annotation_target(const Editor &ed) {
  if (ed.view != WorkspaceView::Galaxy &&
      ed.selected_body < ed.bodies.size())
    return std::pair{ed.bodies[ed.selected_body].id, true};
  if (ed.selected < ed.systems.size())
    return std::pair{ed.systems[ed.selected].id, false};
  return std::nullopt;
}

std::unordered_map<int, edproj::SystemEdit> &annotation_map(Editor &ed,
                                                          bool body) {
  return body ? ed.body_edits : ed.edits;
}

// Effective trait: an annotation override wins over the generated record.
template <class Record>
bool trait(const std::unordered_map<int, edproj::SystemEdit> &map,
           const Record &record, bool Record::*generated,
           std::optional<bool> edproj::SystemEdit::*override) {
  const auto it = map.find(record.id);
  if (it != map.end() && it->second.*override) return *(it->second.*override);
  return record.*generated;
}

// Commits the in-progress field buffer into the annotation layer, recording
// an undo snapshot only when the value actually changes. In the system
// workspace with a body selected, fields bind to that body's record.
void commit_active_field(Editor &ed) {
  if (ed.editing == Field::None) return;
  if (ed.editing == Field::Search) {
    if (ed.search != ed.edit_buffer) {
      ed.search = ed.edit_buffer;
      rebuild_filter(ed);
    }
  } else if (ed.editing == Field::ProjectName) {
    if (ed.project_name != ed.edit_buffer) {
      ed.history.commit({ed.seed, ed.system_count, ed.edits, ed.body_edits,
                         ed.project_name});
      ed.project_name = ed.edit_buffer;
    }
  } else if (const auto target = annotation_target(ed)) {
    auto &map = annotation_map(ed, target->second);
    const auto it = map.find(target->first);
    const std::string current_value =
        it == map.end() ? std::string{}
                        : (ed.editing == Field::Name ? it->second.name
                                                     : it->second.note);
    if (current_value != ed.edit_buffer) {
      ed.history.commit(
          {ed.seed, ed.system_count, ed.edits, ed.body_edits,
           ed.project_name});
      auto &stored = map[target->first];
      (ed.editing == Field::Name ? stored.name : stored.note) =
          ed.edit_buffer;
      rebuild_filter(ed); // overrides are searchable text
      if (target->second && ed.editing == Field::Name)
        rebuild_detail_rows(ed); // body row labels show overrides
    }
  }
  ed.editing = Field::None;
}

void apply_undo(Editor &ed) {
  if (auto state =
          ed.history.undo({ed.seed, ed.system_count, ed.edits,
                           ed.body_edits, ed.project_name})) {
    ed.edits = std::move(state->edits);
    ed.body_edits = std::move(state->body_edits);
    ed.project_name = std::move(state->name);
    rebuild_filter(ed);
    rebuild_detail_rows(ed);
    ed.status = "undo";
  }
}

void apply_redo(Editor &ed) {
  if (auto state =
          ed.history.redo({ed.seed, ed.system_count, ed.edits,
                           ed.body_edits, ed.project_name})) {
    ed.edits = std::move(state->edits);
    ed.body_edits = std::move(state->body_edits);
    ed.project_name = std::move(state->name);
    rebuild_filter(ed);
    rebuild_detail_rows(ed);
    ed.status = "redo";
  }
}

std::string fspec(const char *format, double value) {
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), format, value);
  return buffer;
}

// Flattens a planetary body's authoritative record into detail rows.
void rebuild_body_rows(Editor &ed, const core::PlanetaryBody &body) {
  auto row = [&ed](std::string label, std::string value) {
    ed.detail_rows.emplace_back(std::move(label), std::move(value));
  };
  row("name", body.name);
  row("id", std::to_string(body.id));
  row("kind", std::string(body_kind_name(body.kind)));
  row("radius", fspec("%.3f", body.radius_earth) + " Re");
  row("mass", fspec("%.3f", body.mass_earth) + " Me");
  row("gravity", fspec("%.3f", body.environment.gravity_g) + " g");
  row("temperature",
      fspec("%.0f", body.environment.temperature_kelvin) + " K");
  row("pressure", fspec("%.1f", body.environment.pressure_kpa) + " kPa");
  row("atmosphere",
      std::string(atmosphere_name(body.environment.atmosphere)));
  row("solvent",
      std::string(solvent_name(body.environment.available_solvent)));
  row("radiation", fspec("%.2f", body.environment.radiation_hazard));
  row("surface", body.environment.has_solid_surface ? "solid" : "none");
  if (body.environment.is_immersed_environment)
    row("environment", "immersed");
  if (body.parent_body_id)
    row("parent body", std::to_string(*body.parent_body_id));
  row("orbit index", std::to_string(body.orbit_index));
  row("eccentricity", fspec("%.3f", body.orbital_eccentricity));
  row("inclination",
      fspec("%.1f", body.orbital_inclination_degrees) + " deg");
  if (body.stellar_exposure) {
    row("orbit", fspec("%.3f", body.stellar_exposure->orbit_au) + " AU");
    row("incident flux", fspec("%.3f", body.stellar_exposure->incident_flux));
    row("habitable zone",
        body.stellar_exposure->in_habitable_zone ? "inside" : "outside");
    row("safe approach",
        fspec("%.3f", body.stellar_exposure->safe_approach_au) + " AU");
  }
  if (body.appearance)
    row("appearance",
        core::planet_appearance_display_name(*body.appearance));
  std::string flags;
  if (body.legacy_colonization_candidate) flags += "colonization-candidate ";
  if (trait(ed.body_edits, body, &core::PlanetaryBody::has_anomaly,
            &edproj::SystemEdit::anomaly))
    flags += "anomaly ";
  if (trait(ed.body_edits, body, &core::PlanetaryBody::has_rare_resource,
            &edproj::SystemEdit::rare_resource))
    flags += "rare-resource ";
  if (trait(ed.body_edits, body,
            &core::PlanetaryBody::has_pre_warp_civilization,
            &edproj::SystemEdit::pre_warp_civilization))
    flags += "pre-warp-civ ";
  if (body.cracked_world) flags += "cracked ";
  row("traits", flags.empty() ? "none" : flags);

  // Engine planetary-framework assessment: the same projection +
  // evaluator the engine tools and future games consume
  // (planetary_adapter → evaluate_habitability), scored against a
  // terran-like reference profile. Core species suitability stays
  // authoritative for campaign rules — this row set reports the
  // reusable engine view.
  const auto engine_env = core::to_engine_environment(body);
  static const engine::HabitabilityProfile terran_like = [] {
    engine::HabitabilityProfile profile;
    profile.id = "terran_like";
    profile.temperature_min_k = 240.0;
    profile.temperature_max_k = 330.0;
    profile.atmosphere_min = 0.3;
    profile.atmosphere_max = 3.0;
    profile.gravity_min_g = 0.4;
    profile.gravity_max_g = 1.6;
    profile.water_min = 0.05;
    profile.tolerance = 0.25;
    profile.required_tags = {"atmosphere.oxygen_nitrogen",
                           "solvent.water"};
    profile.forbidden_tags = {"high_radiation", "gas_giant", "immersed",
                            "cracked"};
    return profile;
  }();
  const auto report = engine::evaluate_habitability(engine_env,
                                                    terran_like);
  row("engine suitability",
      fspec("%.3f", report.suitability) +
          (report.habitable ? " (habitable)" : ""));
  for (const auto &reason : report.unmet)
    row("suitability unmet", reason);
}

// Flattens the selected system's authoritative generated record into
// label/value rows for the scrollable inspector detail list. In the system
// workspace with a selected body, shows the body's record instead.
void rebuild_detail_rows(Editor &ed) {
  ed.detail_rows.clear();
  ed.detail_list.scroll_to(0);
  if (ed.view != WorkspaceView::Galaxy &&
      ed.selected_body < ed.bodies.size()) {
    rebuild_body_rows(ed, ed.bodies[ed.selected_body]);
    ed.detail_list.row_count = ed.detail_rows.size();
    return;
  }
  if (ed.selected >= ed.systems.size()) {
    ed.detail_list.row_count = 0;
    return;
  }
  const auto &sys = ed.systems[ed.selected];
  auto row = [&ed](std::string label, std::string value) {
    ed.detail_rows.emplace_back(std::move(label), std::move(value));
  };
  row("name", sys.name);
  row("id", std::to_string(sys.id));
  row("position",
      fspec("%.1f", sys.position.x) + ", " + fspec("%.1f", sys.position.y));
  if (sys.primary)
    row("primary", std::string(class_name(*sys.primary)));
  if (sys.secondary)
    row("secondary", std::string(class_name(*sys.secondary)));
  if (sys.tertiary)
    row("tertiary", std::string(class_name(*sys.tertiary)));
  row("archetype", std::string(archetype_name(sys.archetype)));
  std::string flags;
  if (sys.has_habitable_world) flags += "habitable ";
  if (trait(ed.edits, sys, &core::StellarSystem::has_anomaly,
            &edproj::SystemEdit::anomaly))
    flags += "anomaly ";
  if (trait(ed.edits, sys, &core::StellarSystem::has_rare_resource,
            &edproj::SystemEdit::rare_resource))
    flags += "rare-resource ";
  if (trait(ed.edits, sys, &core::StellarSystem::has_pre_warp_civilization,
            &edproj::SystemEdit::pre_warp_civilization))
    flags += "pre-warp-civ";
  row("traits", flags.empty() ? "none" : flags);
  if (sys.stellar_region)
    row("region", std::string(core::stellar_region_name(*sys.stellar_region)));

  if (sys.stellar_object) {
    const auto &o = *sys.stellar_object;
    row("object", std::string(core::stellar_object_definition(o.type).name));
    row("mass", fspec("%.3f", o.mass_solar) + " M");
    row("radius", fspec("%.3f", o.radius_solar) + " R");
    row("luminosity", fspec("%.3f", o.luminosity_solar) + " L");
    row("temperature", fspec("%.0f", o.effective_temperature_kelvin) + " K");
    row("age", fspec("%.0f", o.age_myr) + " Myr");
    row("habitable zone",
        fspec("%.2f", o.inner_hz_au) + " - " + fspec("%.2f", o.outer_hz_au) +
            " AU");
    row("safe approach", fspec("%.3f", o.safe_approach_au) + " AU");
    if (o.active) row("state", "active");
    if (o.measured_anchor) row("anchor", "measured catalog");
  }

  if (sys.stellar_orbits) {
    const auto &orb = *sys.stellar_orbits;
    row("companions", std::to_string(orb.companions.size()));
    for (std::size_t i = 0; i < orb.companions.size(); ++i)
      row("companion " + std::to_string(i + 1),
          std::string(
              core::stellar_object_definition(orb.companions[i].type).name));
    row("planet bindings", std::to_string(orb.planets.size()));
    row("relative orbits", std::to_string(orb.relative_orbits.size()));
    row("belt host", core::stellar_host_name(orb.belt_host));
  }

  if (sys.small_body_fields) {
    row("small-body fields", std::to_string(sys.small_body_fields->size()));
    for (const auto &field : *sys.small_body_fields)
      row(std::string(core::small_body_field_name(field.type)),
          std::to_string(field.body_count) + " bodies, " +
              fspec("%.1f", field.inner_radius_au) + "-" +
              fspec("%.1f", field.outer_radius_au) + " AU");
  }

  if (sys.stellar_activity)
    for (std::size_t i = 0; i < sys.stellar_activity->size(); ++i)
      row("activity " + core::stellar_host_name(static_cast<int>(i)),
          std::string(core::stellar_activity_name(
              (*sys.stellar_activity)[i].profile.level)));

  if (const auto bodies = ed.bodies_by_system.find(sys.id);
      bodies != ed.bodies_by_system.end()) {
    row("planetary bodies", std::to_string(bodies->second.size()));
    for (const auto body_index : bodies->second) {
      const auto &body = ed.bodies[body_index];
      std::string value = std::string(body_kind_name(body.kind)) + ", " +
                          fspec("%.2f", body.radius_earth) + " Re, " +
                          fspec("%.2f", body.environment.gravity_g) + " g, " +
                          fspec("%.0f", body.environment.temperature_kelvin) +
                          " K, " + std::string(atmosphere_name(
                                       body.environment.atmosphere));
      if (body.stellar_exposure)
        value += ", " + fspec("%.2f", body.stellar_exposure->orbit_au) + " AU";
      if (body.stellar_exposure && body.stellar_exposure->in_habitable_zone)
        value += " [hz]";
      if (trait(ed.body_edits, body, &core::PlanetaryBody::has_anomaly,
                &edproj::SystemEdit::anomaly))
        value += " [anomaly]";
      if (trait(ed.body_edits, body, &core::PlanetaryBody::has_rare_resource,
                &edproj::SystemEdit::rare_resource))
        value += " [rare]";
      if (trait(ed.body_edits, body,
                &core::PlanetaryBody::has_pre_warp_civilization,
                &edproj::SystemEdit::pre_warp_civilization))
        value += " [pre-warp]";
      if (body.cracked_world) value += " [cracked]";
      const auto bit = ed.body_edits.find(body.id);
      const auto marker =
          bit != ed.body_edits.end() && bit->second.bookmarked ? "* " : "";
      ed.detail_rows.push_back(
          {marker + display_name(ed, body), value, body_index});
    }
  }

  ed.detail_list.row_count = ed.detail_rows.size();
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

void field_box(DrawList &out, UiRect r, const std::string &value, bool active,
               const std::string &hint, int font) {
  out.overlay.push_back(
      FilledRectangle{r, active ? row_selected : Color{10, 24, 36, 255}});
  out.overlay.push_back(StrokedRectangle{r, active ? accent : panel_edge});
  out.overlay.push_back(Text{{r.x + 8, r.y + (r.height - font) * .5f - 2},
                          value.empty() ? hint : value,
                          value.empty() ? muted : ink, font, 0, r});
}

void small_button(DrawList &out, UiRect r, const std::string &label, bool active,
                  int font) {
  out.overlay.push_back(
      FilledRectangle{r, active ? row_selected : button_fill});
  out.overlay.push_back(StrokedRectangle{r, panel_edge});
  out.overlay.push_back(
      Text{{r.x + r.width * .5f, r.y + (r.height - font) * .5f - 2}, label,
           ink, font, r.width, r, TextAlign::Center});
}

void render_inspector(DrawList &out, Editor &ed, float s) {
  const auto &r = ed.inspector;
  out.overlay.push_back(FilledRectangle{r, {6, 16, 26, 255}});
  out.overlay.push_back(StrokedRectangle{r, panel_edge});
  float x = r.x + 14 * s, y = r.y + 12 * s;
  const int font = static_cast<int>(13 * s);
  const bool inspecting_body =
      ed.view != WorkspaceView::Galaxy &&
      ed.selected_body < ed.bodies.size() &&
      ed.selected < ed.systems.size();
  out.overlay.push_back(Text{{x, y}, inspecting_body ? "BODY" : "SYSTEM", accent,
                          font + 2, 0, std::nullopt, TextAlign::Left,
                          FontFace::Heading});
  y += (font + 14) * s;

  // Project actions + history docked at the inspector bottom (drawn
  // unconditionally — undo must work with no selection, e.g. after a load).
  const float bw = (r.width - 34 * s) * .5f;
  ed.hit_save = {x, r.y + r.height - (font + 20) * s, bw, (font + 12) * s};
  ed.hit_load = {x + bw + 6 * s, ed.hit_save.y, bw, ed.hit_save.height};
  small_button(out, ed.hit_save, "SAVE PROJECT", false, font);
  small_button(out, ed.hit_load, "LOAD PROJECT", false, font);
  ed.hit_undo = {x, ed.hit_save.y - (font + 18) * s, bw, (font + 12) * s};
  ed.hit_redo = {x + bw + 6 * s, ed.hit_undo.y, bw, ed.hit_undo.height};
  small_button(out, ed.hit_undo, "UNDO", ed.history.can_undo(), font);
  small_button(out, ed.hit_redo, "REDO", ed.history.can_redo(), font);

  if (ed.selected >= ed.systems.size()) {
    out.overlay.push_back(Text{{x, y}, "No system selected", muted, font});
    y += font + 10 * s;
    ed.hit_name = ed.hit_note = ed.hit_bookmark = {};
    ed.hit_anomaly = ed.hit_rare = ed.hit_prewarp = {};
    return;
  }
  const auto &sys = ed.systems[ed.selected];
  // The authoring target: the selected body in the system workspace, else
  // the system itself.
  const bool body_context = ed.view != WorkspaceView::Galaxy &&
                            ed.selected_body < ed.bodies.size();
  const int target_id =
      body_context ? ed.bodies[ed.selected_body].id : sys.id;
  const auto &map = body_context ? ed.body_edits : ed.edits;
  const auto edit_it = map.find(target_id);
  const edproj::SystemEdit empty_edit{};
  const auto &stored =
      edit_it != map.end() ? edit_it->second : empty_edit;

  // Editable display name.
  out.overlay.push_back(Text{{x, y},
                          body_context ? "body display name" : "display name",
                          muted, font - 1});
  y += font + 4;
  ed.hit_name = {x, y, r.width - 28 * s, (font + 12) * s};
  const auto name_value =
      ed.editing == Field::Name
          ? ed.edit_buffer
          : (body_context ? display_name(ed, ed.bodies[ed.selected_body])
                          : display_name(ed, sys));
  field_box(out, ed.hit_name, name_value, ed.editing == Field::Name,
            body_context ? ed.bodies[ed.selected_body].name : sys.name, font);
  y += ed.hit_name.height + 8 * s;

  // Editable note.
  out.overlay.push_back(Text{{x, y}, "note", muted, font - 1});
  y += font + 4;
  ed.hit_note = {x, y, r.width - 28 * s, (font + 12) * s};
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
  y += ed.hit_bookmark.height + 8 * s;

  // Trait overrides cycle AUTO -> YES -> NO -> AUTO: AUTO follows the
  // generated record, YES/NO override it in the annotation layer.
  const auto trait_button = [&](UiRect &hit, const char *label,
                                const std::optional<bool> &value) {
    hit = {x, y, r.width - 28 * s, (font + 12) * s};
    const auto state = value ? (*value ? "YES" : "NO") : "AUTO";
    small_button(out, hit, std::string(label) + ": " + state,
                 value && *value, font);
    y += hit.height + 6 * s;
  };
  trait_button(ed.hit_anomaly, "ANOMALY", stored.anomaly);
  trait_button(ed.hit_rare, "RARE RESOURCE", stored.rare_resource);
  trait_button(ed.hit_prewarp, "PRE-WARP CIV", stored.pre_warp_civilization);
  y += 4 * s;

  // Embedded assets: files dropped under <project>/assets/ ride with the
  // project; the first png previews here.
  if (!ed.embedded_assets.empty()) {
    out.overlay.push_back(
        Text{{x, y},
             "assets: " + std::to_string(ed.embedded_assets.size()) +
                 " embedded",
             muted, font - 1});
    y += font + 4;
    const float thumb = 48 * s;
    if (ed.asset_preview)
      out.overlay.push_back(Image{ed.asset_preview,
                                  {x, y, thumb, thumb}, std::nullopt,
                                  {255, 255, 255, 255}});
    out.overlay.push_back(Text{{x + (ed.asset_preview ? thumb + 8 * s : 0),
                                y + (thumb - font) * .5f},
                               ed.asset_preview_name, ink, font - 1,
                               r.width - thumb - 40 * s});
    y += thumb + 8 * s;
  }

  // Scrollable authoritative detail rows fill the space above the docked
  // project/history buttons.
  const UiRect detail{x, y, r.width - 28 * s,
                      std::max(0.f, ed.hit_undo.y - 8 * s - y)};
  ed.detail_rect = detail;
  out.overlay.push_back(FilledRectangle{detail, {5, 13, 22, 255}});
  out.overlay.push_back(StrokedRectangle{detail, panel_edge});
  ed.detail_list.viewport_height = detail.height;
  ed.detail_list.row_height = font + 8.f;
  const auto range = ed.detail_list.visible_range();
  float ry = detail.y + 4 * s - ed.detail_list.scroll_offset +
             range.first * ed.detail_list.row_height;
  ed.detail_body_hits.clear();
  for (std::size_t i = range.first; i < range.last;
       ++i, ry += ed.detail_list.row_height) {
    const auto &row = ed.detail_rows[i];
    if (row.body_index != static_cast<std::size_t>(-1)) {
      const UiRect rowrect{detail.x, ry, detail.width - 8 * s,
                           ed.detail_list.row_height};
      ed.detail_body_hits.emplace_back(rowrect, row.body_index);
      if (row.body_index == ed.selected_body)
        out.overlay.push_back(FilledRectangle{rowrect, row_selected});
      else if (rowrect.contains(Point{ed.pointer_x, ed.pointer_y}))
        out.overlay.push_back(FilledRectangle{rowrect, row_hover});
    }
    out.overlay.push_back(Text{{detail.x + 8 * s, ry + 2}, row.label, muted,
                            font - 1, 100 * s, detail});
    out.overlay.push_back(Text{{detail.x + 112 * s, ry + 2}, row.value, ink,
                            font - 1, 0, detail});
  }
  if (ed.detail_list.max_scroll() > 0) {
    const float track = detail.height;
    const float thumb = std::max(
        20.f, track * track / (track + ed.detail_list.max_scroll()));
    const float t = ed.detail_list.scroll_offset / ed.detail_list.max_scroll();
    out.overlay.push_back(
        FilledRectangle{{detail.x + detail.width - 5.f,
                         detail.y + t * (track - thumb), 4.f, thumb},
                        accent});
  }
}

void render_system_list(DrawList &out, Editor &ed, float s) {
  const auto &r = ed.list_rect;
  out.overlay.push_back(FilledRectangle{r, {6, 16, 26, 255}});
  out.overlay.push_back(StrokedRectangle{r, panel_edge});
  float x = r.x + 14 * s;
  float y = r.y + 10 * s;
  const int font = static_cast<int>(12 * s);
  out.overlay.push_back(Text{{x, y}, "SYSTEMS", accent, font + 1, 0, std::nullopt,
                          TextAlign::Left, FontFace::Heading});
  y += (font + 12) * s;

  ed.hit_search = {x, y, r.width - 62 * s, (font + 12) * s};
  field_box(out, ed.hit_search, ed.search, ed.editing == Field::Search,
            "search systems...", font);
  ed.hit_bkmk_filter = {x + ed.hit_search.width + 4 * s, y,
                        r.width - 28 * s - ed.hit_search.width - 4 * s,
                        ed.hit_search.height};
  small_button(out, ed.hit_bkmk_filter, "*", ed.bookmark_only, font);
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
    out.overlay.push_back(
        Text{{row.x + 8 * s, row.y + 3 * s},
             marker + display_name(ed, sys), ink, font, 0, list});
    if (sys.primary)
      out.overlay.push_back(
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

// Modal project picker overlay listing every *.json under projects_dir.
void render_picker(DrawList &out, Editor &ed, float s, float w, float h) {
  if (!ed.picker_open) {
    ed.picker_rect = ed.picker_rows = {};
    return;
  }
  const int font = static_cast<int>(13 * s);
  const float pw = std::min(430 * s, w - 60 * s);
  const float ph = std::min(380 * s, h - 120 * s);
  const UiRect r{(w - pw) * .5f, (h - ph) * .5f, pw, ph};
  ed.picker_rect = r;
  out.overlay.push_back(FilledRectangle{r, {8, 20, 32, 250}});
  out.overlay.push_back(StrokedRectangle{r, accent});
  out.overlay.push_back(Text{{r.x + 14 * s, r.y + 10 * s}, "OPEN PROJECT", accent,
                          font + 1, 0, std::nullopt, TextAlign::Left,
                          FontFace::Heading});
  out.overlay.push_back(
      Text{{r.x + 14 * s, r.y + 12 * s + font},
           "esc or click outside to close", muted, font - 2});
  const UiRect rows{r.x + 10 * s, r.y + 18 * s + font * 2, r.width - 20 * s,
                    r.height - 30 * s - font * 2};
  out.overlay.push_back(FilledRectangle{rows, {5, 13, 22, 255}});
  out.overlay.push_back(StrokedRectangle{rows, panel_edge});
  ed.picker_list.viewport_height = rows.height;
  ed.picker_list.row_height = font + 10.f;
  // Hit geometry starts at the first row top (below the 4*s inset).
  ed.picker_rows = {rows.x, rows.y + 4 * s, rows.width, rows.height - 4 * s};
  const auto range = ed.picker_list.visible_range();
  float ry = ed.picker_rows.y - ed.picker_list.scroll_offset +
             range.first * ed.picker_list.row_height;
  for (std::size_t i = range.first; i < range.last;
       ++i, ry += ed.picker_list.row_height) {
    const UiRect row{rows.x, ry, rows.width, ed.picker_list.row_height};
    if (row.contains(Point{ed.pointer_x, ed.pointer_y}))
      out.overlay.push_back(FilledRectangle{row, row_hover});
    out.overlay.push_back(
        Text{{row.x + 8 * s, ry + 4 * s},
             ed.project_files[i].filename().string(), ink, font, 0, rows});
    if (ed.project_files[i] == ed.project_path ||
        (ed.project_dir_form &&
         ed.project_files[i] == ed.project_path.parent_path()))
      out.overlay.push_back(Text{{row.x + row.width - 66 * s, ry + 5 * s},
                              "current", accent, font - 2, 0, rows});
  }
  if (ed.project_files.empty())
    out.overlay.push_back(Text{{rows.x + 8 * s, rows.y + 8 * s},
                            "no saved projects yet", muted, font});
  if (ed.picker_list.max_scroll() > 0) {
    const float track = rows.height;
    const float thumb = std::max(
        20.f, track * track / (track + ed.picker_list.max_scroll()));
    const float t = ed.picker_list.scroll_offset / ed.picker_list.max_scroll();
    out.overlay.push_back(
        FilledRectangle{{rows.x + rows.width - 5.f,
                         rows.y + t * (track - thumb), 4.f, thumb},
                        accent});
  }
}

// Discovers embedded assets under a project directory's assets/ folder and
// decodes the first png for the inspector preview.
void refresh_embedded_assets(Editor &ed) {
  ed.embedded_assets.clear();
  ed.asset_preview.reset();
  ed.asset_preview_name.clear();
  const auto dir = ed.project_path.parent_path() / "assets";
  std::error_code ec;
  if (std::filesystem::is_directory(dir, ec))
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
      if (entry.is_regular_file() && entry.path().extension() == ".png")
        ed.embedded_assets.push_back(entry.path());
  std::sort(ed.embedded_assets.begin(), ed.embedded_assets.end());
  if (!ed.embedded_assets.empty()) {
    try {
      ed.asset_preview = decode_rgba_image(ed.embedded_assets.front(), 256);
      ed.asset_preview_name = ed.embedded_assets.front().filename().string();
    } catch (const std::exception &) {
      ed.asset_preview_name = "unreadable: " +
                              ed.embedded_assets.front().filename().string();
    }
  }
}

void save_project(Editor &ed) {
  try {
    // Save-As by name: the document name drives the filename inside the
    // projects directory, so renaming a project never clobbers another.
    const auto slug = edproj::sanitize_project_name(ed.project_name);
    const auto stem = slug.empty() ? "editor-project" : slug;
    const auto dir = ed.projects_dir / stem;
    // Multi-file form: a project directory holding project.json + assets/.
    // Chosen when the project already lives in one or an assets folder
    // exists — the user embeds files by dropping them under <slug>/assets/.
    const auto assets_dir = dir / "assets";
    ed.project_dir_form =
        ed.project_dir_form || std::filesystem::is_directory(assets_dir);
    if (ed.project_dir_form) {
      std::filesystem::create_directories(assets_dir);
      ed.project_path = dir / "project.json";
    } else {
      ed.project_path = ed.projects_dir / (stem + ".json");
    }
    const edproj::EditorProject project{ed.seed, ed.system_count, ed.edits,
                                        ed.body_edits, ed.project_name};
    const auto text = edproj::serialize_project(project);
    engine::write_file_atomically(ed.project_path,
                                  std::as_bytes(std::span(text)));
    // Upgrading a flat save to a directory retires the old single file.
    if (ed.project_dir_form) {
      const auto stale = ed.projects_dir / (stem + ".json");
      std::error_code ec;
      std::filesystem::remove(stale, ec);
    }
    refresh_embedded_assets(ed);
    ed.status = "saved " + ed.project_path.filename().string() + " - " +
                std::to_string(ed.edits.size() + ed.body_edits.size()) +
                " annotations" +
                (ed.embedded_assets.empty()
                     ? ""
                     : ", " + std::to_string(ed.embedded_assets.size()) +
                           " assets");
  } catch (const std::exception &error) {
    ed.status = std::string("save failed: ") + error.what();
  }
}

void load_project(Editor &ed, const std::filesystem::path &path) {
  try {
    std::error_code ec;
    const bool dir_form = std::filesystem::is_directory(path, ec);
    const auto file = dir_form ? path / "project.json" : path;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
      ed.status = "no project file at " + path.filename().string();
      return;
    }
    const std::string text{std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>()};
    auto project = edproj::parse_project(text);
    // Swap only on full success: a malformed file leaves existing work intact.
    ed.history.commit({ed.seed, ed.system_count, ed.edits, ed.body_edits,
                       ed.project_name});
    ed.edits = std::move(project.edits);
    ed.body_edits = std::move(project.body_edits);
    ed.project_path = file;
    ed.project_dir_form = dir_form;
    if (!project.name.empty()) ed.project_name = std::move(project.name);
    refresh_embedded_assets(ed);
    rebuild_filter(ed);
    rebuild_detail_rows(ed);
    ed.status = "loaded " + path.filename().string() + " - " +
                std::to_string(ed.edits.size() + ed.body_edits.size()) +
                " annotations" +
                (ed.embedded_assets.empty()
                     ? ""
                     : ", " + std::to_string(ed.embedded_assets.size()) +
                           " assets");
  } catch (const std::exception &error) {
    ed.status = std::string("load failed: ") + error.what();
  }
}

// Refreshes and opens the project picker over the projects directory.
void open_picker(Editor &ed) {
  ed.project_files.clear();
  std::error_code ec;
  if (std::filesystem::exists(ed.projects_dir, ec))
    for (const auto &entry :
         std::filesystem::directory_iterator(ed.projects_dir, ec)) {
      if (entry.is_regular_file() && entry.path().extension() == ".json")
        ed.project_files.push_back(entry.path());
      else if (entry.is_directory() &&
               std::filesystem::is_regular_file(entry.path() / "project.json",
                                                ec))
        ed.project_files.push_back(entry.path());
    }
  std::sort(ed.project_files.begin(), ed.project_files.end());
  ed.picker_list.row_count = ed.project_files.size();
  ed.picker_list.scroll_to(0);
  ed.picker_open = true;
  if (ed.project_files.empty())
    ed.status = "no saved projects in " + ed.projects_dir.filename().string();
}

// FILE menu entries, in row order.
constexpr std::array<const char *, 4> menu_labels{
    "NEW PROJECT", "SAVE   ctrl+s", "RENAME / SAVE AS", "OPEN PROJECT...   ctrl+o"};

// Executes a FILE menu item by row index.
void run_menu_item(Editor &ed, Window &window, std::size_t item) {
  switch (item) {
  case 0: // New Project: clear the annotation document, keep the galaxy.
    ed.history.commit({ed.seed, ed.system_count, ed.edits, ed.body_edits,
                       ed.project_name});
    ed.edits.clear();
    ed.body_edits.clear();
    ed.project_name = "untitled";
    ed.project_path = ed.projects_dir / "editor-project.json";
    rebuild_filter(ed);
    rebuild_detail_rows(ed);
    ed.status = "new project";
    break;
  case 1:
    save_project(ed);
    break;
  case 2: // Rename/Save-As: focus the name field; next SAVE writes <name>.json.
    ed.editing = Field::ProjectName;
    ed.edit_buffer = ed.project_name;
    window.set_text_input(true);
    ed.status = "rename the project - SAVE writes <name>.json";
    break;
  case 3:
    open_picker(ed);
    break;
  }
}

// FILE dropdown panel under the toolbar button.
void render_menu(DrawList &out, Editor &ed, float s) {
  ed.menu_item_rects.clear();
  if (!ed.menu_open) {
    ed.menu_rect = {};
    return;
  }
  const int font = static_cast<int>(13 * s);
  const float rh = font + 12.f;
  const float mw = 240 * s;
  const UiRect r{ed.hit_file.x, ed.hit_file.y + ed.hit_file.height + 4 * s, mw,
                 menu_labels.size() * rh + 12 * s};
  ed.menu_rect = r;
  out.overlay.push_back(FilledRectangle{r, {8, 20, 32, 250}});
  out.overlay.push_back(StrokedRectangle{r, accent});
  float ry = r.y + 6 * s;
  for (std::size_t i = 0; i < menu_labels.size(); ++i, ry += rh) {
    const UiRect row{r.x + 4 * s, ry, r.width - 8 * s, rh};
    ed.menu_item_rects.push_back(row);
    if (row.contains(Point{ed.pointer_x, ed.pointer_y}))
      out.overlay.push_back(FilledRectangle{row, row_hover});
    out.overlay.push_back(Text{{row.x + 10 * s, ry + (rh - font) * .5f - 1},
                               menu_labels[i], ink, font, 0, row});
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
    out.world.push_back(
        Text{{p.x + 14, p.y - 8}, display_name(ed, sys), accent,
             static_cast<int>(13 * s), 0, v});
  }
  if (ed.pixels_per_unit > 6.f)
    for (const auto &sys : ed.systems) {
      const auto p = world_to_screen(ed, sys.position.x, sys.position.y);
      if (!v.contains(p)) continue;
      out.world.push_back(Text{{p.x + 7, p.y - 6}, display_name(ed, sys),
                               {150, 180, 195, 220}, static_cast<int>(11 * s),
                               0, v});
    }
  out.overlay.push_back(StrokedRectangle{v, panel_edge});
}

Point system_to_screen(const Editor &ed, double x, double y) {
  const auto &v = ed.viewport;
  return {v.x + v.width * .5f +
              static_cast<float>((x - ed.sys_camera.x) * ed.sys_ppa),
          v.y + v.height * .5f +
              static_cast<float>((y - ed.sys_camera.y) * ed.sys_ppa)};
}

// Samples the analytic orbit into a screen-space polyline ring centered on
// `cx,cy` in the caller's world units. True ellipse-in-3D projected onto
// the view plane.
template <typename ToScreen>
void orbit_ring_mapped(DrawList &out, const engine::AnalyticOrbit &orbit,
                       double cx, double cy, Color color,
                       ToScreen &&to_screen) {
  const double period =
      2.0 * std::numbers::pi / std::max(1e-9, orbit.angular_speed);
  Point prev{};
  for (int k = 0; k <= 72; ++k) {
    const auto p =
        engine::analytic_orbit_position(orbit, k * period / 72.0);
    const auto point = to_screen(cx + p[0], cy + p[1]);
    if (k) out.world.push_back(Line{prev, point, color});
    prev = point;
  }
}

void orbit_ring(DrawList &out, const Editor &ed,
                const engine::AnalyticOrbit &orbit, double cx, double cy,
                Color color) {
  orbit_ring_mapped(out, orbit, cx, cy, color,
                    [&](double x, double y) {
                      return system_to_screen(ed, x, y);
                    });
}

void fit_system_camera(Editor &ed) {
  if (ed.selected >= ed.systems.size()) return;
  const auto &sys = ed.systems[ed.selected];
  const auto hosts = core::stellar_positions(sys, 0.0);
  double min_x = 0, max_x = 0, min_y = 0, max_y = 0;
  auto extend = [&](double x, double y, double r) {
    min_x = std::min(min_x, x - r);
    max_x = std::max(max_x, x + r);
    min_y = std::min(min_y, y - r);
    max_y = std::max(max_y, y + r);
  };
  const int star_count = sys.tertiary ? 3 : (sys.secondary ? 2 : 1);
  for (int i = 0; i < star_count; ++i)
    extend(hosts[static_cast<std::size_t>(i)][0],
           hosts[static_cast<std::size_t>(i)][1], 0.05);
  if (sys.stellar_orbits)
    for (std::size_t i = 0; i < sys.stellar_orbits->relative_orbits.size();
         ++i) {
      const auto &orbit = sys.stellar_orbits->relative_orbits[i];
      const double cx = i == 0 ? hosts[3][0] : 0.0;
      const double cy = i == 0 ? hosts[3][1] : 0.0;
      extend(cx, cy, orbit.radius * (1 + orbit.eccentricity));
    }
  if (const auto bodies = ed.bodies_by_system.find(sys.id);
      bodies != ed.bodies_by_system.end())
    for (const auto body_index : bodies->second) {
      const auto &body = ed.bodies[body_index];
      if (body.parent_body_id) continue;
      try {
        const int host = core::planetary_stellar_host(sys, body.id);
        const auto orbit = core::planetary_stellar_orbit(sys, body);
        const auto &hc = hosts[static_cast<std::size_t>(
            std::clamp(host, 0, 3))];
        extend(hc[0], hc[1], orbit.radius * (1 + orbit.eccentricity));
      } catch (const std::exception &) {
      }
    }
  if (sys.small_body_fields)
    for (const auto &field : *sys.small_body_fields) {
      if (field.planet_centered) continue;
      const int host =
          sys.stellar_orbits ? sys.stellar_orbits->belt_host : 0;
      const auto &hc =
          hosts[static_cast<std::size_t>(std::clamp(host, 0, 3))];
      extend(hc[0], hc[1], field.outer_radius_au);
    }
  ed.sys_camera = {static_cast<float>((min_x + max_x) * .5),
                   static_cast<float>((min_y + max_y) * .5)};
  const float span_x = std::max(0.1f, static_cast<float>(max_x - min_x));
  const float span_y = std::max(0.1f, static_cast<float>(max_y - min_y));
  ed.sys_ppa =
      std::clamp(std::min(ed.viewport.width / span_x,
                          ed.viewport.height / span_y) *
                     .9f,
                 0.5f, 2000.f);
}

// The system workspace: authoritative orbit rings, star positions, bodies,
// and small-body field bands for the selected system, in AU.
void render_system_view(DrawList &out, Editor &ed, float s) {
  const auto &v = ed.viewport;
  // All view content lives in the world layer (above the legacy clear —
  // the dark window fill doubles as the background — and below the
  // chrome overlay) so panels never cover it.
  if (ed.selected >= ed.systems.size()) {
    out.world.push_back(
        Text{{v.x + 20 * s, v.y + 20 * s}, "no system selected", muted,
             static_cast<int>(14 * s), 0, v});
    out.overlay.push_back(StrokedRectangle{v, panel_edge});
    return;
  }
  const auto &sys = ed.systems[ed.selected];
  const auto hosts = core::stellar_positions(sys, ed.system_days);

  // Companion relative orbits: inner pair around the AB barycentre, outer
  // around the system barycentre.
  if (sys.stellar_orbits)
    for (std::size_t i = 0; i < sys.stellar_orbits->relative_orbits.size();
         ++i) {
      const auto &hc = i == 0 ? hosts[3] : core::StellarPosition{0, 0, 0};
      try {
        orbit_ring(out, ed, sys.stellar_orbits->relative_orbits[i], hc[0],
                   hc[1], {70, 110, 135, 160});
      } catch (const std::exception &) {
      }
    }

  // Small-body field bands around their belt host.
  if (sys.small_body_fields)
    for (const auto &field : *sys.small_body_fields) {
      if (field.planet_centered) continue;
      const int host = sys.stellar_orbits ? sys.stellar_orbits->belt_host : 0;
      const auto &hc =
          hosts[static_cast<std::size_t>(std::clamp(host, 0, 3))];
      try {
        orbit_ring(out, ed,
                   engine::AnalyticOrbit{std::max(0.01, field.inner_radius_au),
                                         0, 0, 0, 0, 0, 1},
                   hc[0], hc[1], {45, 80, 100, 130});
        orbit_ring(out, ed,
                   engine::AnalyticOrbit{std::max(0.02, field.outer_radius_au),
                                         0, 0, 0, 0, 0, 1},
                   hc[0], hc[1], {45, 80, 100, 130});
      } catch (const std::exception &) {
      }
    }

  // Planetary orbit rings and body markers.
  if (const auto bodies = ed.bodies_by_system.find(sys.id);
      bodies != ed.bodies_by_system.end()) {
    for (const auto body_index : bodies->second) {
      const auto &body = ed.bodies[body_index];
      try {
        if (!body.parent_body_id) {
          const int host = core::planetary_stellar_host(sys, body.id);
          const auto &hc = hosts[static_cast<std::size_t>(
              std::clamp(host, 0, 3))];
          orbit_ring(out, ed, core::planetary_stellar_orbit(sys, body), hc[0],
                     hc[1], {60, 100, 122, 120});
        }
        const auto bp =
            core::stellar_planet_position(sys, body, ed.system_days);
        const auto p = system_to_screen(ed, bp[0], bp[1]);
        if (!v.contains(p)) continue;
        const bool in_hz =
            body.stellar_exposure && body.stellar_exposure->in_habitable_zone;
        if (body_index == ed.selected_body)
          out.world.push_back(Circle{p, 9.f, accent});
        if (const auto it = ed.body_edits.find(body.id);
            it != ed.body_edits.end() && it->second.bookmarked)
          out.world.push_back(Circle{p, 8.f, {240, 200, 90, 255}});
        out.world.push_back(
            Circle{p, body.parent_body_id ? 2.5f : 4.5f,
                   in_hz ? Color{110, 220, 140, 255}
                         : (body.parent_body_id ? muted : ink)});
        if (ed.sys_ppa > 4.f)
          out.world.push_back(
              Text{{p.x + 8, p.y - 7}, display_name(ed, body),
                   body_index == ed.selected_body
                       ? accent
                       : Color{150, 180, 195, 220},
                   static_cast<int>(11 * s), 0, v});
      } catch (const std::exception &) {
      }
    }
  }

  // Stars last so they sit above rings.
  const int star_count = sys.tertiary ? 3 : (sys.secondary ? 2 : 1);
  const std::array star_classes{sys.primary, sys.secondary, sys.tertiary};
  for (int i = 0; i < star_count; ++i) {
    const auto p = system_to_screen(
        ed, hosts[static_cast<std::size_t>(i)][0],
        hosts[static_cast<std::size_t>(i)][1]);
    const auto stellar_class = star_classes[static_cast<std::size_t>(i)];
    out.world.push_back(
        Circle{p, stellar_class ? class_radius(*stellar_class) * 2.6f : 8.f,
               stellar_class ? class_color(*stellar_class) : ink});
    if (v.contains(p))
      out.world.push_back(
          Text{{p.x + 12, p.y - 8}, core::stellar_host_name(i), accent,
               static_cast<int>(12 * s), 0, v});
  }
  out.overlay.push_back(StrokedRectangle{v, panel_edge});
  out.world.push_back(
      Text{{v.x + 10 * s, v.y + 8 * s},
           "day " + fspec("%.0f", ed.system_days), accent,
           static_cast<int>(13 * s), 0, v});
}

// Indices into `bodies` for satellites of `body_index`, catalog order.
std::vector<std::size_t> moon_indices(const Editor &ed,
                                      std::size_t body_index) {
  std::vector<std::size_t> out;
  if (body_index >= ed.bodies.size()) return out;
  const auto &body = ed.bodies[body_index];
  if (const auto it = ed.bodies_by_system.find(body.system_id);
      it != ed.bodies_by_system.end())
    for (const auto i : it->second)
      if (ed.bodies[i].parent_body_id == body.id) out.push_back(i);
  return out;
}

// Body-view space is kilometres relative to the focus body (its origin).
Point body_to_screen(const Editor &ed, double x_km, double y_km) {
  const auto &v = ed.viewport;
  return {v.x + v.width * .5f +
              static_cast<float>((x_km - ed.body_camera.x) * ed.body_ppa),
          v.y + v.height * .5f +
              static_cast<float>((y_km - ed.body_camera.y) * ed.body_ppa)};
}

// Centres the body view on the focus body and fits its outermost satellite
// orbit (or a fixed-radius fallback for bodies with no moons).
void fit_body_camera(Editor &ed) {
  if (ed.focus_body >= ed.bodies.size()) return;
  const auto &body = ed.bodies[ed.focus_body];
  const double radius_km = std::max(50.0, body.radius_earth * 6371.0);
  double extent = radius_km * 30.0;
  for (const auto mi : moon_indices(ed, ed.focus_body))
    try {
      const auto orbit =
          core::planetary_satellite_orbit(body, ed.bodies[mi]);
      extent = std::max(extent,
                        orbit.relative.radius * (1.0 + orbit.relative.eccentricity) * 1.05);
    } catch (const std::exception &) {
    }
  ed.body_camera = {0, 0};
  ed.body_ppa =
      std::clamp(std::min(ed.viewport.width, ed.viewport.height) * .42f /
                     static_cast<float>(extent),
                 1e-7f, 100.f);
}

// The body workspace: the focus body's satellite system in kilometres.
// Parent disc at the origin, moon orbit rings from the authoritative
// planetary_satellite_orbit, moon markers at satellite_relative_position
// for the current analytic day.
void render_body_view(DrawList &out, Editor &ed, float s) {
  const auto &v = ed.viewport;
  if (ed.focus_body >= ed.bodies.size() ||
      ed.selected >= ed.systems.size()) {
    out.world.push_back(
        Text{{v.x + 20 * s, v.y + 20 * s}, "no body selected", muted,
             static_cast<int>(14 * s), 0, v});
    out.overlay.push_back(StrokedRectangle{v, panel_edge});
    return;
  }
  const auto &body = ed.bodies[ed.focus_body];
  const auto to_screen = [&ed](double x, double y) {
    return body_to_screen(ed, x, y);
  };
  const auto moons = moon_indices(ed, ed.focus_body);

  // Moon orbit rings centred on the parent.
  for (const auto mi : moons)
    try {
      orbit_ring_mapped(out,
                        core::planetary_satellite_orbit(body, ed.bodies[mi])
                            .relative,
                        0, 0, {60, 100, 122, 120}, to_screen);
    } catch (const std::exception &) {
    }

  // Parent disc at its true angular size, floored for legibility.
  const auto pc = to_screen(0.0, 0.0);
  const float parent_px = std::max(
      5.f, static_cast<float>(body.radius_earth * 6371.0 * ed.body_ppa));
  const bool in_hz =
      body.stellar_exposure && body.stellar_exposure->in_habitable_zone;
  if (ed.focus_body == ed.selected_body)
    out.world.push_back(Circle{pc, parent_px + 6.f, accent});
  if (const auto it = ed.body_edits.find(body.id);
      it != ed.body_edits.end() && it->second.bookmarked)
    out.world.push_back(Circle{pc, parent_px + 4.f, {240, 200, 90, 255}});
  out.world.push_back(
      Circle{pc, parent_px, in_hz ? Color{110, 220, 140, 255} : ink});
  if (v.contains(pc))
    out.world.push_back(
        Text{{pc.x + parent_px + 8, pc.y - 8}, display_name(ed, body),
             accent, static_cast<int>(12 * s), 0, v});

  // Moon markers at their authoritative relative positions.
  for (const auto mi : moons) {
    const auto &moon = ed.bodies[mi];
    try {
      const auto orbit = core::planetary_satellite_orbit(body, moon);
      const auto rel =
          core::satellite_relative_position(orbit, ed.system_days);
      const auto p = to_screen(rel[0], rel[1]);
      const float moon_px = std::clamp(
          static_cast<float>(moon.radius_earth * 6371.0 * ed.body_ppa),
          2.5f, 10.f);
      if (mi == ed.selected_body)
        out.world.push_back(Circle{p, moon_px + 5.f, accent});
      if (const auto it = ed.body_edits.find(moon.id);
          it != ed.body_edits.end() && it->second.bookmarked)
        out.world.push_back(Circle{p, moon_px + 4.f, {240, 200, 90, 255}});
      const bool moon_hz =
          moon.stellar_exposure && moon.stellar_exposure->in_habitable_zone;
      out.world.push_back(
          Circle{p, moon_px,
                 moon_hz ? Color{110, 220, 140, 255} : muted});
      if (v.contains(p))
        out.world.push_back(
            Text{{p.x + moon_px + 6, p.y - 6}, display_name(ed, moon),
                 mi == ed.selected_body ? accent
                                        : Color{150, 180, 195, 220},
                 static_cast<int>(11 * s), 0, v});
    } catch (const std::exception &) {
    }
  }
  out.overlay.push_back(StrokedRectangle{v, panel_edge});
  out.world.push_back(
      Text{{v.x + 10 * s, v.y + 8 * s},
           display_name(ed, body) + "  |  day " +
               fspec("%.0f", ed.system_days),
           accent, static_cast<int>(13 * s), 0, v});
}

// Runs the same world-assembly pipeline as fresh campaign generation —
// systems, planetary bodies, stellar physics, orbit reconciliation,
// small-body fields, and stellar orbits — without civilization seeding.
// The editor inspects exactly what a real generated world contains.
std::pair<std::vector<core::StellarSystem>, std::vector<core::PlanetaryBody>>
generate(std::int64_t seed, int count,
         const std::vector<core::CatalogStar> &catalog) {
  auto systems = core::generate_stellar_catalog(seed, count, catalog);
  auto bodies = core::generate_planetary_catalog(seed, systems);
  const auto removed =
      core::apply_stellar_planetary_physics(systems, bodies);
  std::unordered_set<int> habitable_systems;
  for (const auto &body : bodies)
    if (body.legacy_colonization_candidate)
      habitable_systems.insert(body.system_id);
  for (auto &system : systems) {
    if (const auto found = removed.find(system.id); found != removed.end())
      system.engulfed_planets += found->second;
    system.has_habitable_world = habitable_systems.contains(system.id);
  }
  core::reconcile_frozen_planet_orbits(systems, bodies);
  core::initialize_small_body_fields(seed, systems, bodies, true);
  core::reconcile_small_body_orbits(systems, bodies);
  core::initialize_stellar_orbits(seed, systems, bodies, true);
  core::initialize_stellar_activity(seed, systems);
  return {std::move(systems), std::move(bodies)};
}

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
#else
int main(int argc, char **argv) {
#endif
  std::filesystem::path project_arg;
  for (int i = 1; i + 1 < argc; ++i)
    if (std::filesystem::path(argv[i]) == std::filesystem::path("--project"))
      project_arg = argv[++i];

  engine::RuntimeDiagnostics diagnostics("engine-editor",
                                         STELLAR_ENGINE_VERSION);
  try {
    engine::RuntimeDiagnostics::context("stellar engine editor");
    engine::JobSystem jobs;
    auto &profiler = engine::Profiler::instance();
    profiler.set_enabled(true);

    Editor ed;
    if (!project_arg.empty()) {
      // Launched on an engine project (from the engine shell): documents
      // live under <project>/editor/ and the manifest seeds the name.
      ed.projects_dir = project_arg / "editor";
      std::error_code ec;
      std::filesystem::create_directories(ed.projects_dir, ec);
      if (const auto manifest = engine::EngineProject::load(project_arg))
        ed.project_name = manifest->name;
      const auto slug = edproj::sanitize_project_name(ed.project_name);
      ed.project_path = ed.projects_dir /
                        ((slug.empty() ? "editor-project" : slug) + ".json");
      ed.status = "editing project " + ed.project_name;
    } else {
      ed.projects_dir = engine::executable_directory() / "projects";
      ed.project_path = ed.projects_dir / "editor-project.json";
    }
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
    std::vector<core::PlanetaryBody> result_bodies;
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
                          auto [systems, bodies] =
                              generate(seed, count, catalog);
                          const auto elapsed =
                              std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - begin)
                                  .count();
                          std::lock_guard lock(result_mutex);
                          result = std::move(systems);
                          result_bodies = std::move(bodies);
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
          ed.bodies = std::move(result_bodies);
          result_bodies.clear();
          ed.bodies_by_system.clear();
          for (std::size_t i = 0; i < ed.bodies.size(); ++i)
            ed.bodies_by_system[ed.bodies[i].system_id].push_back(i);
          ed.seed = result_seed;
          ed.system_count = result_count;
          ed.generate_ms = result_ms;
          ed.selected = static_cast<std::size_t>(-1);
          ed.selected_body = static_cast<std::size_t>(-1);
          ed.focus_body = static_cast<std::size_t>(-1);
          ed.view = WorkspaceView::Galaxy;
          // Annotations survive regeneration: ids are deterministic for the
          // same seed+count, and a new seed simply orphans old edits.
          rebuild_filter(ed);
          rebuild_detail_rows(ed);
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
          if (ed.picker_open) {
            ed.picker_open = false;
          } else if (ed.menu_open) {
            ed.menu_open = false;
          } else if (ed.editing != Field::None) {
            ed.editing = Field::None;
            window.set_text_input(false);
          } else if (ed.view == WorkspaceView::Body) {
            ed.view = WorkspaceView::System;
          } else if (ed.view == WorkspaceView::System) {
            ed.view = WorkspaceView::Galaxy;
            ed.selected_body = static_cast<std::size_t>(-1);
            rebuild_detail_rows(ed);
          } else {
            return 0;
          }
          continue;
        }
        // Ctrl+Z / Ctrl+Y (or Ctrl+Shift+Z) operate on the committed
        // annotation layer; an in-progress field commits first so its buffer
        // is never silently lost.
        if (event.type == InputEventType::KeyPressed && event.control &&
            (event.key == 'z' || event.key == 'y')) {
          commit_active_field(ed);
          window.set_text_input(false);
          if (event.key == 'y' || event.shift)
            apply_redo(ed);
          else
            apply_undo(ed);
          continue;
        }
        // File shortcuts mirror the FILE menu.
        if (event.type == InputEventType::KeyPressed && event.control &&
            (event.key == 's' || event.key == 'o' || event.key == 'n')) {
          commit_active_field(ed);
          window.set_text_input(false);
          run_menu_item(ed, window,
                        event.key == 's' ? 1 : (event.key == 'o' ? 3 : 0));
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
            commit_active_field(ed);
            window.set_text_input(false);
            continue;
          }
        }
        // Keyboard navigation: arrows scrub analytic time in the system
        // workspace and move selection through the filtered list otherwise.
        if (event.type == InputEventType::KeyPressed &&
            ed.editing == Field::None) {
          constexpr std::uint32_t key_left = 0x40000050,
                                  key_right = 0x4000004f,
                                  key_up = 0x40000052,
                                  key_down = 0x40000051,
                                  key_pageup = 0x4000004b,
                                  key_pagedown = 0x4000004e,
                                  key_home = 0x4000004a;
          if (ed.view != WorkspaceView::Galaxy) {
            if (event.key == key_left || event.key == key_right) {
              ed.system_days =
                  std::max(0.0, ed.system_days +
                                    (event.key == key_right ? 1.0 : -1.0) *
                                        (event.shift ? 10.0 : 1.0));
              continue;
            }
            if (event.key == key_pageup || event.key == key_pagedown) {
              ed.system_days =
                  std::max(0.0, ed.system_days +
                                    (event.key == key_pagedown ? 30.0
                                                               : -30.0));
              continue;
            }
            if (event.key == key_home) {
              ed.system_days = 0;
              continue;
            }
            // Up/down cycle through the system's bodies for quick inspection.
            if ((event.key == key_up || event.key == key_down) &&
                ed.selected < ed.systems.size()) {
              const auto &sys = ed.systems[ed.selected];
              if (const auto it = ed.bodies_by_system.find(sys.id);
                  it != ed.bodies_by_system.end() && !it->second.empty()) {
                const auto &list = it->second;
                std::size_t pos = list.size();
                for (std::size_t i = 0; i < list.size(); ++i)
                  if (list[i] == ed.selected_body) pos = i;
                pos = event.key == key_down
                          ? (pos >= list.size() ? 0 : (pos + 1) % list.size())
                          : (pos == 0 || pos >= list.size() ? list.size() - 1
                                                           : pos - 1);
                ed.selected_body = list[pos];
                rebuild_detail_rows(ed);
              }
              continue;
            }
          } else if ((event.key == key_up || event.key == key_down) &&
                     !ed.filtered.empty()) {
            std::size_t pos = ed.filtered.size();
            for (std::size_t i = 0; i < ed.filtered.size(); ++i)
              if (ed.filtered[i] == ed.selected) pos = i;
            if (event.key == key_down)
              pos = pos >= ed.filtered.size() ? 0
                                              : std::min(pos + 1,
                                                         ed.filtered.size() - 1);
            else
              pos = pos == 0 || pos >= ed.filtered.size()
                        ? ed.filtered.size() - 1
                        : pos - 1;
            ed.selected = ed.filtered[pos];
            ed.selected_body = static_cast<std::size_t>(-1);
            ed.system_list.ensure_visible(pos);
            rebuild_detail_rows(ed);
            continue;
          }
        }
        if (event.type == InputEventType::LeftPressed) {
          // Commit whatever field was being edited before handling the click,
          // so switching fields never silently drops the buffer.
          if (ed.editing != Field::None) {
            commit_active_field(ed);
            window.set_text_input(false);
          }
          if (ed.picker_open) {
            // The picker owns input while open; a click outside dismisses it.
            if (!ed.picker_rect.contains(event.position))
              ed.picker_open = false;
            continue;
          }
          if (ed.hit_file.contains(event.position)) {
            ed.menu_open = !ed.menu_open;
            continue;
          }
          if (ed.menu_open) {
            // The menu owns input while open; clicks run items or dismiss.
            for (std::size_t i = 0; i < ed.menu_item_rects.size(); ++i)
              if (ed.menu_item_rects[i].contains(event.position))
                run_menu_item(ed, window, i);
            ed.menu_open = false;
            continue;
          }
          if (ed.hit_regen.contains(event.position)) {
            submit_generate();
          } else if (ed.hit_seed.contains(event.position)) {
            pending_seed.store(std::random_device{}());
            submit_generate();
          } else if (ed.hit_view.contains(event.position)) {
            if (ed.view == WorkspaceView::Galaxy &&
                ed.selected < ed.systems.size()) {
              ed.view = WorkspaceView::System;
              ed.selected_body = static_cast<std::size_t>(-1);
              fit_system_camera(ed);
            } else if (ed.view == WorkspaceView::System) {
              if (ed.selected_body < ed.bodies.size()) {
                // Descend into the selected body's satellite system; a
                // moon focuses the view on its parent.
                ed.view = WorkspaceView::Body;
                ed.focus_body = ed.selected_body;
                if (const auto pid =
                        ed.bodies[ed.focus_body].parent_body_id)
                  for (std::size_t i = 0; i < ed.bodies.size(); ++i)
                    if (ed.bodies[i].id == *pid) ed.focus_body = i;
                fit_body_camera(ed);
              } else {
                ed.view = WorkspaceView::Galaxy;
                rebuild_detail_rows(ed);
              }
            } else if (ed.view == WorkspaceView::Body) {
              ed.view = WorkspaceView::Galaxy;
              ed.selected_body = static_cast<std::size_t>(-1);
              rebuild_detail_rows(ed);
            }
          } else if (ed.hit_save.contains(event.position)) {
            save_project(ed);
          } else if (ed.hit_load.contains(event.position)) {
            open_picker(ed);
          } else if (ed.hit_bookmark.contains(event.position)) {
            if (const auto target = annotation_target(ed)) {
              ed.history.commit(
                  {ed.seed, ed.system_count, ed.edits, ed.body_edits,
                   ed.project_name});
              auto &map = annotation_map(ed, target->second);
              map[target->first].bookmarked =
                  !map[target->first].bookmarked;
              rebuild_detail_rows(ed); // row bookmark markers refresh
              rebuild_filter(ed); // bookmarked-only view membership changes
            }
          } else if (ed.hit_anomaly.contains(event.position) ||
                     ed.hit_rare.contains(event.position) ||
                     ed.hit_prewarp.contains(event.position)) {
            if (const auto target = annotation_target(ed)) {
              ed.history.commit(
                  {ed.seed, ed.system_count, ed.edits, ed.body_edits,
                   ed.project_name});
              auto &map = annotation_map(ed, target->second);
              auto &field =
                  ed.hit_anomaly.contains(event.position)
                      ? map[target->first].anomaly
                      : ed.hit_rare.contains(event.position)
                            ? map[target->first].rare_resource
                            : map[target->first].pre_warp_civilization;
              // AUTO -> YES -> NO -> AUTO (unset restores the generated trait).
              field = !field ? std::optional<bool>{true}
                             : (*field ? std::optional<bool>{false}
                                       : std::nullopt);
              rebuild_detail_rows(ed); // trait markers refresh
            }
          } else if (ed.hit_undo.contains(event.position)) {
            apply_undo(ed);
          } else if (ed.hit_redo.contains(event.position)) {
            apply_redo(ed);
          } else if (ed.hit_name.contains(event.position)) {
            if (const auto target = annotation_target(ed)) {
              ed.editing = Field::Name;
              const auto &map = annotation_map(ed, target->second);
              const auto it = map.find(target->first);
              ed.edit_buffer =
                  it != map.end() ? it->second.name : std::string{};
              window.set_text_input(true);
            }
          } else if (ed.hit_note.contains(event.position)) {
            if (const auto target = annotation_target(ed)) {
              ed.editing = Field::Note;
              const auto &map = annotation_map(ed, target->second);
              const auto it = map.find(target->first);
              ed.edit_buffer =
                  it != map.end() ? it->second.note : std::string{};
              window.set_text_input(true);
            }
          } else if (ed.hit_project_name.contains(event.position)) {
            ed.editing = Field::ProjectName;
            ed.edit_buffer = ed.project_name;
            window.set_text_input(true);
          } else if (ed.hit_bkmk_filter.contains(event.position)) {
            ed.bookmark_only = !ed.bookmark_only;
            rebuild_filter(ed);
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
              ed.camera_origin =
                  ed.view == WorkspaceView::Galaxy   ? ed.camera
                  : ed.view == WorkspaceView::System ? ed.sys_camera
                                                     : ed.body_camera;
            }
          }
        }
        if (event.type == InputEventType::LeftReleased && ed.dragging) {
          ed.dragging = false;
          const float moved =
              std::abs(event.position.x - ed.drag_origin.x) +
              std::abs(event.position.y - ed.drag_origin.y);
          if (moved < 6.f && ed.viewport.contains(event.position)) {
            if (ed.view == WorkspaceView::Galaxy) {
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
              rebuild_detail_rows(ed);
            } else if (ed.view == WorkspaceView::Body &&
                       ed.focus_body < ed.bodies.size()) {
              // Body view: nearest moon marker or the focus body itself,
              // at the same analytic positions the renderer draws.
              const auto &parent = ed.bodies[ed.focus_body];
              float best = 14.f;
              std::size_t best_body = ed.bodies.size();
              const auto pc = body_to_screen(ed, 0.0, 0.0);
              if (const float d = std::hypot(pc.x - event.position.x,
                                             pc.y - event.position.y);
                  d < best) {
                best = d;
                best_body = ed.focus_body;
              }
              for (const auto mi : moon_indices(ed, ed.focus_body))
                try {
                  const auto rel = core::satellite_relative_position(
                      core::planetary_satellite_orbit(parent,
                                                      ed.bodies[mi]),
                      ed.system_days);
                  const auto p = body_to_screen(ed, rel[0], rel[1]);
                  const float d = std::hypot(p.x - event.position.x,
                                             p.y - event.position.y);
                  if (d < best) {
                    best = d;
                    best_body = mi;
                  }
                } catch (const std::exception &) {
                }
              if (best_body < ed.bodies.size()) {
                ed.selected_body = best_body;
                rebuild_detail_rows(ed);
              }
            } else if (ed.selected < ed.systems.size()) {
              // System view: nearest body marker within a bounded pick radius.
              const auto &sys = ed.systems[ed.selected];
              float best = 12.f;
              std::size_t best_body = ed.bodies.size();
              if (const auto it = ed.bodies_by_system.find(sys.id);
                  it != ed.bodies_by_system.end())
                for (const auto body_index : it->second) {
                  try {
                    const auto bp = core::stellar_planet_position(
                        sys, ed.bodies[body_index], ed.system_days);
                    const auto p = system_to_screen(ed, bp[0], bp[1]);
                    const float d = std::hypot(p.x - event.position.x,
                                               p.y - event.position.y);
                    if (d < best) {
                      best = d;
                      best_body = body_index;
                    }
                  } catch (const std::exception &) {
                  }
                }
              ed.selected_body = best_body;
              rebuild_detail_rows(ed);
            }
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
            ed.selected_body = static_cast<std::size_t>(-1);
            if (ed.view == WorkspaceView::System) fit_system_camera(ed);
            rebuild_detail_rows(ed);
          }
        }
        // Picker rows load the chosen project file.
        if (event.type == InputEventType::LeftReleased && ed.picker_open) {
          if (ed.picker_rows.contains(event.position)) {
            const float local = event.position.y - ed.picker_rows.y +
                                ed.picker_list.scroll_offset;
            const auto idx = static_cast<std::size_t>(
                std::max(0.f, std::floor(local / ed.picker_list.row_height)));
            if (idx < ed.project_files.size()) {
              load_project(ed, ed.project_files[idx]);
              ed.picker_open = false;
            }
          }
          continue; // swallow other release handling while the picker is up
        }
        // Detail-list body rows jump to that body's context.
        if (event.type == InputEventType::LeftReleased)
          for (const auto &[rect, body_index] : ed.detail_body_hits)
            if (rect.contains(event.position)) {
              ed.view = WorkspaceView::System;
              ed.selected_body = body_index;
              rebuild_detail_rows(ed);
              break;
            }
        if (event.type == InputEventType::Wheel && ed.picker_open &&
            ed.picker_rect.contains(event.position))
          ed.picker_list.scroll_to(ed.picker_list.scroll_offset -
                                   event.wheel_y * 40.f);
        if (event.type == InputEventType::Wheel &&
            ed.list_rect.contains(event.position))
          ed.system_list.scroll_to(ed.system_list.scroll_offset -
                                   event.wheel_y * 40.f);
        if (event.type == InputEventType::Wheel &&
            ed.detail_rect.contains(event.position))
          ed.detail_list.scroll_to(ed.detail_list.scroll_offset -
                                   event.wheel_y * 40.f);
        if (event.type == InputEventType::PointerMove && ed.dragging) {
          const float scale =
              ed.view == WorkspaceView::Galaxy   ? ed.pixels_per_unit
              : ed.view == WorkspaceView::System ? ed.sys_ppa
                                                 : ed.body_ppa;
          const Point target{
              ed.camera_origin.x -
                  (event.position.x - ed.drag_origin.x) / scale,
              ed.camera_origin.y -
                  (event.position.y - ed.drag_origin.y) / scale};
          if (ed.view == WorkspaceView::Galaxy)
            ed.camera = target;
          else if (ed.view == WorkspaceView::System)
            ed.sys_camera = target;
          else
            ed.body_camera = target;
        }
        if (event.type == InputEventType::Wheel &&
            ed.viewport.contains(event.position)) {
          float &scale =
              ed.view == WorkspaceView::Galaxy   ? ed.pixels_per_unit
              : ed.view == WorkspaceView::System ? ed.sys_ppa
                                                 : ed.body_ppa;
          const Point &cam =
              ed.view == WorkspaceView::Galaxy   ? ed.camera
              : ed.view == WorkspaceView::System ? ed.sys_camera
                                                 : ed.body_camera;
          const float before_x =
              cam.x + (event.position.x - ed.viewport.x -
                       ed.viewport.width * .5f) /
                          scale;
          const float before_y =
              cam.y + (event.position.y - ed.viewport.y -
                       ed.viewport.height * .5f) /
                          scale;
          scale = std::clamp(
              scale * (event.wheel_y > 0 ? 1.18f : 0.85f),
              ed.view == WorkspaceView::Body ? 1e-7f : 0.02f,
              ed.view == WorkspaceView::Galaxy   ? 400.f
              : ed.view == WorkspaceView::System ? 4000.f
                                                 : 100.f);
          const Point next{
              before_x - (event.position.x - ed.viewport.x -
                          ed.viewport.width * .5f) /
                             scale,
              before_y - (event.position.y - ed.viewport.y -
                          ed.viewport.height * .5f) /
                             scale};
          if (ed.view == WorkspaceView::Galaxy)
            ed.camera = next;
          else if (ed.view == WorkspaceView::System)
            ed.sys_camera = next;
          else
            ed.body_camera = next;
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
      draw.overlay.push_back(
          Text{{bar.x + 14 * s, bar.y + 14 * s}, "STELLAR ENGINE EDITOR", ink,
               static_cast<int>(20 * s), 0, std::nullopt, TextAlign::Left,
               FontFace::Heading});
      if (ed.emblem)
        draw.overlay.push_back(
            Image{ed.emblem, {bar.x + bar.width - 12 * s - 38 * s,
                              bar.y + 7 * s, 38 * s, 38 * s}});

      ed.hit_file = {bar.x + 235 * s, bar.y + 10 * s, 56 * s, 32 * s};
      draw.overlay.push_back(FilledRectangle{
          ed.hit_file, ed.menu_open ? row_selected : button_fill});
      draw.overlay.push_back(StrokedRectangle{ed.hit_file, panel_edge});
      draw.overlay.push_back(
          Text{{ed.hit_file.x + ed.hit_file.width * .5f,
                ed.hit_file.y + 9 * s},
               "FILE", ink, static_cast<int>(13 * s), ed.hit_file.width,
               ed.hit_file, TextAlign::Center});

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
        draw.overlay.push_back(
            Text{{button.x + button.width * .5f, button.y + 9 * s},
                 std::to_string(count), active ? ink : muted,
                 static_cast<int>(13 * s), button.width, button,
                 TextAlign::Center});
        bx += 84 * s;
      }
      ed.hit_regen = {bx, bar.y + 10 * s, 110 * s, 32 * s};
      draw.overlay.push_back(FilledRectangle{ed.hit_regen, button_fill});
      draw.overlay.push_back(StrokedRectangle{ed.hit_regen, panel_edge});
      draw.overlay.push_back(
          Text{{ed.hit_regen.x + ed.hit_regen.width * .5f,
                ed.hit_regen.y + 9 * s},
               "REGENERATE", ink, static_cast<int>(13 * s),
               ed.hit_regen.width, ed.hit_regen, TextAlign::Center});
      bx += 118 * s;
      ed.hit_seed = {bx, bar.y + 10 * s, 100 * s, 32 * s};
      draw.overlay.push_back(FilledRectangle{ed.hit_seed, button_fill});
      draw.overlay.push_back(StrokedRectangle{ed.hit_seed, panel_edge});
      draw.overlay.push_back(
          Text{{ed.hit_seed.x + ed.hit_seed.width * .5f,
                ed.hit_seed.y + 9 * s},
               "NEW SEED", ink, static_cast<int>(13 * s), ed.hit_seed.width,
               ed.hit_seed, TextAlign::Center});
      bx += 108 * s;
      // Workspace view toggle: system orbit view requires a selection.
      ed.hit_view = {bx, bar.y + 10 * s, 110 * s, 32 * s};
      const bool view_ready =
          ed.view != WorkspaceView::Galaxy ||
          ed.selected < ed.systems.size();
      draw.overlay.push_back(FilledRectangle{
          ed.hit_view,
          ed.view != WorkspaceView::Galaxy ? row_selected : button_fill});
      draw.overlay.push_back(StrokedRectangle{ed.hit_view, panel_edge});
      const char *view_label =
          ed.view == WorkspaceView::Galaxy   ? "SYSTEM VIEW"
          : ed.view == WorkspaceView::System
              ? (ed.selected_body < ed.bodies.size() ? "BODY VIEW"
                                                     : "GALAXY VIEW")
              : "GALAXY VIEW";
      draw.overlay.push_back(
          Text{{ed.hit_view.x + ed.hit_view.width * .5f,
                ed.hit_view.y + 9 * s},
               view_label,
               view_ready ? ink : muted, static_cast<int>(13 * s),
               ed.hit_view.width, ed.hit_view, TextAlign::Center});
      bx += 122 * s;
      draw.overlay.push_back(
          Text{{bx, bar.y + 18 * s}, "seed " + std::to_string(ed.seed), muted,
               static_cast<int>(13 * s)});
      bx += 90 * s;
      ed.hit_project_name = {bx, bar.y + 10 * s, 170 * s, 32 * s};
      field_box(draw, ed.hit_project_name,
                ed.editing == Field::ProjectName ? ed.edit_buffer
                                                 : ed.project_name,
                ed.editing == Field::ProjectName, "project name",
                static_cast<int>(13 * s));

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
      if (ed.view == WorkspaceView::System)
        render_system_view(draw, ed, s);
      else if (ed.view == WorkspaceView::Body)
        render_body_view(draw, ed, s);
      else
        render_viewport(draw, ed, s);
      render_inspector(draw, ed, s);
      render_system_list(draw, ed, s);
      draw.overlay.push_back(
          Text{{ed.viewport.x + 6 * s, ed.viewport.y + ed.viewport.height - 22 * s},
               ed.status +
                   (ed.view == WorkspaceView::System
                        ? "  |  drag pan, wheel zoom, click body, "
                              "up/down cycle bodies, left/right +/-1d, "
                              "pgup/pgdn +/-30d, home reset, esc galaxy"
                    : ed.view == WorkspaceView::Body
                        ? "  |  drag pan, wheel zoom, click moon, "
                              "left/right +/-1d, pgup/pgdn +/-30d, "
                              "home reset, esc system"
                        : "  |  drag to pan, wheel to zoom, click to select, "
                              "up/down moves selection"),
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

      render_menu(draw, ed, s);
      render_picker(draw, ed, s, w, h); // topmost modal
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
