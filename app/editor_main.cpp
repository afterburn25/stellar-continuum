// Stellar Engine Editor — native C++23 standalone editor host.
//
// Successor to the 0.1.9-era WPF editor (PR #326, work/stellar-engine-editor,
// never merged). This host links the shared engine and core libraries
// directly instead of driving a pinned runtime as a hidden child process.
// The galaxy tool generates through the same authoritative
// generate_stellar_catalog path the game uses — no duplicated generation.

#include "stellar/build_version.hpp"

#include <stellar/core/galaxy_catalog.hpp>
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
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace stellar::native_map;
namespace engine = stellar::engine;
namespace core = stellar::core;

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

  // Toolbar hit regions, rebuilt each frame.
  std::vector<UiRect> hits;
  std::vector<int> hit_sizes; // system count choices for the first N buttons
  UiRect hit_regen{}, hit_seed{};
  UiRect viewport{}, inspector{};

  std::shared_ptr<const RgbaImage> emblem;
};

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

void render_inspector(DrawList &out, Editor &ed, float s) {
  const auto &r = ed.inspector;
  out.overlay.push_back(FilledRectangle{r, {6, 16, 26, 255}});
  out.overlay.push_back(StrokedRectangle{r, panel_edge});
  float x = r.x + 14 * s, y = r.y + 12 * s;
  const int font = static_cast<int>(13 * s);
  out.text.push_back(Text{{x, y}, "SYSTEM", accent, font + 2, 0, std::nullopt,
                          TextAlign::Left, FontFace::Heading});
  y += (font + 14) * s;
  if (ed.selected >= ed.systems.size()) {
    out.text.push_back(Text{{x, y}, "No system selected", muted, font});
    return;
  }
  const auto &sys = ed.systems[ed.selected];
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
  if (sys.stellar_object) {
    const auto &star = *sys.stellar_object;
    line(out, x, y, "mass", std::to_string(star.mass_solar), font);
    line(out, x, y, "luminosity", std::to_string(star.luminosity_solar), font);
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
        Text{{p.x + 14, p.y - 8}, sys.name, accent, static_cast<int>(13 * s), 0,
             v});
  }
  if (ed.pixels_per_unit > 6.f)
    for (const auto &sys : ed.systems) {
      const auto p = world_to_screen(ed, sys.position.x, sys.position.y);
      if (!v.contains(p)) continue;
      out.text.push_back(Text{{p.x + 7, p.y - 6}, sys.name,
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
          ed.generate_ms = result_ms;
          ed.selected = static_cast<std::size_t>(-1);
          ed.system_list.row_count = ed.systems.size();
          ed.system_list.scroll_to(0);
          fit_camera(ed);
          ed.status = "ready - " + std::to_string(ed.systems.size()) +
                      " systems in " + std::to_string(ed.generate_ms) + " ms";
        }
      }

      for (const auto &event : snapshot.events) {
        if (event.type == InputEventType::EscapePressed) return 0;
        if (event.type == InputEventType::LeftPressed) {
          if (ed.hit_regen.contains(event.position)) {
            submit_generate();
          } else if (ed.hit_seed.contains(event.position)) {
            pending_seed.store(std::random_device{}());
            submit_generate();
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

      // Workspace: viewport + inspector.
      ed.inspector = {w - 320 * s, bar.y + bar.height + 10 * s, 304 * s,
                      h - bar.height - 46 * s};
      ed.viewport = {16 * s, bar.y + bar.height + 10 * s,
                     w - 340 * s - 32 * s, ed.inspector.height};

      profiler.begin_frame();
      const auto frame_scope = profiler.span("editor.frame", "frame");
      render_viewport(draw, ed, s);
      render_inspector(draw, ed, s);
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
