// Standalone Stellar Engine shell and tools host.
//
// This executable deliberately links only the engine libraries
// (stellar_engine + stellar_native_platform). It owns no game module: it
// opens an engine window through the engine platform layer, drives the job
// system, profiler, localization table and UI view models, and renders a
// multi-tool engine workspace. It is the runnable proof that the engine is
// reusable without the Stellar Continuum game code, and the foundation the
// standalone editor grows from.

#include "stellar/build_version.hpp"

#if defined(_WIN32)
#include <stellar/engine/asset_cooker.hpp>
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif
#include <stellar/engine/asset_registry.hpp>
#include <stellar/engine/content_resolver.hpp>
#include <stellar/engine/foundation.hpp>
#include <stellar/engine/mesh3d_loader.hpp>
#include <stellar/engine/scene_components.hpp>
#include <stellar/engine/scene_document.hpp>
#include <stellar/engine/localization.hpp>
#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/native_scene3d.hpp>
#include <stellar/engine/package.hpp>
#include <stellar/engine/profiler.hpp>
#include <stellar/engine/project.hpp>
#include <stellar/engine/population.hpp>
#include <stellar/engine/colony.hpp>
#include <stellar/engine/economy_catalog.hpp>
#include <stellar/engine/resource_economy.hpp>
#include <stellar/engine/strategic_ai.hpp>
#include <stellar/engine/warfare.hpp>
#include <stellar/engine/mission_graph.hpp>
#include <stellar/engine/event_bus.hpp>
#include <stellar/engine/physics.hpp>
#include <stellar/engine/terraforming.hpp>
#include <stellar/engine/flow_network.hpp>
#include <stellar/engine/logistics.hpp>
#include <stellar/engine/galaxy_map.hpp>
#include <stellar/engine/simulation_executor.hpp>
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
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using namespace stellar::native_map;
namespace engine = stellar::engine;

constexpr Color ink{210, 230, 244, 255};
constexpr Color muted{122, 170, 190, 255};
constexpr Color accent{86, 196, 255, 255};
constexpr Color panel_fill{8, 22, 34, 235};
constexpr Color panel_edge{28, 64, 88, 255};
constexpr Color bar_fill{24, 90, 120, 255};
constexpr Color row_hover{16, 40, 56, 255};
constexpr Color row_selected{22, 62, 92, 255};

enum class Tool { Projects, Dashboard, Scene, Scene3D, Assets, Profiler,
                  Localization, Simulation, Colony, Economy, Planet,
                  Ai, Warfare, Missions, Physics, Galaxy };
constexpr std::array kTools{Tool::Projects, Tool::Dashboard, Tool::Scene,
                            Tool::Scene3D, Tool::Assets, Tool::Profiler,
                            Tool::Localization, Tool::Simulation,
                            Tool::Colony, Tool::Economy, Tool::Planet,
                            Tool::Ai, Tool::Warfare, Tool::Missions,
                            Tool::Physics, Tool::Galaxy};
constexpr std::array<const char *, 16> kToolNames{
    "Projects", "Dashboard", "Scene",     "Scene3D",
    "Assets",   "Profiler",  "Localization", "Simulation", "Colony",
    "Economy",  "Planet",    "AI",        "Warfare",   "Missions",
    "Physics",  "Galaxy"};

std::filesystem::path find_path(const char *relative) {
  // Beside the executable first (packaged layout), then upward so a
  // build-tree run reaches the shared assets/data directories.
  auto base = engine::executable_directory();
  for (int depth = 0; depth < 4; ++depth) {
    const auto candidate = base / relative;
    if (std::filesystem::exists(candidate)) return candidate;
    if (!base.has_parent_path() || base == base.parent_path()) break;
    base = base.parent_path();
  }
  return relative;
}

std::string ms(double value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.2f ms", value);
  return buffer;
}

// Last non-empty line of a (possibly still-being-written) log file, bounded
// to the tail 4 KiB — used to stream subprocess progress into the status.
std::string last_log_line(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) return {};
  const auto size = in.tellg();
  if (size <= 0) return {};
  const auto span = std::min<std::streamoff>(size, 4096);
  in.seekg(size - span);
  std::string buffer(static_cast<std::size_t>(span), '\0');
  in.read(buffer.data(), span);
  const auto end = buffer.find_last_not_of("\r\n \t");
  if (end == std::string::npos) return {};
  buffer.erase(end + 1);
  const auto start = buffer.find_last_of("\r\n");
  if (start != std::string::npos) buffer.erase(0, start + 1);
  if (buffer.size() > 90) buffer = "..." + buffer.substr(buffer.size() - 87);
  return buffer;
}

std::string human_bytes(std::uintmax_t bytes) {
  if (bytes >= 1024ull * 1024ull)
    return std::to_string(bytes / 1048576ull) + " MiB";
  if (bytes >= 1024ull) return std::to_string(bytes / 1024ull) + " KiB";
  return std::to_string(bytes) + " B";
}

bool is_image(const std::filesystem::path &path) {
  auto ext = path.extension().string();
  std::ranges::transform(ext, ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp";
}

struct Shell {
  Tool tool{Tool::Dashboard};
  std::vector<UiRect> tool_hits;
  float pointer_x{}, pointer_y{};

  // Asset browser.
  std::filesystem::path asset_root;
  std::vector<std::filesystem::path> asset_files;
  engine::VirtualizedList asset_list;
  std::size_t selected_asset{static_cast<std::size_t>(-1)};
  std::shared_ptr<const RgbaImage> preview;
  std::string preview_label;
  std::filesystem::path previewed_path;
  // Cooked view: lists the open project's runtime.stmanifest records.
  bool show_cooked{};
  std::vector<engine::AssetRecord> cooked_records;
  std::atomic<bool> cooked_dirty{true};
  UiRect hit_cooked_toggle{};

  // Localization inspector.
  engine::VirtualizedList key_list;
  std::vector<std::string> sample_keys;

  // Projects tool: directories containing project.stellar.json under
  // `projects_root`, plus the currently open project's package state.
  std::filesystem::path projects_root;
  std::vector<std::filesystem::path> projects;
  engine::VirtualizedList project_list;
  std::size_t selected_project{static_cast<std::size_t>(-1)};
  std::optional<engine::EngineProject> project;
  engine::PackageRegistry package_registry;
  // Tracked handle of the launched host process (Windows HANDLE).
  void *run_process{};
  // New projects scaffold with the console ("blank") starter when set.
  bool blank_template{};
  engine::PackageLoadPlan load_plan;
  std::vector<std::string> package_errors;
  // Profiler tool: two capture slots compared via compare_captures —
  // slots fill in-memory (CAPTURE) or from disk (LOAD/SAVE under
  // <exe>/profiler_captures/) so builds can diff against saved baselines.
  std::optional<engine::ProfileCapture> prof_capture_a, prof_capture_b;
  std::string prof_status;
  UiRect hit_prof_cap_a{}, hit_prof_cap_b{}, hit_prof_save_a{},
      hit_prof_save_b{}, hit_prof_load_a{}, hit_prof_load_b{};
  // New-project name field and panel hit regions.
  bool editing_project_name{}, editing_import{}, editing_package{};
  std::string project_name_buffer, import_buffer, package_buffer;
  UiRect hit_project_name{}, hit_project_create{}, hit_project_open{},
      hit_project_close{}, hit_project_cook{}, hit_project_build{},
      hit_project_run{}, hit_project_editor{}, hit_project_package{},
      hit_project_rename{}, hit_project_test{}, hit_template_toggle{},
      hit_import_field{},
      hit_import_button{}, hit_package_field{}, hit_package_button{};
  UiRect project_rows{};
  // Content cooking, host builds and packaging run on the JobSystem; the
  // UI thread reads their status under the mutex.
  std::atomic<bool> cooking{}, building{}, packaging{}, testing{};
  std::atomic<std::size_t> cook_done{}, cook_total{};
  std::mutex project_mutex;
  std::string cook_status, build_status, package_status, test_status;
  // Content staleness: newest source write vs cooked manifest time,
  // rescanned at most once per second on the UI thread.
  std::string content_status;
  std::chrono::steady_clock::time_point content_scan_at{};

  // Scene tool: authors <project>/editor/scene.json — the starter host
  // spawns these entities into its World at launch.
  engine::SceneDocument scene_doc;
  std::atomic<bool> scene_dirty{true};
  std::size_t selected_entity{static_cast<std::size_t>(-1)};
  engine::VirtualizedList entity_list;
  bool scene_modified{};
  bool editing_scene{};
  int scene_field{}; // 1=name, 2=pos, 3=vel, 4=sprite, 5=size, 6=color
  std::string scene_buffer;
  // Bounded whole-document undo: every mutation commits the pre-state.
  engine::UndoHistory<engine::SceneDocument> scene_history{64};
  UiRect hit_scene_undo{}, hit_scene_redo{}, hit_scene_dup{};
  UiRect hit_scene_add{}, hit_scene_del{}, hit_scene_save{},
      hit_scene_name{}, hit_scene_pos{}, hit_scene_vel{}, hit_scene_sprite{},
      hit_scene_size{}, hit_scene_color{}, hit_scene_layer{},
      hit_scene_parallax{}, hit_scene_text{}, hit_scene_grav{},
      hit_scene_gravity{}, hit_scene_solid{}, hit_scene_bg{},
      hit_scene_flipx{}, hit_scene_flipy{}, hit_scene_visible{},
      hit_scene_oneway{}, hit_scene_up{}, hit_scene_down{},
      hit_scene_data{}, hit_scene_opacity{}, hit_scene_parent{},
      hit_scene_fcols{}, hit_scene_animloop{}, hit_scene_vfx{},
      hit_scene_frames{}, hit_scene_fps{}, hit_scene_rot{},
      hit_scene_ttl{}, hit_scene_tilemap{}, hit_scene_tilesel{},
      hit_scene_tiledel{}, hit_scene_tileset{},
      hit_scene_tilesize{}, hit_scene_tilecols{},
      hit_scene_tilecollide{}, hit_scene_tilelayer{},
      hit_scene_tilepar{}, hit_scene_tilecells{},
      hit_scene_tileorigin{}, hit_scene_tilename{},
      hit_scene_paint{}, hit_scene_paintcell{}, hit_scene_brushsz{},
      hit_scene_fill{}, hit_scene_music{},
      hit_scene_spin{}, hit_scene_worldsize{}, hit_scene_bounce{},
      scene_preview{}, scene_rows{};
  // Decoded scene sprites keyed by resolved content path; cleared on
  // document reload so re-imported art refreshes.
  std::unordered_map<std::string, std::shared_ptr<const RgbaImage>>
      scene_sprites;
  bool scene_dragging{}; // pointer is dragging an entity in the preview
  UiRect scene_sheet_rect{}; // tile picker strip drawn in paint mode
  float scene_sheet_scale{1.f};
  int scene_sheet_cols{0};
  bool scene_paint{};    // PAINT mode: clicks write cells, not select
  bool scene_painting{}; // pointer is mid paint stroke
  int scene_paint_cell{}; // brush value written into tilemap cells
  int scene_paint_brush{1}; // NxN cells per stamp, centered on the click
  bool scene_paint_fill{}; // FILL mode: clicks flood a connected region
  // Which document tilemap the tile fields/paint mode edit — scenes can
  // stack several grids (decor, collision, foreground) on own layers.
  std::size_t scene_tile_index{};

  // Scene3D tool: authors <project>/editor/scene3d.json — the document
  // --scene3d hosts load. The preview builds a live Scene3D from the doc
  // every frame; right-drag orbits the authored camera, wheel tunes fov,
  // left click ray-picks the entity under the cursor.
  engine::Scene3dDocument scene3_doc;
  std::atomic<bool> scene3_dirty{true};
  bool scene3_modified{};
  bool editing_scene3{};
  int scene3_field{0};
  std::string scene3_buffer;
  std::size_t scene3_sel{static_cast<std::size_t>(-1)};
  engine::VirtualizedList scene3_list;
  engine::UndoHistory<engine::Scene3dDocument> scene3_history{64};
  std::unordered_map<std::string,
                     std::shared_ptr<const stellar::native_map::Mesh3D>>
      scene3_meshes;
  // Decoded scene3d textures arrive through a shared state so misses can
  // decode on the JobSystem instead of stalling the draw thread; `done`
  // is set release/acquire after `image`.
  struct Scene3Tex {
    std::shared_ptr<const RgbaImage> image;
    std::string error;                 // job-written before done
    std::atomic<bool> done{false};
    bool reported{false};              // UI-thread: error surfaced once
  };
  std::unordered_map<std::string, std::shared_ptr<Scene3Tex>>
      scene3_textures;
  engine::JobSystem *jobs{}; // main() wires the shell's JobSystem once
  std::unique_ptr<engine::ContentResolver> scene3_content;
  bool scene3_looking{};  // right-drag orbit is active in the preview
  bool scene3_dragging{}; // left-drag applies the transform mode below
                          // to the picked entity
  int scene3_drag_mode{0}; // 0 move (Y-plane), 1 rotate (yaw/pitch),
                           // 2 scale (uniform)
  UiRect scene3_preview{}, scene3_rows{};
  UiRect hit3_mode_move{}, hit3_mode_rot{}, hit3_mode_scale{};
  UiRect hit3_add{}, hit3_del{}, hit3_save{}, hit3_undo{}, hit3_redo{},
      hit3_dup{}, hit3_name{}, hit3_mesh{}, hit3_pos{}, hit3_rot{},
      hit3_scale{}, hit3_vel{}, hit3_color{}, hit3_tex{},
      hit3_opacity{}, hit3_dbl{}, hit3_solid{}, hit3_gravs{},
      hit3_ttl{}, hit3_data{}, hit3_parent{}, hit3_vfx{}, hit3_cam{},
      hit3_camrot{}, hit3_fov{}, hit3_clip{}, hit3_lightdir{}, hit3_lightint{},
      hit3_grav{}, hit3_ground{}, hit3_bounds{}, hit3_bg{},
      hit3_music{}, hit3_filla_dir{}, hit3_filla_tint{},
      hit3_fillb_dir{}, hit3_fillb_tint{}, hit3_pbr{}, hit3_mr{},
      hit3_emis{}, hit3_emit{}, hit3_night{}, hit3_env{},
      hit3_envstr{}, hit3_cutout{}, hit3_tile{}, hit3_atmo{},
      hit3_atmotint{}, hit3_exposure{}, hit3_bloom{}, hit3_grade{},
      hit3_quality{}, hit3_plights{}, hit3_debug{}, hit3_range{},
      hit3_shadow{}, hit3_surfmaps{}, hit3_surfshape{}, hit3_clouddeck{},
      hit3_termwrap{}, hit3_limbdark{}, hit3_lods{}, hit3_lodpixels{},
      hit3_bandshear{}, hit3_bandwaves{}, hit3_banddrift{},
      hit3_orbitbeam{},
      hit3_starkelvin{}, hit3_accretion{}, hit3_fwdscatter{},
      hit3_volume{}, hit3_lodfade{}, hit3_visfade{}, hit3_lodgroup{};

  // Simulation tool: a live engine::SimulationExecutor driving real
  // framework state (per-settlement Population cohorts, a shared power
  // FlowNetwork and a LogisticsNetwork freight route) — the visible
  // proof of the engine's simulation LOD machinery. Tasks span Active /
  // Normal / Dormant tiers; STEP advances one tick, RUN auto-advances,
  // WAKE exercises the event-wakeup path on the dormant relay.
  struct SimDemo {
    bool initialized{false};
    engine::SimulationExecutor executor;
    std::vector<engine::Population> settlements;
    engine::FlowNetwork power{"power"};
    engine::LogisticsNetwork freight;
    double relay_pings{0.0};
    double delivered{0.0};
    bool running{false};
    double run_accum{0.0};
    engine::SimulationStepReport last{};
    std::size_t selected{0};
  } sim;
  UiRect hit_sim_step{}, hit_sim_run{}, hit_sim_wake{}, hit_sim_tier{},
      hit_sim_list{};

  // Colony tool: a live engine::Colony designer — author districts and
  // structures from the spec catalog, advance construction/operations
  // over days, and watch jobs/housing/utilities/upkeep resolve against a
  // real Inventory stockpile. Demonstrates the specialization colony
  // framework end to end (specs -> construction -> operation ->
  // shortfalls), not a mockup.
  struct ColonyDemo {
    bool initialized{false};
    engine::Colony colony;
    engine::Inventory stockpile{10000.0};
    std::uint64_t next_id{1};
    double workers{400.0};
    double maintenance{1.0};
    double day{0.0};
    engine::ColonyDelta last{};
    // Flattened row model rebuilt each render: districts first, each
    // followed by its hosted structures, then standalone structures.
    // (is_district, id) — selection + hit testing index into this.
    std::vector<std::pair<bool, std::uint64_t>> rows;
    std::size_t selected{0};
    std::string notice;
  } col;
  UiRect hit_col_step{}, hit_col_run30{}, hit_col_workers_dn{},
      hit_col_workers_up{}, hit_col_maint{}, hit_col_resupply{},
      hit_col_enable{}, hit_col_demolish{};
  std::vector<UiRect> hit_col_build;
  std::vector<UiRect> hit_col_rows;

  // Economy tool: a catalog/network inspector over the specialization
  // economy framework — a fixed EconomyCatalog (ResourceSpec +
  // RecipeSpec rows) validated on demand, bridged into a live
  // ResourceNetwork (two nodes, producers, a transfer lane) that
  // advances over days, with analyze_economy demand/bottleneck
  // diagnostics rolled up from real network state. A BREAK toggle
  // injects a dangling recipe to exercise the validation diagnostics.
  struct EconomyDemo {
    bool initialized{false};
    engine::EconomyCatalog catalog;
    engine::ResourceNetwork network;
    std::uint64_t smelter_producer{0};
    bool validated{false};
    bool break_catalog{false};
    std::vector<engine::ValidationIssue> issues;
    bool analyzed{false};
    std::vector<engine::EconomyDiagnostic> diagnostics;
    double day{0.0};
  } eco;
  UiRect hit_eco_validate{}, hit_eco_break{}, hit_eco_analyze{},
      hit_eco_step{}, hit_eco_run10{}, hit_eco_producer{};

  // Planet tool: habitability + terraforming over the specialization
  // planetary framework — a PlanetEnvironment with editable
  // temperature/atmosphere/gravity/water, evaluated live against
  // selectable HabitabilityProfiles, with real Terraforming projects
  // (staged linear deltas + discrete tag application) advancing the
  // environment over days.
  struct PlanetDemo {
    bool initialized{false};
    engine::Terraforming terra;
    int profile{0};
    double day{0.0};
    engine::TerraformAdvance last{};
  } planet;
  enum : std::size_t { kPlanTemp, kPlanAtm, kPlanGrav, kPlanWater,
                       kPlanParamCount };
  std::array<UiRect, kPlanParamCount * 2> hit_plan_param{};
  UiRect hit_plan_profile{}, hit_plan_start{}, hit_plan_cancel{},
      hit_plan_step{}, hit_plan_run30{}, hit_plan_project{};
  int planet_project{0};

  // AI tool: a StrategicMind debugger — a small deterministic world
  // (minerals/mines/fleets/threat drift per day) with four registered
  // UtilityActions across economy and military domains. DECIDE runs
  // decide() at the campaign clock, RUN auto-advances, action rows
  // toggle enabled live, and the bounded decision journal renders WHY
  // the AI acted (utility, candidates, incumbent switches).
  struct AiDemo {
    bool initialized{false};
    engine::StrategicMind mind{64};
    double day{0.0};
    double minerals{80.0};
    double mines{1.0};
    double tech{0.0};
    double fleets{1.0};
    double forts{0.0};
    double threat{25.0};
    bool running{false};
    double run_accum{0.0};
  } ai;
  UiRect hit_ai_decide{}, hit_ai_run{}, hit_ai_threat_dn{},
      hit_ai_threat_up{}, hit_ai_reset{};
  std::vector<UiRect> hit_ai_actions;

  // Warfare tool: a theater inspector over WarfareModel — two hostile
  // fleets plus an interdictor on a strategic plane. Rows select a
  // fleet; ORDER cycles its order kind (Move steers at the hostile
  // fleet); ENGAGE resolves deterministic Lanchester attrition; STEP/RUN
  // advance movement + supply burn at the theater clock.
  struct WarfareDemo {
    bool initialized{false};
    engine::WarfareModel model;
    double day{0.0};
    bool running{false};
    double run_accum{0.0};
    std::uint64_t selected{1};
    std::string last_engagement{"none"};
  } war;
  UiRect hit_war_step{}, hit_war_run{}, hit_war_order{}, hit_war_engage{},
      hit_war_reset{};
  std::vector<UiRect> hit_war_fleets;

  // Missions tool: a MissionRuntime debugger over a real EventBus — two
  // data-driven mission definitions parsed from JSON, canned domain
  // events fired through handle_event, stage timers advanced on the
  // demo clock, choices applied through choose(), and every
  // MissionEffectEvent the runtime publishes captured into a log.
  struct MissionDemo {
    bool initialized{false};
    engine::EventBus bus;
    engine::MissionRuntime runtime{&bus};
    engine::Subscription effects_sub;
    double day{0.0};
    std::size_t event_cursor{0};
    std::uint64_t selected{0};
    bool running{false};
    double run_accum{0.0};
    std::string saved_state;
    std::vector<std::string> log;
  } missions;
  UiRect hit_mis_fire{}, hit_mis_step{}, hit_mis_run{}, hit_mis_choose{},
      hit_mis_save{}, hit_mis_load{}, hit_mis_reset{};
  std::vector<UiRect> hit_mis_instances;

  // Physics tool: a PhysicsWorld inspector — circles/AABBs with layers,
  // a drifting mover crossing a trigger volume, raycast and swept-circle
  // queries against the live broadphase, and the trigger enter/exit
  // event log produced by advance().
  struct PhysicsDemo {
    bool initialized{false};
    engine::PhysicsWorld world{128.0f};
    double seconds{0.0};
    bool running{false};
    double run_accum{0.0};
    engine::PhysicsBodyId selected{0};
    engine::PhysicsBodyId mover{0};
    engine::PhysicsBodyId target{0};
    std::string last_query{"none"};
    std::vector<std::string> log;
  } phys;
  UiRect hit_phys_step{}, hit_phys_run{}, hit_phys_ray{},
      hit_phys_sweep{}, hit_phys_reset{};
  std::vector<UiRect> hit_phys_bodies;

  // Galaxy tool: a GalaxyMap debugger — a deterministic synthetic star
  // chart with lanes, colony markers and fleet travellers that move
  // system-to-system along the lane graph as days advance. Click to
  // select the nearest system, wheel zooms the chart.
  struct GalaxyDemo {
    bool initialized{false};
    engine::GalaxyMap map;
    double day{0.0};
    bool running{false};
    double run_accum{0.0};
    std::uint64_t selected{0};
    std::uint64_t destination{0}; // right-click sets a route target
    float zoom{1.0f};
    // Fleet markers in transit between neighbor systems.
    struct Traveller {
      std::uint64_t marker{0};
      std::uint64_t from{0}, to{0};
      double progress{0.0}; // light-years covered along the leg
      std::size_t next_hop{0};
    };
    std::vector<Traveller> travellers;
  } gal;
  UiRect hit_gal_step{}, hit_gal_run{}, hit_gal_reset{}, hit_gal_map{};
  std::string status{"ready"};
};

void line(DrawList &out, float x, float &y, std::string label,
          std::string value, int font = 14) {
  out.overlay.push_back(Text{{x, y}, std::move(label), muted, font});
  out.overlay.push_back(Text{{x + 210.f, y}, std::move(value), ink, font});
  y += font + 8.f;
}

void heading(DrawList &out, float x, float &y, const std::string &title) {
  out.overlay.push_back(Text{{x, y}, title, accent, 15, 0, std::nullopt,
                          TextAlign::Left, FontFace::Heading});
  y += 26.f;
}

// Loads the open project's cooked manifest (build/cooked/Content/
// runtime.stmanifest) into cooked_records for the Assets tool's cooked view.
void load_cooked(Shell &shell) {
  shell.cooked_records.clear();
  if (!shell.project) return;
  const auto manifest = shell.project->root / "build" / "cooked" / "Content" /
                        "runtime.stmanifest";
  if (!std::filesystem::is_regular_file(manifest)) return;
  try {
    const engine::AssetRegistry registry(manifest);
    shell.cooked_records = registry.records();
  } catch (const std::exception &) {
  }
}

void scan_assets(Shell &shell, std::filesystem::path root) {
  shell.asset_root = std::move(root);
  shell.asset_files.clear();
  std::error_code ec;
  if (!std::filesystem::is_directory(shell.asset_root, ec)) return;
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(shell.asset_root, ec)) {
    if (entry.is_regular_file(ec))
      shell.asset_files.push_back(
          std::filesystem::relative(entry.path(), shell.asset_root, ec));
    if (shell.asset_files.size() >= 20000) break;
  }
  std::ranges::sort(shell.asset_files);
  shell.asset_list.row_height = 22.f;
  shell.asset_list.set_row_count(shell.asset_files.size());
}

// Projects tool -----------------------------------------------------------
//
// A game project is a directory with project.stellar.json (engine/project).
// Opening one loads its manifest, scans its content roots into the package
// registry, resolves the load plan, and re-roots the asset browser at the
// project's own content so the shell works on the new game rather than on
// the host repository's assets.

void refresh_projects(Shell &shell) {
  shell.projects = engine::find_projects(shell.projects_root);
  shell.project_list.row_height = 24.f;
  shell.project_list.set_row_count(shell.projects.size());
  if (shell.selected_project >= shell.projects.size())
    shell.selected_project = shell.projects.empty()
                                 ? static_cast<std::size_t>(-1)
                                 : shell.projects.size() - 1;
}

// Rebuilds the package registry for the open project: its own content
// roots register first, the project namespace is then protected, and
// mod packages under mods/ scan last so they cannot override the base.
std::size_t reload_packages(Shell &shell) {
  shell.package_registry = engine::PackageRegistry{};
  shell.package_errors.clear();
  std::size_t count = 0;
  for (const auto &dir : shell.project->content_dirs)
    count += engine::scan_packages(shell.package_registry,
                                   (shell.project->root / dir).string(),
                                   &shell.package_errors);
  shell.package_registry.protect_namespace(shell.project->id);
  count += engine::scan_packages(shell.package_registry,
                                 (shell.project->root / "mods").string(),
                                 &shell.package_errors);
  shell.load_plan = shell.package_registry.resolve();
  return count;
}

void open_project(Shell &shell, const std::filesystem::path &root) {
  std::string error;
  auto loaded = engine::EngineProject::load(root, &error);
  if (!loaded) {
    shell.status = "open failed: " + error;
    return;
  }
  shell.project = std::move(*loaded);
  shell.cooked_dirty = true;
  shell.scene_dirty = true;
  shell.scene3_dirty = true;
  shell.scene3_content.reset(); // rebuilt lazily for the new project
  const auto count = reload_packages(shell);
  scan_assets(shell, root / shell.project->content_dirs.front());
  shell.status = "opened " + shell.project->name + " - " +
                 std::to_string(count) + " package(s), load plan " +
                 (shell.load_plan.ok ? "ok" : "FAILED");
}

// Scaffolds an additional content package under the open project's
// namespace (packages/<project>.<name>/) depending on the base package,
// then rebuilds the registry and load plan.
void create_package(Shell &shell) {
  if (!shell.project) {
    shell.status = "open a project before adding packages";
    return;
  }
  const std::string name =
      shell.package_buffer.empty() ? "content" : shell.package_buffer;
  const auto slug = engine::sanitize_project_id(name).substr(5);
  const auto package_id = shell.project->id + "." + slug;
  const auto dir = shell.project->root / "packages" / package_id;
  std::error_code ec;
  if (std::filesystem::exists(dir / "package.json", ec)) {
    shell.status = "package already exists: " + package_id;
    return;
  }
  std::filesystem::create_directories(dir / "content", ec);
  if (ec) {
    shell.status = "package failed: " + ec.message();
    return;
  }
  const std::string manifest =
      "{\n  \"id\": \"" + package_id + "\",\n  \"name\": \"" + name +
      "\",\n  \"version\": \"0.1.0\",\n  \"priority\": 10,\n"
      "  \"dependencies\": [{\"id\": \"" + shell.project->id + "\"}],\n"
      "  \"provides\": [\"" + package_id + "\"]\n}\n";
  {
    std::ofstream out(dir / "package.json");
    out << manifest;
    if (!out) {
      shell.status = "package manifest write failed";
      return;
    }
  }
  shell.package_buffer.clear();
  const auto count = reload_packages(shell);
  scan_assets(shell,
              shell.project->root / shell.project->content_dirs.front());
  shell.status = "added " + package_id + " - " + std::to_string(count) +
                 " package(s) total";
}

// Applies the name field as the open project's display name and atomically
// rewrites its manifest — the id/content namespace intentionally stays put
// so packages and cooked output remain valid.
void rename_project(Shell &shell) {
  if (!shell.project || shell.project_name_buffer.empty()) return;
  try {
    shell.project->name = shell.project_name_buffer;
    shell.project->save();
    shell.status = "renamed project to " + shell.project->name;
    shell.project_name_buffer.clear();
    shell.editing_project_name = false;
  } catch (const std::exception &error) {
    shell.status = std::string("rename failed: ") + error.what();
  }
}

void close_project(Shell &shell) {
  shell.project.reset();
  shell.scene_doc = engine::SceneDocument{};
  shell.selected_entity = static_cast<std::size_t>(-1);
  shell.scene3_doc = engine::Scene3dDocument{};
  shell.scene3_sel = static_cast<std::size_t>(-1);
  shell.scene3_meshes.clear();
  shell.scene3_textures.clear();
  shell.scene3_content.reset();
  scan_assets(shell, find_path("assets"));
  {
    std::lock_guard lock(shell.project_mutex);
    shell.cook_status.clear();
    shell.build_status.clear();
    shell.package_status.clear();
    shell.test_status.clear();
  }
  shell.status = "project closed - browsing host assets";
}

#if defined(_WIN32)
// Cooks the open project's packages directory into build/cooked/ using the
// generic content scan — the same pipeline Stellar Continuum's assets run
// through, pointed at the project's own tree.
void start_cook(Shell &shell, engine::JobSystem &jobs) {
  if (!shell.project || shell.cooking.exchange(true)) return;
  engine::AssetCookOptions options;
  options.root = shell.project->root / "packages";
  options.output = shell.project->root / "build" / "cooked";
  options.cache = shell.project->root / "build" / "cache";
  options.report = shell.project->root / "build" / "cook-report.json";
  options.scan_content = true;
  options.package_group = shell.project->id;
  shell.cook_done = 0;
  shell.cook_total = 0;
  options.progress = [&shell](std::size_t done, std::size_t total) {
    shell.cook_done = done;
    shell.cook_total = total;
  };
  {
    std::lock_guard lock(shell.project_mutex);
    shell.cook_status = "cooking " + options.root.generic_string();
  }
  (void)jobs.submit("project.cook", engine::JobPriority::Normal, {},
                    [&shell, options] {
                      std::string message;
                      try {
                        engine::cook_asset_repository(options);
                        std::size_t packages = 0;
                        std::error_code ec;
                        const auto dir = options.output / "Content";
                        for (const auto &entry :
                             std::filesystem::directory_iterator(dir, ec))
                          if (entry.path().extension() == ".stpak") ++packages;
                        message = "cook ok - " + std::to_string(packages) +
                                  " package(s) in build/cooked";
                      } catch (const std::exception &error) {
                        message = std::string("cook failed: ") + error.what();
                      }
                      {
                        std::lock_guard lock(shell.project_mutex);
                        shell.cook_status = std::move(message);
                      }
                      shell.cooking = false;
                      shell.cooked_dirty = true;
                    });
}
#else
void start_cook(Shell &shell, engine::JobSystem &) {
  shell.status = "cook unavailable on this platform";
}
#endif

#if defined(_WIN32)
// Configures and builds the open project's host executable (src/main.cpp via
// Configures + compiles a project's host against the exported engine SDK;
// output goes to build/host/build.log. Shared by the UI job and the
// headless --build path.
std::string build_project_sync(const std::filesystem::path &root,
                               const std::string &exe_name) {
  const auto sdk = find_path("engine-sdk");
  const auto host = root / "build" / "host";
  const auto log = (host / "build.log").string();
  std::filesystem::create_directories(host);
  // The whole command gets an outer quote pair so cmd /c doesn't strip the
  // quoted exe path (quote-preservation requires exactly two quotes).
  const std::string cmake = "\"" STELLAR_CMAKE_COMMAND "\"";
  auto run = [&](const std::string &command) {
    return std::system(
        ("\"" + command + " >\"" + log + "\" 2>&1\"").c_str());
  };
  if (run(cmake + " -S \"" + root.string() + "\" -B \"" + host.string() +
          "\" -DCMAKE_BUILD_TYPE=Release -DSTELLAR_ENGINE_SDK=\"" +
          sdk.generic_string() + "\"") != 0)
    return "configure failed - see build/host/build.log";
  if (run(cmake + " --build \"" + host.string() + "\" --config Release") != 0)
    return "build failed - see build/host/build.log";
  return "build ok - " + exe_name + ".exe ready";
}

// its generated CMakeLists.txt) against the exported engine SDK. Runs on the
// JobSystem; output goes to build/host/build.log inside the project.
void start_build(Shell &shell, engine::JobSystem &jobs) {
  if (!shell.project || shell.building.exchange(true)) return;
  const auto root = shell.project->root;
  const auto exe_name = shell.project->id.substr(5);
  {
    std::lock_guard lock(shell.project_mutex);
    shell.build_status = "building " + exe_name + "...";
  }
  (void)jobs.submit("project.build", engine::JobPriority::Normal, {},
                    [&shell, root, exe_name] {
                      std::string message;
                      try {
                        message = build_project_sync(root, exe_name);
                      } catch (const std::exception &error) {
                        message = std::string("build failed: ") + error.what();
                      }
                      {
                        std::lock_guard lock(shell.project_mutex);
                        shell.build_status = std::move(message);
                      }
                      shell.building = false;
                    });
}

// Smoke-tests the built host: launches it hidden with `--frames N
// --headless`, waits up to 30s, and reports pass/fail. Headless skips
// Window/audio entirely, so TEST also works on machines with no display
// or GPU; hosts built against an older SDK ignore the flag and still run
// hidden. Shared by the UI TEST job and the headless --test path.
std::string test_project_sync(const std::filesystem::path &root,
                              const std::string &exe_name, int frames) {
  for (const auto dir : {root / "build" / "host" / "Release",
                         root / "build" / "host"}) {
    const auto exe = dir / (exe_name + ".exe");
    if (std::filesystem::is_regular_file(exe)) {
      const std::string frames_arg =
          "--frames " + std::to_string(frames) + " --headless";
      SHELLEXECUTEINFOA info{};
      info.cbSize = sizeof(info);
      info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE;
      info.lpVerb = "open";
      info.lpFile = exe.string().c_str();
      info.lpParameters = frames_arg.c_str();
      info.lpDirectory = root.string().c_str();
      info.nShow = SW_HIDE;
      if (!ShellExecuteExA(&info) || info.hProcess == nullptr)
        return "test launch failed: " + exe_name + ".exe";
      const auto wait = WaitForSingleObject(info.hProcess, 30000);
      DWORD code = 1;
      GetExitCodeProcess(info.hProcess, &code);
      if (wait == WAIT_TIMEOUT) TerminateProcess(info.hProcess, 2);
      CloseHandle(info.hProcess);
      if (wait == WAIT_OBJECT_0 && code == 0)
        return "test ok - " + std::to_string(frames) + " frames";
      return "test failed - " + exe_name +
             (wait == WAIT_TIMEOUT ? " timed out" : " exited") + " code " +
             std::to_string(code);
    }
  }
  return "no built host - run BUILD first";
}

// Launches the built host executable with the project root as its working
// directory, so relative content paths resolve.
void run_project(Shell &shell) {
  if (!shell.project) return;
  const auto exe_name = shell.project->id.substr(5) + ".exe";
  for (const auto dir : {shell.project->root / "build" / "host" / "Release",
                         shell.project->root / "build" / "host"}) {
    const auto exe = dir / exe_name;
    if (std::filesystem::is_regular_file(exe)) {
      SHELLEXECUTEINFOA info{};
      info.cbSize = sizeof(info);
      info.fMask = SEE_MASK_NOCLOSEPROCESS;
      info.lpVerb = "open";
      info.lpFile = exe.string().c_str();
      info.lpDirectory = shell.project->root.string().c_str();
      info.nShow = SW_SHOW;
      if (ShellExecuteExA(&info) && info.hProcess != nullptr) {
        shell.run_process = info.hProcess;
        shell.status = "launched " + exe_name + " (pid " +
                       std::to_string(GetProcessId(info.hProcess)) + ")";
      } else {
        shell.status = "launch failed: " + exe_name;
      }
      return;
    }
  }
  shell.status = "no built host - run BUILD first";
}

void stop_project(Shell &shell) {
  if (shell.run_process == nullptr) return;
  TerminateProcess(static_cast<HANDLE>(shell.run_process), 0);
  CloseHandle(static_cast<HANDLE>(shell.run_process));
  shell.run_process = nullptr;
  shell.status = "host stopped";
}

void start_test(Shell &shell, engine::JobSystem &jobs) {
  if (!shell.project || shell.testing.exchange(true)) return;
  const auto root = shell.project->root;
  const auto exe_name = shell.project->id.substr(5);
  {
    std::lock_guard lock(shell.project_mutex);
    shell.test_status = "testing " + exe_name + " (90 frames)...";
  }
  (void)jobs.submit("project.test", engine::JobPriority::Normal, {},
                    [&shell, root, exe_name] {
                      std::string message;
                      try {
                        message = test_project_sync(root, exe_name, 90);
                      } catch (const std::exception &error) {
                        message = std::string("test failed: ") + error.what();
                      }
                      {
                        std::lock_guard lock(shell.project_mutex);
                        shell.test_status = std::move(message);
                      }
                      shell.testing = false;
                    });
}

// Clears the tracked handle once the launched host exits on its own.
void poll_run_process(Shell &shell) {
  if (shell.run_process != nullptr &&
      WaitForSingleObject(static_cast<HANDLE>(shell.run_process), 0) ==
          WAIT_OBJECT_0) {
    CloseHandle(static_cast<HANDLE>(shell.run_process));
    shell.run_process = nullptr;
    shell.status = "host exited";
  }
}

// Assembles a distributable folder: the built host plus its runtime files,
// the cooked Content/ tree, and the source packages the host scans at
// startup — everything a player needs in dist/<name>/. Shared by the UI job
// and the headless --package path.
std::string package_project_sync(const std::filesystem::path &root,
                                 const std::string &exe_name) {
  std::filesystem::path exe;
  for (const auto dir :
       {root / "build" / "host" / "Release", root / "build" / "host"})
    if (std::filesystem::is_regular_file(dir / (exe_name + ".exe"))) {
      exe = dir / (exe_name + ".exe");
      break;
    }
  if (exe.empty()) return "no built host - run BUILD first";
  const auto dist = root / "dist" / exe_name;
  std::error_code ec;
  std::filesystem::remove_all(dist, ec);
  std::filesystem::create_directories(dist, ec);
  std::size_t files = 0;
  std::uintmax_t bytes = 0;
  auto copy_tree = [&](const std::filesystem::path &src,
                       const std::filesystem::path &dst) {
    if (!std::filesystem::is_directory(src)) return;
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(src)) {
      if (!entry.is_regular_file()) continue;
      const auto target = dst / std::filesystem::relative(entry.path(), src);
      std::filesystem::create_directories(target.parent_path(), ec);
      std::filesystem::copy_file(entry.path(), target,
                                 std::filesystem::copy_options::
                                     overwrite_existing,
                                 ec);
      if (ec) throw std::runtime_error("copy failed: " + ec.message());
      ++files;
      bytes += entry.file_size();
    }
  };
  copy_tree(exe.parent_path(), dist);
  // The cooker writes <output>/Content/; copying the cooked root yields
  // Content/ beside the exe, which is where the starter probes in packaged
  // layout.
  copy_tree(root / "build" / "cooked", dist);
  copy_tree(root / "packages", dist / "packages");
  return "packaged " + std::to_string(files) + " files, " +
         human_bytes(bytes) + " - dist/" + exe_name;
}

void start_package(Shell &shell, engine::JobSystem &jobs) {
  if (!shell.project || shell.packaging.exchange(true)) return;
  const auto root = shell.project->root;
  const auto exe_name = shell.project->id.substr(5);
  {
    std::lock_guard lock(shell.project_mutex);
    shell.package_status = "packaging " + exe_name + "...";
  }
  (void)jobs.submit("project.package", engine::JobPriority::Normal, {},
                    [&shell, root, exe_name] {
                      std::string message;
                      try {
                        message = package_project_sync(root, exe_name);
                      } catch (const std::exception &error) {
                        message =
                            std::string("package failed: ") + error.what();
                      }
                      {
                        std::lock_guard lock(shell.project_mutex);
                        shell.package_status = std::move(message);
                      }
                      shell.packaging = false;
                    });
}

// Launches the native editor on the open project: the editor stores its
// annotation documents under <project>/editor/ and seeds the name from
// the project manifest.
void open_editor(Shell &shell) {
  if (!shell.project) return;
  const auto editor =
      engine::executable_directory() / "stellar-editor.exe";
  if (!std::filesystem::is_regular_file(editor)) {
    shell.status = "stellar-editor.exe not found beside the shell";
    return;
  }
  const std::string args =
      "--project \"" + shell.project->root.string() + "\"";
  ShellExecuteA(nullptr, "open", editor.string().c_str(), args.c_str(),
                engine::executable_directory().string().c_str(), SW_SHOW);
  shell.status = "editor launched for " + shell.project->name;
}
#else
void start_build(Shell &shell, engine::JobSystem &) {
  shell.status = "build unavailable on this platform";
}
void run_project(Shell &shell) {
  shell.status = "run unavailable on this platform";
}
void stop_project(Shell &) {}
void poll_run_process(Shell &) {}
void open_editor(Shell &shell) {
  shell.status = "editor launch unavailable on this platform";
}
void start_package(Shell &shell, engine::JobSystem &) {
  shell.status = "packaging unavailable on this platform";
}
void start_test(Shell &shell, engine::JobSystem &) {
  shell.status = "testing unavailable on this platform";
}
#endif

// Copies a file into the open project's base content package
// (packages/<id>/content/), then refreshes the project-rooted asset browser.
void import_asset(Shell &shell) {
  if (!shell.project) {
    shell.status = "open a project before importing";
    return;
  }
  const std::filesystem::path source = shell.import_buffer;
  std::error_code ec;
  if (!std::filesystem::is_regular_file(source, ec)) {
    shell.status = "import failed: not a file - " + source.string();
    return;
  }
  const auto dir = shell.project->root / "packages" / shell.project->id /
                   "content";
  std::filesystem::create_directories(dir, ec);
  const auto target = dir / source.filename();
  if (std::filesystem::copy_file(
          source, target, std::filesystem::copy_options::overwrite_existing,
          ec)) {
    shell.status = "imported " + target.filename().generic_string() +
                   " - run COOK to package it";
    shell.import_buffer.clear();
    scan_assets(shell,
                shell.project->root / shell.project->content_dirs.front());
  } else {
    shell.status = "import failed: " + ec.message();
  }
}

// Headless project pipeline — no window is created, so the full loop is
// scriptable (CI, external tools, testing). Returns a process exit code.
//   --create <name> [--root <dir>] [--template windowed|blank]
//   --cook|--build|--package|--run|--test <project-root>
int run_headless(Shell &shell, const std::vector<std::string> &args) {
  const auto &op = args.front();
  if (op == "--create") {
    if (args.size() < 2) {
      std::cerr << "usage: --create <name> [--root <dir>] "
                   "[--template windowed|blank]\n";
      return 2;
    }
    std::filesystem::path root;
    std::string templ{engine::kTemplateWindowed};
    for (std::size_t i = 2; i + 1 < args.size(); i += 2) {
      if (args[i] == "--root") root = args[i + 1];
      else if (args[i] == "--template") templ = args[i + 1];
    }
    if (root.empty())
      root = shell.projects_root /
             engine::sanitize_project_id(args[1]).substr(5);
    std::string error;
    if (!engine::create_project(root, args[1], STELLAR_ENGINE_VERSION,
                                &error, templ)) {
      std::cerr << "create failed: " << error << '\n';
      return 1;
    }
    std::cout << "created " << root.generic_string() << '\n';
    return 0;
  }
  if (args.size() < 2) {
    std::cerr << "usage: " << op << " <project-root>\n";
    return 2;
  }
  std::string error;
  const auto project = engine::EngineProject::load(args[1], &error);
  if (!project) {
    std::cerr << "open failed: " << error << '\n';
    return 1;
  }
  const auto exe_name = project->id.substr(5);
#if defined(_WIN32)
  try {
    if (op == "--cook") {
      engine::AssetCookOptions options;
      options.root = project->root / "packages";
      options.output = project->root / "build" / "cooked";
      options.cache = project->root / "build" / "cache";
      options.report = project->root / "build" / "cook-report.json";
      options.scan_content = true;
      options.package_group = project->id;
      options.progress = [](std::size_t done, std::size_t total) {
        std::cout << "  cooked " << done << "/" << total << '\n';
      };
      engine::cook_asset_repository(options);
      std::size_t packages = 0;
      std::error_code ec;
      for (const auto &entry : std::filesystem::directory_iterator(
               options.output / "Content", ec))
        if (entry.path().extension() == ".stpak") ++packages;
      std::cout << "cook ok - " << packages << " package(s)\n";
      return 0;
    }
    if (op == "--build") {
      const auto message = build_project_sync(project->root, exe_name);
      std::cout << message << '\n';
      return message.starts_with("build ok") ? 0 : 1;
    }
    if (op == "--package") {
      const auto message = package_project_sync(project->root, exe_name);
      std::cout << message << '\n';
      return message.starts_with("packaged") ? 0 : 1;
    }
    if (op == "--run") {
      for (const auto dir : {project->root / "build" / "host" / "Release",
                             project->root / "build" / "host"}) {
        const auto exe = dir / (exe_name + ".exe");
        if (std::filesystem::is_regular_file(exe)) {
          SHELLEXECUTEINFOA info{};
          info.cbSize = sizeof(info);
          info.fMask = SEE_MASK_NOCLOSEPROCESS;
          info.lpVerb = "open";
          info.lpFile = exe.string().c_str();
          info.lpDirectory = project->root.string().c_str();
          info.nShow = SW_SHOW;
          if (ShellExecuteExA(&info) && info.hProcess != nullptr) {
            std::cout << "running " << exe_name << ".exe (pid "
                      << GetProcessId(info.hProcess) << ")\n";
            CloseHandle(info.hProcess);
            return 0;
          }
          std::cerr << "launch failed: " << exe_name << ".exe\n";
          return 1;
        }
      }
      std::cerr << "no built host - run --build first\n";
      return 1;
    }
    if (op == "--test") {
      // Smoke-test: run the built host for --frames N and check the exit
      // code. Optional trailing arg overrides the frame count.
      int frames = 90;
      if (args.size() > 2) frames = std::atoi(args[2].c_str());
      const auto message = test_project_sync(project->root, exe_name, frames);
      std::cout << message << '\n';
      return message.starts_with("test ok") ? 0 : 1;
    }
  } catch (const std::exception &e) {
    std::cerr << op << " failed: " << e.what() << '\n';
    return 1;
  }
#endif
  std::cerr << "unknown or unsupported op: " << op << '\n';
  return 2;
}

void create_project_from_field(Shell &shell) {
  std::string error;
  const std::string name =
      shell.project_name_buffer.empty() ? "untitled" : shell.project_name_buffer;
  const auto id = engine::sanitize_project_id(name);
  const auto dir = shell.projects_root / id.substr(5);
  if (engine::create_project(dir, name, STELLAR_ENGINE_VERSION, &error,
                             shell.blank_template ? engine::kTemplateBlank
                                                  : engine::kTemplateWindowed)) {
    shell.status = "created " + dir.filename().string() +
                   (shell.blank_template ? " (blank)" : " (windowed)");
    shell.project_name_buffer.clear();
    refresh_projects(shell);
  } else {
    shell.status = "create failed: " + error;
  }
}

void shell_button(DrawList &out, const UiRect &rect, const char *label,
                  bool active, int font, float s);
void field_box(DrawList &out, const UiRect &rect, const std::string &value,
               bool editing, const char *hint, int font, float s);

// ---- Scene tool: authors <project>/editor/scene.json ----------------

std::filesystem::path scene_path(const Shell &shell) {
  return shell.project->root / "editor" /
         std::string(engine::SceneDocument::filename);
}

void load_scene(Shell &shell) {
  shell.scene_doc = engine::SceneDocument{};
  shell.selected_entity = static_cast<std::size_t>(-1);
  shell.scene_tile_index = 0;
  shell.scene_modified = false;
  shell.scene_history.clear();
  shell.scene_sprites.clear();
  if (!shell.project) return;
  std::string error;
  if (auto doc = engine::SceneDocument::load(scene_path(shell), &error))
    shell.scene_doc = *doc;
  else if (std::filesystem::is_regular_file(scene_path(shell)))
    shell.status = "scene load failed: " + error;
}

void save_scene(Shell &shell) {
  if (!shell.project) return;
  try {
    shell.scene_doc.save(scene_path(shell));
    shell.scene_modified = false;
    shell.status = "scene saved - editor/scene.json";
  } catch (const std::exception &error) {
    shell.status = std::string("scene save failed: ") + error.what();
  }
}

engine::SceneEntity *selected_scene_entity(Shell &shell) {
  if (shell.selected_entity >= shell.scene_doc.entities.size())
    return nullptr;
  return &shell.scene_doc.entities[shell.selected_entity];
}

// The tilemap the tile fields and paint mode currently edit.
engine::SceneTilemap *scene_tile(Shell &shell) {
  auto &tms = shell.scene_doc.tilemaps;
  if (tms.empty()) return nullptr;
  if (shell.scene_tile_index >= tms.size())
    shell.scene_tile_index = tms.size() - 1;
  return &tms[shell.scene_tile_index];
}
const engine::SceneTilemap *scene_tile(const Shell &shell) {
  const auto &tms = shell.scene_doc.tilemaps;
  if (tms.empty()) return nullptr;
  return &tms[std::min(shell.scene_tile_index, tms.size() - 1)];
}

// Parses "x,y" or "vx,vy" pairs; returns false on malformed input.
bool parse_pair(std::string_view text, float &a, float &b) {
  const auto comma = text.find(',');
  if (comma == std::string_view::npos) return false;
  try {
    a = std::stof(std::string(text.substr(0, comma)));
    b = std::stof(std::string(text.substr(comma + 1)));
  } catch (const std::exception &) {
    return false;
  }
  return true;
}

// Parses "a,b,c,d" into four floats.
bool parse_quad(std::string_view text, float &a, float &b, float &c,
                float &d) {
  const auto c1 = text.find(',');
  const auto c2 = c1 == std::string_view::npos
                      ? c1
                      : text.find(',', c1 + 1);
  const auto c3 = c2 == std::string_view::npos
                      ? c2
                      : text.find(',', c2 + 1);
  if (c1 == std::string_view::npos || c2 == std::string_view::npos ||
      c3 == std::string_view::npos)
    return false;
  try {
    a = std::stof(std::string(text.substr(0, c1)));
    b = std::stof(std::string(text.substr(c1 + 1, c2 - c1 - 1)));
    c = std::stof(std::string(text.substr(c2 + 1, c3 - c2 - 1)));
    d = std::stof(std::string(text.substr(c3 + 1)));
    return std::isfinite(a) && std::isfinite(b) && std::isfinite(c) &&
           std::isfinite(d);
  } catch (const std::exception &) {
    return false;
  }
}

// Parses "r,g,b" into clamped 0-255 channels.
bool parse_color(std::string_view text, std::uint8_t &r, std::uint8_t &g,
                 std::uint8_t &b) {
  const auto c1 = text.find(',');
  const auto c2 = c1 == std::string_view::npos ? c1 : text.find(',', c1 + 1);
  if (c1 == std::string_view::npos || c2 == std::string_view::npos)
    return false;
  try {
    const auto channel = [](std::string_view v) {
      return static_cast<std::uint8_t>(
          std::clamp(std::stoi(std::string(v)), 0, 255));
    };
    r = channel(text.substr(0, c1));
    g = channel(text.substr(c1 + 1, c2 - c1 - 1));
    b = channel(text.substr(c2 + 1));
  } catch (const std::exception &) {
    return false;
  }
  return true;
}

void commit_scene_field(Shell &shell) {
  // Fields 11/13 are document-level — no entity needed.
  if (shell.scene_field == 11) {
    try {
      const float g = std::stof(shell.scene_buffer);
      shell.scene_history.commit(shell.scene_doc);
      shell.scene_doc.gravity = g;
      shell.scene_modified = true;
      shell.status = "scene gravity " + std::to_string(g) +
                     " - SAVE to persist";
    } catch (const std::exception &) {
      shell.status = "invalid value - use a number like 600";
    }
    shell.scene_buffer.clear();
    return;
  }
  if (shell.scene_field == 13) {
    std::uint8_t r, g, b;
    if (parse_color(shell.scene_buffer, r, g, b)) {
      shell.scene_history.commit(shell.scene_doc);
      shell.scene_doc.bg_r = r;
      shell.scene_doc.bg_g = g;
      shell.scene_doc.bg_b = b;
      shell.scene_modified = true;
      shell.status = "scene background updated - SAVE to persist";
    } else {
      shell.status = "invalid value - use \"r,g,b\"";
    }
    shell.scene_buffer.clear();
    return;
  }
  // Field 38 is the document's scene-load music track.
  if (shell.scene_field == 38) {
    shell.scene_history.commit(shell.scene_doc);
    shell.scene_doc.music = shell.scene_buffer;
    shell.scene_modified = true;
    shell.status = shell.scene_buffer.empty()
                       ? "scene music cleared - SAVE to persist"
                       : "scene music '" + shell.scene_buffer +
                             "' - SAVE to persist";
    shell.scene_buffer.clear();
    return;
  }
  // Field 39: per-scene world bounds (0,0 reverts to viewport/argv).
  if (shell.scene_field == 39) {
    float w, h;
    if (parse_pair(shell.scene_buffer, w, h)) {
      shell.scene_history.commit(shell.scene_doc);
      shell.scene_doc.world_w = std::max(0.f, w);
      shell.scene_doc.world_h = std::max(0.f, h);
      shell.scene_modified = true;
      shell.status = "scene world bounds updated - SAVE to persist";
    } else {
      shell.status = "invalid value - use \"w,h\" like 2560,1440";
    }
    shell.scene_buffer.clear();
    return;
  }
  // Fields 30+ edit the selected document tilemap; committing creates it
  // on demand so "tileset" alone is enough to begin a grid.
  if (shell.scene_field >= 30) {
    auto tm = scene_tile(shell) ? *scene_tile(shell)
                                : engine::SceneTilemap{};
    bool ok = false;
    if (shell.scene_field == 30) {
      tm.tileset = shell.scene_buffer;
      ok = true;
    } else if (shell.scene_field == 31) {
      float w, h;
      ok = parse_pair(shell.scene_buffer, w, h);
      if (ok) {
        tm.tile_w = std::max(1, static_cast<int>(w));
        tm.tile_h = std::max(1, static_cast<int>(h));
      }
    } else if (shell.scene_field == 32) {
      try {
        tm.columns = std::max(0, std::stoi(shell.scene_buffer));
        ok = true;
      } catch (const std::exception &) {
      }
    } else if (shell.scene_field == 33) {
      const auto b = shell.scene_buffer;
      if (b == "1" || b == "true" || b == "yes") {
        tm.collide = true;
        ok = true;
      } else if (b == "0" || b == "false" || b == "no") {
        tm.collide = false;
        ok = true;
      }
    } else if (shell.scene_field == 34) {
      try {
        tm.layer = std::stoi(shell.scene_buffer);
        ok = true;
      } catch (const std::exception &) {
      }
    } else if (shell.scene_field == 35) {
      try {
        tm.parallax = std::stof(shell.scene_buffer);
        ok = true;
      } catch (const std::exception &) {
      }
    } else if (shell.scene_field == 40) {
      ok = parse_pair(shell.scene_buffer, tm.x, tm.y);
    } else if (shell.scene_field == 41) {
      tm.name = shell.scene_buffer;
      ok = true;
    } else if (shell.scene_field == 36) {
      tm.cells.clear();
      std::stringstream ss(shell.scene_buffer);
      std::string item;
      ok = true;
      while (std::getline(ss, item, ',')) {
        try {
          tm.cells.push_back(std::stoi(item));
        } catch (const std::exception &) {
          ok = false;
          break;
        }
      }
    }
    if (shell.scene_field == 37) {
      try {
        shell.scene_paint_cell = std::stoi(shell.scene_buffer);
        shell.status = "paint brush " + shell.scene_buffer;
      } catch (const std::exception &) {
        shell.status = "invalid value - use a tile index or -1";
      }
      shell.scene_buffer.clear();
      return;
    }
    if (shell.scene_field == 42) {
      try {
        shell.scene_paint_brush =
            std::clamp(std::stoi(shell.scene_buffer), 1, 8);
        shell.status = "brush size " +
                       std::to_string(shell.scene_paint_brush) + "x" +
                       std::to_string(shell.scene_paint_brush);
      } catch (const std::exception &) {
        shell.status = "invalid value - use 1..8";
      }
      shell.scene_buffer.clear();
      return;
    }
    if (ok) {
      shell.scene_history.commit(shell.scene_doc);
      if (auto *sel = scene_tile(shell)) {
        *sel = std::move(tm);
      } else {
        shell.scene_doc.tilemaps.push_back(std::move(tm));
        shell.scene_tile_index = shell.scene_doc.tilemaps.size() - 1;
      }
      shell.scene_modified = true;
      shell.status = "tilemap updated - SAVE to persist";
    } else {
      shell.status = "invalid value - check the field hint";
    }
    shell.scene_buffer.clear();
    return;
  }
  auto *entity = selected_scene_entity(shell);
  if (entity == nullptr) {
    shell.scene_buffer.clear();
    return;
  }
  // Parse into a scratch copy so a failed parse leaves the document
  // untouched and produces no undo step.
  auto next = *entity;
  bool ok = false;
  if (shell.scene_field == 1 && !shell.scene_buffer.empty()) {
    next.name = shell.scene_buffer;
    ok = true;
  } else if (shell.scene_field == 2) {
    ok = parse_pair(shell.scene_buffer, next.x, next.y);
  } else if (shell.scene_field == 3) {
    ok = parse_pair(shell.scene_buffer, next.vx, next.vy);
  } else if (shell.scene_field == 4) {
    next.sprite = shell.scene_buffer;
    ok = true;
  } else if (shell.scene_field == 5) {
    ok = parse_pair(shell.scene_buffer, next.w, next.h);
  } else if (shell.scene_field == 6) {
    ok = parse_color(shell.scene_buffer, next.r, next.g, next.b);
  } else if (shell.scene_field == 7) {
    try {
      next.layer = std::stoi(shell.scene_buffer);
      ok = true;
    } catch (const std::exception &) {
    }
  } else if (shell.scene_field == 8) {
    try {
      next.parallax = std::stof(shell.scene_buffer);
      ok = true;
    } catch (const std::exception &) {
    }
  } else if (shell.scene_field == 9) {
    next.text = shell.scene_buffer;
    ok = true;
  } else if (shell.scene_field == 10) {
    try {
      next.gravity_scale = std::stof(shell.scene_buffer);
      ok = true;
    } catch (const std::exception &) {
    }
  } else if (shell.scene_field == 12) {
    const auto b = shell.scene_buffer;
    if (b == "1" || b == "true" || b == "yes") {
      next.solid = true;
      ok = true;
    } else if (b == "0" || b == "false" || b == "no") {
      next.solid = false;
      ok = true;
    }
  } else if (shell.scene_field == 14) {
    try {
      next.frames = std::max(1, std::stoi(shell.scene_buffer));
      ok = true;
    } catch (const std::exception &) {
    }
  } else if (shell.scene_field == 15) {
    try {
      next.fps = std::stof(shell.scene_buffer);
      ok = true;
    } catch (const std::exception &) {
    }
  } else if (shell.scene_field == 16) {
    try {
      next.rotation = std::stof(shell.scene_buffer);
      ok = true;
    } catch (const std::exception &) {
    }
  } else if (shell.scene_field == 17) {
    try {
      next.ttl = std::stof(shell.scene_buffer);
      ok = true;
    } catch (const std::exception &) {
    }
  } else if (shell.scene_field == 18 || shell.scene_field == 19 ||
             shell.scene_field == 20 || shell.scene_field == 21 ||
             shell.scene_field == 25 || shell.scene_field == 28) {
    const auto b = shell.scene_buffer;
    bool value;
    if (b == "1" || b == "true" || b == "yes") {
      value = true;
      ok = true;
    } else if (b == "0" || b == "false" || b == "no") {
      value = false;
      ok = true;
    } else {
      value = false;
    }
    if (ok) {
      if (shell.scene_field == 18)
        next.flip_x = value;
      else if (shell.scene_field == 19)
        next.flip_y = value;
      else if (shell.scene_field == 20)
        next.visible = value;
      else if (shell.scene_field == 21)
        next.oneway = value;
      else if (shell.scene_field == 25)
        next.bounce = value;
      else
        next.anim_loop = value;
    }
  } else if (shell.scene_field == 22) {
    next.data = shell.scene_buffer;
    ok = true;
  } else if (shell.scene_field == 23) {
    try {
      next.opacity = std::clamp(std::stof(shell.scene_buffer), 0.f, 1.f);
      ok = true;
    } catch (const std::exception &) {
    }
  } else if (shell.scene_field == 24) {
    try {
      next.spin = std::stof(shell.scene_buffer);
      ok = true;
    } catch (const std::exception &) {
    }
  } else if (shell.scene_field == 26) {
    next.parent = shell.scene_buffer;
    ok = true;
  } else if (shell.scene_field == 27) {
    try {
      next.fcols = std::max(0, std::stoi(shell.scene_buffer));
      ok = true;
    } catch (const std::exception &) {
    }
  } else if (shell.scene_field == 29) {
    next.vfx = shell.scene_buffer;
    ok = true;
  }
  if (ok) {
    shell.scene_history.commit(shell.scene_doc);
    *entity = std::move(next);
  }
  if (ok) {
    shell.scene_modified = true;
    shell.status = "entity " + entity->name + " updated - SAVE to persist";
  } else {
    shell.status = "invalid value - use \"x,y\", \"r,g,b\" or a path";
  }
  shell.scene_buffer.clear();
}

// Entity indices sorted by draw order: layer ascending, stable within a
// layer — the last index is the topmost entity for hit-testing.
// ---- Scene3D tool: authors <project>/editor/scene3d.json -----------

std::filesystem::path scene3_path(const Shell &shell) {
  return shell.project
             ? shell.project->root / "editor" /
                   std::string(engine::Scene3dDocument::filename)
             : std::filesystem::path{};
}

void load_scene3(Shell &shell) {
  shell.scene3_doc = engine::Scene3dDocument{};
  shell.scene3_sel = static_cast<std::size_t>(-1);
  shell.scene3_modified = false;
  shell.scene3_history.clear();
  shell.scene3_meshes.clear();
  shell.scene3_textures.clear();
  if (shell.project && !shell.scene3_content)
    shell.scene3_content = std::make_unique<engine::ContentResolver>(
        shell.project->id, shell.project->root,
        engine::executable_directory());
  std::string error;
  if (auto doc =
          engine::Scene3dDocument::load(scene3_path(shell), &error))
    shell.scene3_doc = *doc;
  else if (std::filesystem::is_regular_file(scene3_path(shell)))
    shell.status = "scene3d load failed: " + error;
}

engine::Scene3dEntity *selected_scene3_entity(Shell &shell) {
  if (shell.scene3_sel >= shell.scene3_doc.entities.size())
    return nullptr;
  return &shell.scene3_doc.entities[shell.scene3_sel];
}

std::shared_ptr<const Mesh3D> scene3_mesh(Shell &shell,
                                          const std::string &spec) {
  if (const auto it = shell.scene3_meshes.find(spec);
      it != shell.scene3_meshes.end())
    return it->second;
  auto mesh = engine::resolve_mesh_spec(spec, shell.scene3_content.get());
  shell.scene3_meshes.emplace(spec, mesh);
  return mesh;
}

std::shared_ptr<const RgbaImage> scene3_tex(Shell &shell,
                                            const std::string &path) {
  if (const auto it = shell.scene3_textures.find(path);
      it != shell.scene3_textures.end()) {
    auto &state = *it->second;
    if (!state.done.load(std::memory_order_acquire)) return nullptr;
    if (!state.error.empty() && !state.reported) {
      state.reported = true;
      shell.status = "texture decode failed: " + path;
    }
    return state.image;
  }
  auto state = std::make_shared<Shell::Scene3Tex>();
  shell.scene3_textures.emplace(path, state);
  const auto loose = shell.scene3_content
                         ? shell.scene3_content->loose_path(path)
                         : std::filesystem::path{};
  if (!std::filesystem::is_regular_file(loose)) {
    state->done.store(true, std::memory_order_release);
    return nullptr;
  }
  if (shell.jobs) {
    // Decode off the draw thread; the entity binds untextured until the
    // job publishes the image.
    (void)shell.jobs->submit("scene3d.texture",
                             engine::JobPriority::Normal, {},
                             [state, loose] {
      try {
        state->image = decode_rgba_image(loose, 4096);
      } catch (const std::exception &error) {
        state->error = error.what();
      } catch (...) {
        state->error = "unknown decode error";
      }
      state->done.store(true, std::memory_order_release);
    });
    return nullptr;
  }
  try {
    state->image = decode_rgba_image(loose, 4096);
  } catch (const std::exception &error) {
    state->error = error.what();
  } catch (...) {
    state->error = "unknown decode error";
  }
  state->done.store(true, std::memory_order_release);
  if (!state->error.empty()) {
    state->reported = true;
    shell.status = "texture decode failed: " + path;
  }
  return state->image;
}

// Parses "x,y,z" into three floats.
bool parse_triple(std::string_view text, float &a, float &b, float &c) {
  const auto c1 = text.find(',');
  const auto c2 = c1 == std::string_view::npos
                      ? c1
                      : text.find(',', c1 + 1);
  if (c1 == std::string_view::npos || c2 == std::string_view::npos)
    return false;
  try {
    a = std::stof(std::string(text.substr(0, c1)));
    b = std::stof(std::string(text.substr(c1 + 1, c2 - c1 - 1)));
    c = std::stof(std::string(text.substr(c2 + 1)));
    return std::isfinite(a) && std::isfinite(b) && std::isfinite(c);
  } catch (const std::exception &) {
    return false;
  }
}

// The authored camera's world-space ray through a preview pixel — same
// convention as RuntimeHost::entity3d_at (local -Z forward, +X right).
Vec3 scene3_ray(const engine::Scene3dDocument &doc, const UiRect &pv,
                Point pos) {
  const float nx = ((pos.x - pv.x) / pv.width) * 2.f - 1.f;
  const float ny = 1.f - ((pos.y - pv.y) / pv.height) * 2.f;
  constexpr float kDeg = 3.14159265f / 180.f;
  const float ht = std::tan(doc.fov_deg * kDeg * .5f);
  Vec3 d{nx * ht * (pv.width / pv.height), ny * ht, -1.f};
  const float l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
  d = {d.x / l, d.y / l, d.z / l};
  const Quaternion cq = compose_rotation(
      rotation_axis_angle({0.f, 1.f, 0.f}, doc.cam_yaw_deg * kDeg),
      rotation_axis_angle({1.f, 0.f, 0.f}, doc.cam_pitch_deg * kDeg));
  return rotate_vec(cq, d);
}

// Ray-picks the document entity under a preview pixel via a scratch
// world (spawn order == document order, so the hit id maps back).
std::optional<std::size_t> scene3_pick(Shell &shell, Point pos) {
  const auto &d = shell.scene3_doc;
  const Vec3 dir = scene3_ray(d, shell.scene3_preview, pos);
  engine::World scratch;
  engine::register_scene_components(scratch);
  const auto ids = engine::spawn_scene3d(scratch, d);
  const auto resolve = [&shell](const std::string &spec) {
    return scene3_mesh(shell, spec);
  };
  const auto hit = engine::raycast_world3d(
      scratch, ids, resolve, d.cam_x, d.cam_y, d.cam_z, dir.x, dir.y,
      dir.z, d.far_plane);
  if (!hit) return std::nullopt;
  const auto it = std::find(ids.begin(), ids.end(), hit->entity);
  return it == ids.end()
             ? std::nullopt
             : std::optional<std::size_t>(it - ids.begin());
}

void commit_scene3_field(Shell &shell) {
  auto &doc = shell.scene3_doc;
  auto *e = selected_scene3_entity(shell);
  const auto fail = [&](const char *hint) {
    shell.status = std::string("invalid value - ") + hint;
    shell.scene3_buffer.clear();
  };
  const auto ok = [&](std::string msg) {
    shell.scene3_modified = true;
    shell.status = msg + " - SAVE to persist";
    shell.scene3_buffer.clear();
  };
  const auto commit = [&] { shell.scene3_history.commit(doc); };
  // Document-level fields first — no entity needed.
  float a, b, c;
  switch (shell.scene3_field) {
  case 20: // camera position
    if (!parse_triple(shell.scene3_buffer, a, b, c))
      return fail("use \"x,y,z\"");
    commit();
    doc.cam_x = a; doc.cam_y = b; doc.cam_z = c;
    return ok("camera position updated");
  case 21: // camera yaw,pitch
    if (!parse_pair(shell.scene3_buffer, a, b))
      return fail("use \"yaw,pitch\" degrees");
    commit();
    doc.cam_yaw_deg = a; doc.cam_pitch_deg = std::clamp(b, -89.f, 89.f);
    return ok("camera orientation updated");
  case 22: // fov
    try {
      a = std::stof(shell.scene3_buffer);
    } catch (const std::exception &) {
      return fail("use a number like 60");
    }
    commit();
    doc.fov_deg = std::clamp(a, 10.f, 140.f);
    return ok("camera fov updated");
  case 71: // camera clip planes near,far
    if (!parse_pair(shell.scene3_buffer, a, b))
      return fail("use \"near,far\" - e.g. 0.05,5000");
    if (!(a > 0.f) || !(b > a + 1e-3f) || !(b <= 1e7f))
      return fail("need 0 < near < far <= 1e7");
    commit();
    doc.near_plane = a; doc.far_plane = b;
    return ok("camera clip planes updated");
  case 23: // key light direction
    if (!parse_triple(shell.scene3_buffer, a, b, c))
      return fail("use \"x,y,z\"");
    commit();
    doc.light_x = a; doc.light_y = b; doc.light_z = c;
    return ok("key light direction updated");
  case 24: // key light intensity
    try {
      a = std::stof(shell.scene3_buffer);
    } catch (const std::exception &) {
      return fail("use a number like 1.2");
    }
    commit();
    doc.light_intensity = std::max(0.f, a);
    return ok("key light intensity updated");
  case 25: // gravity
    try {
      a = std::stof(shell.scene3_buffer);
    } catch (const std::exception &) {
      return fail("use a number like 9.8");
    }
    commit();
    doc.gravity = a;
    return ok("scene gravity updated");
  case 26: // groundY
    try {
      a = std::stof(shell.scene3_buffer);
    } catch (const std::exception &) {
      return fail("use a number like 0");
    }
    commit();
    doc.ground_y = a;
    return ok("ground plane updated");
  case 27: // xz bounds
    try {
      a = std::stof(shell.scene3_buffer);
    } catch (const std::exception &) {
      return fail("use a number like 30");
    }
    commit();
    doc.bounds = std::max(0.f, a);
    return ok("XZ bounds updated");
  case 28: // background color
    std::uint8_t r8, g8, b8;
    if (!parse_color(shell.scene3_buffer, r8, g8, b8))
      return fail("use \"r,g,b\"");
    commit();
    doc.bg_r = r8; doc.bg_g = g8; doc.bg_b = b8;
    return ok("background updated");
  case 29: // music
    commit();
    doc.music = shell.scene3_buffer;
    return ok(shell.scene3_buffer.empty() ? "scene music cleared"
                                          : "scene music set");
  // Fill lights: fields 30/32 set a slot's direction, 31/33 set its
  // "r,g,b,intensity" tint (0..1 channels); an empty direction removes
  // the slot — the material pipeline evaluates at most two.
  case 30:
  case 32: {
    const std::size_t slot = shell.scene3_field == 30 ? 0 : 1;
    if (shell.scene3_buffer.empty()) {
      if (slot >= doc.lights.size()) {
        shell.scene3_buffer.clear();
        return;
      }
      commit();
      doc.lights.erase(doc.lights.begin() + slot);
      return ok("fill light removed");
    }
    if (!parse_triple(shell.scene3_buffer, a, b, c))
      return fail("use \"x,y,z\" direction, empty removes");
    commit();
    while (doc.lights.size() <= slot)
      doc.lights.push_back(engine::Scene3dLight{});
    doc.lights[slot].dir_x = a;
    doc.lights[slot].dir_y = b;
    doc.lights[slot].dir_z = c;
    return ok("fill light direction updated");
  }
  case 31:
  case 33: {
    const std::size_t slot = shell.scene3_field == 31 ? 0 : 1;
    float intensity;
    if (!parse_quad(shell.scene3_buffer, a, b, c, intensity))
      return fail("use \"r,g,b,intensity\" like 0.4,0.6,1,0.5");
    commit();
    while (doc.lights.size() <= slot)
      doc.lights.push_back(engine::Scene3dLight{});
    doc.lights[slot].r = a;
    doc.lights[slot].g = b;
    doc.lights[slot].b = c;
    doc.lights[slot].intensity = std::max(0.f, intensity);
    return ok("fill light tint updated");
  }
  // View post-processing — applied to the 3D viewport's HDR resolve.
  case 34: // exposure
    try {
      a = std::stof(shell.scene3_buffer);
    } catch (const std::exception &) {
      return fail("use a number like 1.0");
    }
    if (!(a > 0.f)) return fail("exposure must be positive");
    commit();
    doc.exposure = a;
    return ok("exposure updated");
  case 35: // bloom strength,threshold
    if (!parse_pair(shell.scene3_buffer, a, b))
      return fail("use \"strength,threshold\" like 0.6,1.0");
    commit();
    doc.bloom = std::clamp(a, 0.f, 8.f);
    doc.bloom_threshold = std::clamp(b, 0.f, 8.f);
    return ok("bloom updated");
  case 36: // contrast,saturation,sharpen
    if (!parse_triple(shell.scene3_buffer, a, b, c))
      return fail("use \"contrast,saturation,sharpen\" like 1.1,1,0.3");
    commit();
    doc.contrast = std::clamp(a, 0.f, 2.f);
    doc.saturation = std::clamp(b, 0.f, 2.f);
    doc.sharpen = std::clamp(c, 0.f, 1.f);
    return ok("grading updated");
  case 37: { // quality tier
    const auto &q = shell.scene3_buffer;
    if (q != "low" && q != "medium" && q != "high" && q != "ultra")
      return fail("use low|medium|high|ultra");
    commit();
    doc.quality = q;
    return ok("quality tier updated");
  }
  case 38: { // point lights: "x,y,z,r,g,b,intensity,range[,dx,dy,dz,inner,outer]; ..."
    std::vector<engine::Scene3dPointLight> parsed;
    if (!shell.scene3_buffer.empty()) {
      std::istringstream entries(shell.scene3_buffer);
      std::string entry;
      while (std::getline(entries, entry, ';')) {
        std::istringstream values(entry);
        std::string token;
        float v[13];
        int n = 0;
        while (n < 13 && std::getline(values, token, ',')) {
          try {
            v[n++] = std::stof(token);
          } catch (const std::exception &) {
            return fail("use \"x,y,z,r,g,b,intensity,range[,dx,dy,dz,inner,outer]; ...\"");
          }
        }
        if (n != 8 && n != 13)
          return fail("each point light needs x,y,z,r,g,b,intensity,range[,dx,dy,dz,inner,outer]");
        engine::Scene3dPointLight l;
        l.x = v[0]; l.y = v[1]; l.z = v[2];
        l.r = v[3]; l.g = v[4]; l.b = v[5];
        l.intensity = std::max(0.f, v[6]);
        l.range = std::max(0.f, v[7]);
        if (n == 13) {
          l.spot_x = v[8]; l.spot_y = v[9]; l.spot_z = v[10];
          l.spot_inner = v[11]; l.spot_outer = v[12];
          const double sd2 = static_cast<double>(l.spot_x) * l.spot_x +
                             static_cast<double>(l.spot_y) * l.spot_y +
                             static_cast<double>(l.spot_z) * l.spot_z;
          if (sd2 <= 0 || l.spot_inner <= l.spot_outer ||
              l.spot_inner <= 0.f || l.spot_inner > 1.f ||
              l.spot_outer < 0.f || l.spot_outer >= 1.f)
            return fail("spot cones need a nonzero direction and 0<=outer<inner<=1");
        }
        parsed.push_back(l);
      }
    }
    if (parsed.size() > 4) return fail("at most four point lights");
    commit();
    doc.point_lights = std::move(parsed);
    return ok("point lights updated");
  }
  case 39: { // debug shading override
    const auto &d = shell.scene3_buffer;
    if (d != "lit" && d != "unlit" && d != "albedo" && d != "normals" &&
        d != "roughness" && d != "metallic" && d != "emissive" &&
        d != "lighting" && d != "lod" && d != "residency")
      return fail("use lit|unlit|albedo|normals|roughness|metallic|"
                  "emissive|lighting|lod|residency");
    commit();
    doc.debug_view = d;
    return ok("debug view updated");
  }
  case 52: { // key-light shadow map: "extent,dist,depth[,strength,bias[,res]]"
    if (shell.scene3_buffer.empty()) { // empty clears
      commit();
      doc.shadow_extent = 0.f;
      return ok("shadow map disabled");
    }
    float v[6]{};
    {
      std::istringstream values(shell.scene3_buffer);
      std::string token;
      int n = 0;
      while (n < 6 && std::getline(values, token, ',')) {
        try {
          v[n++] = std::stof(token);
        } catch (const std::exception &) {
          return fail("use extent,distance,depth[,strength,bias[,resolution]]");
        }
      }
      if (n < 3)
        return fail("use extent,distance,depth[,strength,bias[,resolution]]");
      commit();
      doc.shadow_extent = v[0];
      doc.shadow_distance = v[1];
      doc.shadow_depth = v[2];
      doc.shadow_strength = n > 3 ? v[3] : 1.f;
      doc.shadow_bias = n > 4 ? v[4] : .0005f;
      doc.shadow_resolution =
          n > 5 ? static_cast<std::uint32_t>(std::max(0.f, v[5])) : 0u;
    }
    return ok("shadow map updated");
  }
  default:
    break;
  }
  if (e == nullptr) {
    fail("no entity selected");
    return;
  }
  engine::Scene3dEntity next = *e;
  bool valid = false;
  const bool truthy = shell.scene3_buffer == "1" ||
                      shell.scene3_buffer == "true" ||
                      shell.scene3_buffer == "yes";
  const bool falsy = shell.scene3_buffer == "0" ||
                     shell.scene3_buffer == "false" ||
                     shell.scene3_buffer == "no";
  switch (shell.scene3_field) {
  case 1: next.name = shell.scene3_buffer; valid = !next.name.empty(); break;
  case 2: valid = parse_triple(shell.scene3_buffer, a, b, c);
          if (valid) { next.x = a; next.y = b; next.z = c; } break;
  case 3: valid = parse_triple(shell.scene3_buffer, a, b, c);
          if (valid) { next.vx = a; next.vy = b; next.vz = c; } break;
  case 4: next.mesh = shell.scene3_buffer; valid = !next.mesh.empty();
          break;
  case 5: valid = parse_triple(shell.scene3_buffer, a, b, c);
          if (valid) {
            next.yaw_deg = a; next.pitch_deg = b; next.roll_deg = c;
          }
          break;
  case 6:
          try { a = std::stof(shell.scene3_buffer); }
          catch (const std::exception &) { break; }
          if ((valid = a > 0.f)) next.scale = a;
          break;
  case 7: valid = parse_color(shell.scene3_buffer, next.r, next.g,
                              next.b); break;
  case 8: next.texture = shell.scene3_buffer; valid = true; break;
  case 9:
          try { a = std::stof(shell.scene3_buffer); }
          catch (const std::exception &) { break; }
          next.opacity = std::clamp(a, 0.f, 1.f); valid = true; break;
  case 10:
          try { a = std::stof(shell.scene3_buffer); }
          catch (const std::exception &) { break; }
          next.gravity_scale = a; valid = true; break;
  case 11: if (truthy || falsy) { next.solid = truthy; valid = true; }
           break;
  case 12: if (truthy || falsy) { next.double_sided = truthy;
                                  valid = true; }
           break;
  case 13:
          try { a = std::stof(shell.scene3_buffer); }
          catch (const std::exception &) { break; }
          next.ttl = std::max(0.f, a); valid = true; break;
  case 14: next.data = shell.scene3_buffer; valid = true; break;
  case 15: next.parent = shell.scene3_buffer; valid = true; break;
  case 16: next.vfx = shell.scene3_buffer; valid = true; break;
  // Metallic-workflow material extensions — every field lands on the
  // entity's MaterialPbr/AtmosphereShell components and renders in the
  // preview immediately.
  case 40:
          valid = parse_pair(shell.scene3_buffer, a, b);
          if (valid) {
            next.metallic = std::clamp(a, 0.f, 1.f);
            next.roughness = std::clamp(b, 0.04f, 1.f);
          }
          break;
  case 41: next.metallic_roughness = shell.scene3_buffer; valid = true;
           break;
  case 42: next.emissive = shell.scene3_buffer; valid = true; break;
  case 43: {
           float strength;
           valid = parse_quad(shell.scene3_buffer, strength, a, b, c);
           if (valid) {
             next.emissive_strength = std::max(0.f, strength);
             next.emissive_r = a; next.emissive_g = b; next.emissive_b = c;
           }
           break; }
  case 44:
          try { a = std::stof(shell.scene3_buffer); }
          catch (const std::exception &) { break; }
          next.night_emissive = std::clamp(a, 0.f, 1.f); valid = true;
          break;
  case 45: next.environment = shell.scene3_buffer; valid = true; break;
  case 46:
          try { a = std::stof(shell.scene3_buffer); }
          catch (const std::exception &) { break; }
          next.environment_strength = std::clamp(a, 0.f, 16.f);
          valid = true; break;
  case 47:
          try { a = std::stof(shell.scene3_buffer); }
          catch (const std::exception &) { break; }
          next.alpha_cutout = std::clamp(a, 0.f, 1.f); valid = true; break;
  case 48:
          valid = parse_pair(shell.scene3_buffer, a, b);
          if (valid && a >= .01f && a <= 64.f && b >= .01f && b <= 64.f) {
            next.uv_tile_x = a; next.uv_tile_y = b;
          } else valid = false;
          break;
  case 49:
          valid = parse_triple(shell.scene3_buffer, a, b, c);
          if (valid) {
            next.atmo_strength = std::clamp(a, 0.f, 16.f);
            next.atmo_power = std::clamp(b, .5f, 16.f);
            next.atmo_night = std::clamp(c, 0.f, 1.f);
          }
          break;
  case 50:
          valid = parse_triple(shell.scene3_buffer, a, b, c);
          if (valid && a >= 0.f && b >= 0.f && c >= 0.f) {
            next.atmo_r = a; next.atmo_g = b; next.atmo_b = c;
          } else valid = false;
          break;
  case 51:
          try { a = std::stof(shell.scene3_buffer); }
          catch (const std::exception &) { break; }
          if (a >= 0.f && a <= 1e12f) { next.visible_range = a; valid = true; }
          break;
  case 53: { // surface maps: "normal,properties,cloud" (empty allowed)
          std::istringstream maps(shell.scene3_buffer);
          std::string first, second, third;
          if (std::getline(maps, first, ',') &&
              std::getline(maps, second, ',') &&
              std::getline(maps, third)) {
            next.normal_map = first;
            next.properties_map = second;
            next.cloud_map = third;
            valid = true;
          }
          break; }
  case 54:
          valid = parse_pair(shell.scene3_buffer, a, b);
          if (valid && a >= 0.f && a <= 2.f && b >= 0.f && b <= .02f) {
            next.normal_strength = a; next.relief = b;
          } else valid = false;
          break;
  case 55: {
          float albedo;
          // Optional 5th field: cloud-deck height (object units, [0,.1]).
          float height = 0.f;
          std::string_view text = shell.scene3_buffer;
          const auto c4 = [&] {
            auto p = text.find(',');
            for (int i = 0; i < 3 && p != std::string_view::npos; ++i)
              p = text.find(',', p + 1);
            return p;
          }();
          if (c4 != std::string_view::npos) {
            try {
              height = std::stof(std::string(text.substr(c4 + 1)));
            } catch (const std::exception &) {
              valid = false;
              break;
            }
            text = text.substr(0, c4);
          }
          valid = parse_quad(text, a, albedo, b, c);
          if (valid && a >= 0.f && a <= 1.f && albedo >= 0.f &&
              albedo <= 1.f && b >= -2.f && b <= 2.f && c >= -2.f &&
              c <= 2.f && height >= 0.f && height <= 0.1f) {
            next.cloud_opacity = a; next.cloud_albedo = albedo;
            next.cloud_offset_x = b; next.cloud_offset_y = c;
            next.cloud_height = height;
          } else valid = false;
          break; }
  case 56:
          try { a = std::stof(shell.scene3_buffer); }
          catch (const std::exception &) { break; }
          if (a >= 0.f && a <= 1.f) { next.terminator_wrap = a; valid = true; }
          break;
  case 57: {
          std::string part;
          std::istringstream csv(shell.scene3_buffer);
          std::vector<std::string> parts;
          while (std::getline(csv, part, ',')) parts.push_back(part);
          if (parts.size() >= 1 && parts.size() <= 2) {
            try { a = std::stof(parts[0]); valid = a >= 0.f && a <= 1.f; }
            catch (const std::exception &) { break; }
            if (!valid) break;
            next.limb_darkening = a;
            if (parts.size() == 2) {
              try { a = std::stof(parts[1]); }
              catch (const std::exception &) { valid = false; break; }
              if (!(a >= 0.f && a <= 1.f)) { valid = false; break; }
              next.limb_darkening_q = a;
            }
          }
          break; }
  case 58: {
          next.lod_meshes.clear();
          std::string spec;
          std::istringstream csv(shell.scene3_buffer);
          bool oversize = false;
          while (std::getline(csv, spec, ',')) {
            if (spec.size() > 256) { oversize = true; break; }
            if (!spec.empty()) next.lod_meshes.push_back(spec);
          }
          valid = !oversize && next.lod_meshes.size() <= 8;
          break; }
  case 59:
          try { a = std::stof(shell.scene3_buffer); }
          catch (const std::exception &) { break; }
          if (a >= 1.f && a <= 4096.f) { next.lod_pixels = a; valid = true; }
          break;
  case 60:
          try { a = std::stof(shell.scene3_buffer); }
          catch (const std::exception &) { break; }
          if (a >= -0.5f && a <= 0.5f) { next.band_shear = a; valid = true; }
          break;
  case 61:
          try { a = std::stof(shell.scene3_buffer); }
          catch (const std::exception &) { break; }
          if (a >= -1.f && a <= 1.f) { next.orbital_beaming = a; valid = true; }
          break;
  case 62:
          try { a = std::stof(shell.scene3_buffer); }
          catch (const std::exception &) { break; }
          if (a >= 100.f && a <= 100000.f) { next.star_kelvin = a; valid = true; }
          break;
  case 63: {
          float inner, outer, kelvin, beam;
          valid = parse_quad(shell.scene3_buffer, inner, outer, kelvin,
                             beam);
          if (valid && inner > 0.f && outer > inner &&
              kelvin >= 100.f && kelvin <= 100000.f &&
              beam >= -1.f && beam <= 1.f)
            next.accretion = {inner, outer, kelvin, beam};
          else valid = false;
          break; }
  case 64:
          try { a = std::stof(shell.scene3_buffer); }
          catch (const std::exception &) { break; }
          if (a >= -1.f && a <= 1.f) { next.forward_scatter = a; valid = true; }
          break;
  case 65: {
          float v[8]{};
          std::istringstream csv(shell.scene3_buffer);
          std::vector<std::string> toks;
          std::string tok;
          while (std::getline(csv, tok, ',')) toks.push_back(tok);
          int n = 0;
          for (; n < 8 && n < (int)toks.size(); ++n) {
            try { v[n] = std::stof(toks[n]); }
            catch (const std::exception &) { n = -1; break; }
          }
          const std::string img2 = n >= 0 && toks.size() > 8
              ? toks[8] : "";
          float occ = 0.f, rate = 0.f;
          if (n >= 0 && toks.size() > 9) {
            try { occ = std::stof(toks[9]); }
            catch (const std::exception &) { n = -1; }
          }
          if (n >= 0 && toks.size() > 10) {
            try { rate = std::stof(toks[10]); }
            catch (const std::exception &) { n = -1; }
          }
          if (n >= 5 && v[0] >= 0.f && v[0] <= 0.75f && v[1] > 0.f &&
              v[1] <= 32.f && std::abs(v[2]) <= 1e4f && v[3] >= 8.f &&
              v[3] <= 64.f && v[4] >= 0.f && v[4] <= 1.f &&
              std::abs(v[5]) <= 1e4f && v[6] >= 0.f && v[6] <= 0.1f &&
              v[7] >= 0.f && v[7] <= 1.f &&
              (v[7] == 0.f || !img2.empty()) &&
              occ >= 0.f && occ <= 1e4f && std::abs(rate) <= 64.f &&
              (v[0] == 0.f || !next.texture.empty())) {
            next.volume_depth = v[0];
            next.volume_density = v[1];
            next.volume_seed = v[2];
            next.volume_steps = static_cast<int>(v[3]);
            next.volume_scatter = v[4];
            next.volume_flow = v[5];
            next.volume_distort = v[6];
            next.volume_blend = v[7];
            next.volume_image2 = img2;
            next.volume_occlude = occ;
            next.volume_flow_rate = rate;
            valid = true;
          }
          break; }
  case 70: {
          std::string part;
          std::istringstream csv(shell.scene3_buffer);
          std::vector<std::string> parts;
          while (std::getline(csv, part, ',')) parts.push_back(part);
          if (parts.size() >= 1 && parts.size() <= 2) {
            try { a = std::stof(parts[0]); valid = a >= -0.25f && a <= 0.25f; }
            catch (const std::exception &) { break; }
            if (!valid) break;
            next.band_drift = a;
            if (parts.size() == 2) {
              try { a = std::stof(parts[1]); }
              catch (const std::exception &) { valid = false; break; }
              if (!(a >= -8.f && a <= 8.f)) { valid = false; break; }
              next.band_turbulence = a;
            }
          }
          break; }
  case 66:
          try { a = std::stof(shell.scene3_buffer); }
          catch (const std::exception &) { break; }
          if (a >= 0.f && a <= 0.5f) { next.lod_fade = a; valid = true; }
          break;
  case 67:
          try { a = std::stof(shell.scene3_buffer); }
          catch (const std::exception &) { break; }
          if (a >= 0.f && a <= 0.5f) { next.visible_fade = a; valid = true; }
          break;
  case 68:
          try { a = std::stof(shell.scene3_buffer); }
          catch (const std::exception &) { break; }
          if (a >= 0.f && a <= 1.f) { next.band_waves = a; valid = true; }
          break;
  case 69: {
          next.lod_group.clear(); next.lod_proxy.clear();
          next.lod_proxy_pixels = 16.f;
          if (shell.scene3_buffer.empty()) { valid = true; break; }
          std::string part;
          std::istringstream csv(shell.scene3_buffer);
          std::vector<std::string> parts;
          while (std::getline(csv, part, ';')) parts.push_back(part);
          if (parts.size() >= 1 && parts.size() <= 3) {
            valid = parts[0].size() <= 64;
            next.lod_group = parts[0];
            if (valid && parts.size() >= 2) {
              valid = parts[1].size() <= 256;
              next.lod_proxy = parts[1];
            }
            if (valid && parts.size() >= 3) {
              try { a = std::stof(parts[2]); }
              catch (const std::exception &) { valid = false; break; }
              valid = a >= 1.f && a <= 4096.f;
              next.lod_proxy_pixels = a;
            }
          }
          break; }
  default: break;
  }
  if (!valid) return fail("check the field hint");
  commit();
  *e = std::move(next);
  ok("entity updated");
}

void render_scene3(DrawList &out, Shell &shell, UiRect body, float s) {
  float x = body.x + 22 * s;
  float y = body.y + 18 * s;
  const int font = static_cast<int>(13 * s);
  heading(out, x, y, "SCENE3D AUTHORING");
  if (!shell.project) {
    line(out, x, y, "open project", "none - open one in Projects", font);
    shell.hit3_add = shell.hit3_del = shell.hit3_save = shell.hit3_undo =
        shell.hit3_redo = shell.hit3_dup = shell.hit3_name =
            shell.hit3_mesh = shell.hit3_pos = shell.hit3_rot =
                shell.hit3_scale = shell.hit3_vel = shell.hit3_color =
                    shell.hit3_tex = shell.hit3_opacity = shell.hit3_dbl =
                        shell.hit3_solid = shell.hit3_gravs =
                            shell.hit3_ttl = shell.hit3_data =
                                shell.hit3_parent = shell.hit3_vfx =
                                    shell.hit3_cam =
                                    shell.hit3_camrot = shell.hit3_fov =
                                        shell.hit3_clip =
                                        shell.hit3_lightdir =
                                            shell.hit3_lightint =
                                                shell.hit3_grav =
                                                    shell.hit3_ground =
                                                        shell.hit3_bounds =
                                                            shell.hit3_bg =
                                                                shell.hit3_music =
                                                                    shell.hit3_filla_dir =
                                                                        shell.hit3_filla_tint =
                                                                            shell.hit3_fillb_dir =
                                                                                shell.hit3_fillb_tint =
                                                                    {};
    shell.hit3_pbr = shell.hit3_mr = shell.hit3_emis = shell.hit3_emit =
        shell.hit3_night = shell.hit3_env = shell.hit3_envstr =
            shell.hit3_cutout = shell.hit3_tile = shell.hit3_atmo =
                shell.hit3_atmotint = shell.hit3_exposure =
                    shell.hit3_bloom = shell.hit3_grade =
                        shell.hit3_quality = shell.hit3_plights =
                            shell.hit3_debug = shell.hit3_range =
                                shell.hit3_shadow = shell.hit3_surfmaps =
                                    shell.hit3_surfshape =
                                        shell.hit3_clouddeck =
                                            shell.hit3_termwrap =
                                                shell.hit3_limbdark =
                                                    shell.hit3_lods =
                                                        shell.hit3_lodpixels =
                                                            shell.hit3_bandshear =
                                                                shell.hit3_bandwaves =
                                                                    shell.hit3_orbitbeam =
                                                                    shell.hit3_starkelvin =
                                                                        shell.hit3_accretion =
                                                                            shell.hit3_fwdscatter =
                                                                                shell.hit3_volume =
                                                                                    shell.hit3_lodfade =
                                                                                        shell.hit3_visfade =
                                                                                            shell.hit3_lodgroup =
                                                                                                shell.hit3_banddrift = {};
    shell.hit3_mode_move = shell.hit3_mode_rot =
        shell.hit3_mode_scale = {};
    shell.scene3_preview = shell.scene3_rows = {};
    return;
  }
  if (shell.scene3_dirty.exchange(false)) load_scene3(shell);
  auto &doc = shell.scene3_doc;
  line(out, x, y, "project", shell.project->name, font);
  line(out, x, y, "document", "editor/scene3d.json", font);
  line(out, x, y, "entities", std::to_string(doc.entities.size()), font);
  y += 4 * s;

  // Action row.
  const float bh = (font + 14) * s;
  shell.hit3_add = {x, y, 148 * s, bh};
  shell_button(out, shell.hit3_add, "ADD ENTITY", false, font, s);
  shell.hit3_del = {x + 158 * s, y, 100 * s, bh};
  shell_button(out, shell.hit3_del, "DELETE",
               shell.scene3_sel < doc.entities.size(), font, s);
  shell.hit3_save = {x + 268 * s, y, 90 * s, bh};
  shell_button(out, shell.hit3_save,
               shell.scene3_modified ? "SAVE *" : "SAVE",
               shell.scene3_modified, font, s);
  shell.hit3_undo = {x + 368 * s, y, 90 * s, bh};
  shell_button(out, shell.hit3_undo, "UNDO",
               shell.scene3_history.can_undo(), font, s);
  shell.hit3_redo = {x + 468 * s, y, 90 * s, bh};
  shell_button(out, shell.hit3_redo, "REDO",
               shell.scene3_history.can_redo(), font, s);
  shell.hit3_dup = {x + 568 * s, y, 128 * s, bh};
  shell_button(out, shell.hit3_dup, "DUPLICATE",
               shell.scene3_sel < doc.entities.size(), font, s);
  // LMB drag transform mode — what a left-drag does to the picked
  // entity in the preview below.
  shell.hit3_mode_move = {x + 716 * s, y, 86 * s, bh};
  shell_button(out, shell.hit3_mode_move, "MOVE",
               shell.scene3_drag_mode == 0, font, s);
  shell.hit3_mode_rot = {x + 810 * s, y, 100 * s, bh};
  shell_button(out, shell.hit3_mode_rot, "ROTATE",
               shell.scene3_drag_mode == 1, font, s);
  shell.hit3_mode_scale = {x + 918 * s, y, 90 * s, bh};
  shell_button(out, shell.hit3_mode_scale, "SCALE",
               shell.scene3_drag_mode == 2, font, s);
  y += bh + 14 * s;

  // Entity list (left) + live 3D preview (right).
  const UiRect list_rect{x, y, body.width * 0.34f, body.height * 0.42f};
  out.overlay.push_back(FilledRectangle{list_rect, {6, 16, 26, 255}});
  out.overlay.push_back(StrokedRectangle{list_rect, panel_edge});
  shell.scene3_rows = list_rect;
  shell.scene3_list.configure(doc.entities.size(), 22 * s,
                              list_rect.height);
  const auto range = shell.scene3_list.visible_range();
  float ry = list_rect.y - shell.scene3_list.scroll_offset +
             range.first * shell.scene3_list.row_height;
  for (std::size_t i = range.first; i < range.last;
       ++i, ry += shell.scene3_list.row_height) {
    const auto &e = doc.entities[i];
    const UiRect row{list_rect.x + 4 * s, ry, list_rect.width - 8 * s,
                     shell.scene3_list.row_height};
    if (i == shell.scene3_sel)
      out.overlay.push_back(FilledRectangle{row, {22, 52, 74, 255}});
    out.overlay.push_back(Text{
        {row.x + 8 * s, row.y + 4 * s},
        e.name + "  [" + e.mesh + "]", ink, font});
  }

  // Preview: a real Scene3D built from the document — the same scene a
  // --scene3d host renders (meshes resolve through the shared spec cache).
  const UiRect pv{list_rect.x + list_rect.width + 16 * s, list_rect.y,
                  body.x + body.width - list_rect.x - list_rect.width -
                      38 * s,
                  list_rect.height};
  shell.scene3_preview = pv;
  out.overlay.push_back(
      FilledRectangle{pv, {doc.bg_r, doc.bg_g, doc.bg_b, 255}});
  {
    constexpr float kDeg = 3.14159265f / 180.f;
    Camera3D cam;
    cam.position = {doc.cam_x, doc.cam_y, doc.cam_z};
    const Quaternion cam_q = compose_rotation(
        rotation_axis_angle({0.f, 1.f, 0.f}, doc.cam_yaw_deg * kDeg),
        rotation_axis_angle({1.f, 0.f, 0.f}, doc.cam_pitch_deg * kDeg));
    cam.orientation = cam_q;
    cam.vertical_fov_radians = doc.fov_deg * kDeg;
    cam.near_plane = std::max(doc.near_plane, 1e-6f);
    cam.far_plane = std::clamp(doc.far_plane, cam.near_plane + 1e-3f,
                               1e7f);
    std::vector<MeshInstance3D> instances;
    instances.reserve(doc.entities.size());
    for (std::size_t i = 0; i < doc.entities.size(); ++i) {
      const auto &e = doc.entities[i];
      const auto mesh = scene3_mesh(shell, e.mesh);
      if (!mesh) continue;
      MeshInstance3D inst;
      inst.mesh = mesh;
      inst.position = {e.x, e.y, e.z};
      inst.rotation =
          engine::euler_to_quat3(e.yaw_deg, e.pitch_deg, e.roll_deg);
      inst.scale = e.scale;
      inst.material.tint = Color{e.r, e.g, e.b, 255};
      inst.material.opacity = e.opacity;
      inst.material.transparent = e.opacity < 1.f;
      if (!e.texture.empty())
        inst.material.texture = scene3_tex(shell, e.texture);
      inst.material.double_sided = e.double_sided;
      // Spectral-class preset: seeds tint/ambient/limb; explicit fields
      // (limbDarken below, texture, atmosphere) still override.
      if (e.star_kelvin >= 100.0) {
        const auto star = star_photosphere3d(e.star_kelvin);
        inst.material.tint = star.tint;
        inst.material.ambient = star.ambient;
        inst.material.diffuse = star.diffuse;
        inst.material.light_color = star.light_color;
        inst.material.linear_light = star.linear_light;
        inst.material.limb_darkening = star.limb_darkening;
      inst.material.limb_darkening_q = star.limb_darkening_q;
      }
      // Accretion-disc preset: generated radial texture (unless an
      // authored texture wins) + emissive-dominant response + beaming.
      if (e.accretion[2] >= 100.f && e.accretion[0] > 0.f &&
          e.accretion[1] > e.accretion[0] &&
          std::abs(e.accretion[3]) <= 1.f) {
        const auto disc = accretion_disc_material3d(
            e.accretion[0], e.accretion[1], e.accretion[2], e.accretion[3]);
        if (e.texture.empty()) inst.material.texture = disc.texture;
        inst.material.ambient = disc.ambient;
        inst.material.diffuse = disc.diffuse;
        inst.material.light_color = disc.light_color;
        inst.material.linear_light = disc.linear_light;
        inst.material.double_sided = disc.double_sided;
        inst.material.orbital_beaming = disc.orbital_beaming;
        inst.material.anisotropic_texture = disc.anisotropic_texture;
      }
      // The same fields the runtime maps through MaterialPbr/AtmosphereShell.
      if (e.metallic != 0.f || e.roughness != 0.55f ||
          !e.metallic_roughness.empty() || !e.emissive.empty() ||
          e.emissive_strength != 0.f || e.night_emissive != 0.f ||
          !e.environment.empty() || e.environment_strength != 0.f ||
          e.alpha_cutout != 0.f || e.uv_tile_x != 1.f ||
          e.uv_tile_y != 1.f) {
        PbrSurface3D surface;
        surface.metallic = e.metallic;
        surface.roughness = e.roughness;
        surface.emissive_strength = e.emissive_strength;
        surface.night_emissive = e.night_emissive;
        surface.environment_strength = e.environment_strength;
        surface.emissive_tint = {e.emissive_r, e.emissive_g, e.emissive_b};
        if (!e.metallic_roughness.empty())
          surface.metallic_roughness = scene3_tex(shell, e.metallic_roughness);
        if (!e.emissive.empty())
          surface.emissive = scene3_tex(shell, e.emissive);
        if (!e.environment.empty())
          surface.environment = scene3_tex(shell, e.environment);
        inst.material.pbr = surface;
        inst.material.alpha_threshold = e.alpha_cutout;
        inst.material.texture_tiling = {e.uv_tile_x, e.uv_tile_y};
      }
      // The same fields the runtime maps through MaterialSurface.
      if (!e.normal_map.empty() || !e.properties_map.empty() ||
          !e.cloud_map.empty()) {
        SurfaceResponse3D response;
        if (!e.normal_map.empty())
          response.normal = scene3_tex(shell, e.normal_map);
        if (!e.properties_map.empty())
          response.properties = scene3_tex(shell, e.properties_map);
        if (!e.cloud_map.empty())
          response.cloud_shadow = scene3_tex(shell, e.cloud_map);
        response.normal_strength = e.normal_strength;
        response.relief = e.relief;
        response.cloud_opacity = e.cloud_opacity;
        response.cloud_albedo = e.cloud_albedo;
        response.cloud_height = e.cloud_height;
        response.cloud_offset = {e.cloud_offset_x, e.cloud_offset_y};
        inst.material.surface_response = response;
      }
      inst.material.terminator_wrap = e.terminator_wrap;
      inst.material.limb_darkening = e.limb_darkening;
      inst.material.limb_darkening_q = e.limb_darkening_q;
      inst.material.band_shear = e.band_shear;
      inst.material.band_waves = e.band_waves;
      inst.material.band_drift = e.band_drift;
      inst.material.band_turbulence = e.band_turbulence;
      inst.material.orbital_beaming = e.orbital_beaming;
      inst.material.forward_scatter = e.forward_scatter;
      // Emission volume: the entity texture is the emission image and
      // the volume branch requires transparency (mirrors runtime host).
      if (e.volume_depth > 0.f && inst.material.texture) {
        SurfaceEffect3D effect;
        effect.next_texture = inst.material.texture;
        effect.volume_depth = e.volume_depth;
        effect.volume_density = e.volume_density;
        effect.volume_seed = e.volume_seed;
        effect.volume_steps = e.volume_steps;
        effect.volume_scatter = e.volume_scatter;
        effect.flow_phase = e.volume_flow;
        effect.distortion = e.volume_distort;
        effect.blend = e.volume_blend;
        effect.occlude = e.volume_occlude;
        effect.flow_rate = e.volume_flow_rate;
        if (!e.volume_image2.empty())
          if (const auto alt = scene3_tex(shell, e.volume_image2))
            effect.next_texture = alt;
        inst.material.surface_effect = effect;
        inst.material.transparent = true;
      }
      if (e.atmo_strength != 0.f)
        inst.material.atmosphere =
            Atmosphere3D{{e.atmo_r, e.atmo_g, e.atmo_b}, e.atmo_strength,
                         e.atmo_power, e.atmo_night};
      inst.visible_range = e.visible_range;
      inst.visible_fade = e.visible_fade;
      inst.lod_pixels = e.lod_pixels;
      inst.lod_fade = e.lod_fade;
      for (const auto &spec : e.lod_meshes)
        if (auto lod_mesh = scene3_mesh(shell, spec))
          inst.lod_meshes.push_back(std::move(lod_mesh));
      if (inst.lod_meshes.empty()) inst.lod_pixels = 32.f;
      inst.lod_group = e.lod_group;
      inst.lod_group_pixels = e.lod_proxy_pixels;
      // An unresolvable proxy spec drops the collapse, not the group.
      if (auto proxy_mesh = scene3_mesh(shell, e.lod_proxy))
        inst.lod_group_proxy = std::move(proxy_mesh);
      inst.material.light_intensity = doc.light_intensity;
      inst.material.linear_light = true;
      // Selected entity highlight: a bright grazing-angle shell marks
      // the pick (rim_power>0 renders the rim band).
      if (i == shell.scene3_sel) {
        inst.material.ambient = .55f;
        inst.material.rim_power = 2.5f;
      }
      instances.push_back(std::move(inst));
    }
    const Quaternion inv{-cam_q.x, -cam_q.y, -cam_q.z, cam_q.w};
    const Vec3 light_cam =
        rotate_vec(inv, {doc.light_x, doc.light_y, doc.light_z});
    for (auto &inst : instances)
      for (std::size_t li = 0; li < doc.lights.size() && li < 2; ++li) {
        const auto &l = doc.lights[li];
        inst.material.additional_lights[li] = DirectionalLight3D{
            rotate_vec(inv, {l.dir_x, l.dir_y, l.dir_z}),
            {l.r, l.g, l.b}, l.intensity};
      }
    std::vector<PointLight3D> point_lights;
    point_lights.reserve(doc.point_lights.size());
    for (const auto &l : doc.point_lights)
      point_lights.push_back(PointLight3D{{l.x, l.y, l.z},
                                          {l.r, l.g, l.b},
                                          l.intensity, l.range,
                                          {l.spot_x, l.spot_y, l.spot_z},
                                          l.spot_inner, l.spot_outer});
    std::optional<ShadowMap3D> shadow_map;
    if (doc.shadow_extent > 0.f)
      shadow_map = ShadowMap3D{doc.shadow_extent, doc.shadow_distance,
                               doc.shadow_depth, doc.shadow_strength,
                               doc.shadow_bias, doc.shadow_resolution};
    if (auto scene = Scene3D::create(cam, std::move(instances), light_cam,
                                     std::move(point_lights), shadow_map)) {
      Scene3DView view{std::move(scene), pv};
      view.options.exposure = doc.exposure;
      view.options.bloom_strength = doc.bloom;
      view.options.bloom_threshold = doc.bloom_threshold;
      view.options.contrast = doc.contrast;
      view.options.saturation = doc.saturation;
      view.options.sharpen = doc.sharpen;
      view.options.quality =
          doc.quality == "low"      ? RenderQuality3D::Low
          : doc.quality == "medium" ? RenderQuality3D::Medium
          : doc.quality == "ultra"  ? RenderQuality3D::Ultra
                                    : RenderQuality3D::High;
      view.options.debug_view =
          doc.debug_view == "unlit"     ? DebugView3D::Unlit
          : doc.debug_view == "albedo"  ? DebugView3D::Albedo
          : doc.debug_view == "normals" ? DebugView3D::Normals
          : doc.debug_view == "roughness" ? DebugView3D::Roughness
          : doc.debug_view == "metallic"  ? DebugView3D::Metallic
          : doc.debug_view == "emissive"  ? DebugView3D::Emissive
          : doc.debug_view == "lighting"  ? DebugView3D::LightingOnly
          : doc.debug_view == "lod"       ? DebugView3D::Lod
          : doc.debug_view == "residency" ? DebugView3D::Residency
                                        : DebugView3D::Lit;
      // Preview clock — animated material terms (bandDrift, volume
      // flowRate) need a nonzero scene time to show their motion.
      static float preview_time = 0.f;
      static std::chrono::steady_clock::time_point preview_last{};
      const auto now = std::chrono::steady_clock::now();
      if (preview_last.time_since_epoch().count() > 0)
        preview_time += std::chrono::duration<float>(now - preview_last).count();
      preview_last = now;
      view.options.time = preview_time;
      out.overlay.push_back(std::move(view));
    }
  }
  out.overlay.push_back(StrokedRectangle{pv, panel_edge});
  out.overlay.push_back(
      Text{{pv.x + 8 * s, pv.y + 6 * s},
           "click: pick | LMB drag: move/rotate/scale (mode above) | "
           "RMB drag: orbit | wheel: fov",
           muted, font - 2});

  // Fields under the list: entity props, then document-level props.
  const auto *entity = selected_scene3_entity(shell);
  float fx = list_rect.x;
  float fy = list_rect.y + list_rect.height + 16 * s;
  const float col_w = body.width * 0.44f;
  const auto field = [&](UiRect &hit, const char *label,
                         const std::string &value, bool editing,
                         const char *hint) {
    out.overlay.push_back(Text{{fx, fy}, label, muted, font});
    hit = {fx + 90 * s, fy - 4 * s, col_w - 90 * s, (font + 8) * s};
    field_box(out, hit, editing ? shell.scene3_buffer : value, editing,
              hint, font, s);
    fy += hit.height + 4 * s;
  };
  const auto fmt3 = [](float a, float b, float c) {
    return std::to_string(static_cast<int>(a)) + "," +
           std::to_string(static_cast<int>(b)) + "," +
           std::to_string(static_cast<int>(c));
  };
  const auto ed = [&](int f) {
    return shell.editing_scene3 && shell.scene3_field == f;
  };
  field(shell.hit3_name, "name", entity ? entity->name : "", ed(1),
        "entity name");
  field(shell.hit3_mesh, "mesh", entity ? entity->mesh : "", ed(4),
        "box:sx,sy,sz | sphere:c,r | annulus:i,o[,s] | .obj path");
  field(shell.hit3_pos, "pos",
        entity ? fmt3(entity->x, entity->y, entity->z) : "", ed(2),
        "e.g. 0,1.5,-4");
  field(shell.hit3_rot, "rot",
        entity ? fmt3(entity->yaw_deg, entity->pitch_deg,
                      entity->roll_deg)
               : "",
        ed(5), "yaw,pitch,roll degrees");
  field(shell.hit3_scale, "scale",
        entity ? std::to_string(entity->scale) : "", ed(6),
        "uniform scale > 0");
  field(shell.hit3_vel, "vel",
        entity ? fmt3(entity->vx, entity->vy, entity->vz) : "", ed(3),
        "units/sec");
  field(shell.hit3_color, "color",
        entity ? std::to_string(entity->r) + "," +
                     std::to_string(entity->g) + "," +
                     std::to_string(entity->b)
               : "",
        ed(7), "e.g. 255,200,60");
  field(shell.hit3_tex, "texture", entity ? entity->texture : "", ed(8),
        "content-relative image path");
  field(shell.hit3_opacity, "opacity",
        entity ? std::to_string(entity->opacity) : "", ed(9),
        "0..1 - below 1 renders transparent");
  field(shell.hit3_solid, "solid",
        entity ? (entity->solid ? "true" : "false") : "", ed(11),
        "blocker - things push out of it");
  field(shell.hit3_dbl, "doubleSided",
        entity ? (entity->double_sided ? "true" : "false") : "", ed(12),
        "render back faces (fins, paper)");
  field(shell.hit3_gravs, "gravScale",
        entity ? std::to_string(entity->gravity_scale) : "", ed(10),
        "gravity multiplier - 0 floats");
  field(shell.hit3_ttl, "ttl",
        entity ? std::to_string(entity->ttl) : "", ed(13),
        "seconds until despawn - 0 immortal");
  field(shell.hit3_data, "data", entity ? entity->data : "", ed(14),
        "freeform game data");
  field(shell.hit3_parent, "parent", entity ? entity->parent : "",
        ed(15), "entity name to follow");
  field(shell.hit3_vfx, "vfx", entity ? entity->vfx : "", ed(16),
        "named emitter attached on spawn");
  // Second column: document-level fields.
  fx = list_rect.x + col_w + 20 * s;
  fy = list_rect.y + list_rect.height + 16 * s;
  field(shell.hit3_cam, "cam pos", fmt3(doc.cam_x, doc.cam_y, doc.cam_z),
        ed(20), "e.g. 0,3,10");
  field(shell.hit3_camrot, "cam yaw,pitch",
        std::to_string(static_cast<int>(doc.cam_yaw_deg)) + "," +
            std::to_string(static_cast<int>(doc.cam_pitch_deg)),
        ed(21), "degrees - or drag RMB in preview");
  field(shell.hit3_fov, "cam fov", std::to_string(doc.fov_deg), ed(22),
        "10..140 - or wheel in preview");
  field(shell.hit3_clip, "clipPlanes",
        std::to_string(doc.near_plane) + "," + std::to_string(doc.far_plane),
        ed(71), "near,far - depth range");
  field(shell.hit3_lightdir, "light dir",
        fmt3(doc.light_x, doc.light_y, doc.light_z), ed(23),
        "world-space direction");
  field(shell.hit3_lightint, "light int",
        std::to_string(doc.light_intensity), ed(24), "multiplier");
  field(shell.hit3_grav, "gravity", std::to_string(doc.gravity), ed(25),
        "units/s^2 pulling -Y");
  field(shell.hit3_ground, "groundY", std::to_string(doc.ground_y),
        ed(26), "rest plane height");
  field(shell.hit3_bounds, "bounds", std::to_string(doc.bounds), ed(27),
        "XZ half-extent clamp");
  field(shell.hit3_bg, "background",
        std::to_string((int)doc.bg_r) + "," + std::to_string((int)doc.bg_g) +
            "," + std::to_string((int)doc.bg_b),
        ed(28), "clear color r,g,b");
  field(shell.hit3_music, "music", doc.music, ed(29),
        "content-relative track");
  const auto fill_dir = [&doc, &fmt3](std::size_t slot) {
    return slot < doc.lights.size()
               ? fmt3(doc.lights[slot].dir_x, doc.lights[slot].dir_y,
                      doc.lights[slot].dir_z)
               : std::string{};
  };
  const auto fill_tint = [&doc](std::size_t slot) {
    if (slot >= doc.lights.size()) return std::string{};
    const auto &l = doc.lights[slot];
    return std::to_string(l.r) + "," + std::to_string(l.g) + "," +
           std::to_string(l.b) + "," + std::to_string(l.intensity);
  };
  field(shell.hit3_filla_dir, "fillA dir", fill_dir(0), ed(30),
        "x,y,z - empty removes slot");
  field(shell.hit3_filla_tint, "fillA tint", fill_tint(0), ed(31),
        "r,g,b,intensity 0..1");
  field(shell.hit3_fillb_dir, "fillB dir", fill_dir(1), ed(32),
        "x,y,z - empty removes slot");
  field(shell.hit3_fillb_tint, "fillB tint", fill_tint(1), ed(33),
        "r,g,b,intensity 0..1");
  // Third column under the preview: the PBR/material extensions and the
  // view's post-processing controls.
  fx = pv.x;
  fy = list_rect.y + list_rect.height + 16 * s;
  const auto fmt_pair = [](float a, float b) {
    return std::to_string(a) + "," + std::to_string(b);
  };
  field(shell.hit3_pbr, "metal,rough",
        entity ? fmt_pair(entity->metallic, entity->roughness) : "",
        ed(40), "0..1 - hull metalness / gloss");
  field(shell.hit3_mr, "mrMap",
        entity ? entity->metallic_roughness : "", ed(41),
        "packed G=rough B=metal map (empty = scalars)");
  field(shell.hit3_emis, "emisMap", entity ? entity->emissive : "",
        ed(42), "night lights / engine glow map");
  field(shell.hit3_emit, "emit s,r,g,b",
        entity ? std::to_string(entity->emissive_strength) + "," +
                     std::to_string(entity->emissive_r) + "," +
                     std::to_string(entity->emissive_g) + "," +
                     std::to_string(entity->emissive_b)
               : "",
        ed(43), "0 disables emission");
  field(shell.hit3_night, "nightEmis",
        entity ? std::to_string(entity->night_emissive) : "", ed(44),
        "0..1 - 1 emits only past terminator");
  field(shell.hit3_env, "envMap", entity ? entity->environment : "",
        ed(45), "equirect radiance for IBL");
  field(shell.hit3_envstr, "envStr",
        entity ? std::to_string(entity->environment_strength) : "",
        ed(46), "0..16 - 0 disables IBL");
  field(shell.hit3_cutout, "cutout",
        entity ? std::to_string(entity->alpha_cutout) : "", ed(47),
        "0..1 alpha discard threshold");
  field(shell.hit3_tile, "uvTile",
        entity ? fmt_pair(entity->uv_tile_x, entity->uv_tile_y) : "",
        ed(48), "u,v repeat - 1,1 disables");
  field(shell.hit3_atmo, "atmo s,p,floor",
        entity ? std::to_string(entity->atmo_strength) + "," +
                     std::to_string(entity->atmo_power) + "," +
                     std::to_string(entity->atmo_night)
               : "",
        ed(49), "limb scatter - strength 0 off");
  field(shell.hit3_atmotint, "atmoTint",
        entity ? std::to_string(entity->atmo_r) + "," +
                     std::to_string(entity->atmo_g) + "," +
                     std::to_string(entity->atmo_b)
               : "",
        ed(50), "rim color r,g,b 0..1");
  field(shell.hit3_range, "visRange",
        entity ? std::to_string(entity->visible_range) : "", ed(51),
        "distance cull, world units - 0 always");
  field(shell.hit3_visfade, "visFade",
        entity ? std::to_string(entity->visible_fade) : "", ed(67),
        "screen-door fade width 0..0.5 x range - 0 = hard cut");
  field(shell.hit3_surfmaps, "surfMaps",
        entity ? entity->normal_map + "," + entity->properties_map + "," +
                     entity->cloud_map
               : "",
        ed(53), "normal,properties,cloud paths - any subset");
  field(shell.hit3_surfshape, "surfShape",
        entity ? fmt_pair(entity->normal_strength, entity->relief) : "",
        ed(54), "normal strength 0..2, relief 0..0.02");
  field(shell.hit3_clouddeck, "cloudDeck",
        entity ? std::to_string(entity->cloud_opacity) + "," +
                     std::to_string(entity->cloud_albedo) + "," +
                     fmt_pair(entity->cloud_offset_x, entity->cloud_offset_y) +
                     (entity->cloud_height != 0.f
                          ? "," + std::to_string(entity->cloud_height)
                          : "")
               : "",
        ed(55), "shadow opacity, deck albedo 0..1, uv offset[, height 0..0.1]");
  field(shell.hit3_termwrap, "termWrap",
        entity ? std::to_string(entity->terminator_wrap) : "", ed(56),
        "wrap-diffuse 0..1 - 0 keeps Lambert");
  field(shell.hit3_limbdark, "limbDark",
        entity ? std::to_string(entity->limb_darkening) + "," +
                     std::to_string(entity->limb_darkening_q)
               : "",
        ed(57),
        "limb darkening u 0..1[, quadratic q 0..1] - sun ~0.6");
  field(shell.hit3_lods, "meshLods",
        entity ? [&] {
          std::string v;
          for (const auto &s : entity->lod_meshes) {
            if (!v.empty()) v += ",";
            v += s;
          }
          return v;
        }()
               : "",
        ed(58), "csv mesh specs, coarser per level - max 8");
  field(shell.hit3_lodpixels, "lodPixels",
        entity ? std::to_string(entity->lod_pixels) : "", ed(59),
        "px diameter for LOD 0->1 - halves per level 1..4096");
  field(shell.hit3_lodfade, "lodFade",
        entity ? std::to_string(entity->lod_fade) : "", ed(66),
        "screen-door crossfade width 0..0.5 - 0 = hard switch");
  field(shell.hit3_lodgroup, "lodGroup",
        entity && !entity->lod_group.empty()
            ? entity->lod_group + ";" + entity->lod_proxy + ";" +
                  std::to_string(entity->lod_proxy_pixels)
            : "", ed(69),
        "name;proxy spec;px - group collapses to one proxy draw");
  field(shell.hit3_bandshear, "bandShear",
        entity ? std::to_string(entity->band_shear) : "", ed(60),
        "latitude uv shear -0.5..0.5 - gas-giant banding");
  field(shell.hit3_bandwaves, "bandWaves",
        entity ? std::to_string(entity->band_waves) : "", ed(68),
        "zonal jet harmonic 0..1 - layered on bandShear");
  field(shell.hit3_banddrift, "bandDrift",
        entity ? std::to_string(entity->band_drift) + "," +
                     std::to_string(entity->band_turbulence)
               : "",
        ed(70),
        "drift uv/s -0.25..0.25[,turbulence rad/s -8..8]");
  field(shell.hit3_orbitbeam, "orbitalBeam",
        entity ? std::to_string(entity->orbital_beaming) : "", ed(61),
        "approaching-lane brightening -1..1 - accretion discs");
  field(shell.hit3_starkelvin, "starKelvin",
        entity ? std::to_string(static_cast<long long>(entity->star_kelvin)) : "", ed(62),
        "photosphere kelvin 100..100000 - blackbody tint + limb");
  field(shell.hit3_accretion, "accretion",
        entity ? std::to_string(entity->accretion[0]) + "," +
                     std::to_string(entity->accretion[1]) + "," +
                     std::to_string(entity->accretion[2]) + "," +
                     std::to_string(entity->accretion[3])
               : "",
        ed(63), "inner,outer,kelvin,beaming - annulus disc preset");
  field(shell.hit3_fwdscatter, "fwdScatter",
        entity ? std::to_string(entity->forward_scatter) : "", ed(64),
        "backlit brightening -1..1 - dusty rings, icy opposition");
  field(shell.hit3_volume, "volume",
        entity && entity->volume_depth > 0.f
            ? std::to_string(entity->volume_depth) + "," +
                  std::to_string(entity->volume_density) + "," +
                  std::to_string(entity->volume_seed) + "," +
                  std::to_string(entity->volume_steps) + "," +
                  std::to_string(entity->volume_scatter) + "," +
                  std::to_string(entity->volume_flow) + "," +
                  std::to_string(entity->volume_distort) + "," +
                  std::to_string(entity->volume_blend) +
                  (entity->volume_image2.empty() &&
                           entity->volume_occlude == 0.f &&
                           entity->volume_flow_rate == 0.f
                       ? ""
                       : "," + entity->volume_image2 + "," +
                             std::to_string(entity->volume_occlude) +
                             (entity->volume_flow_rate == 0.f
                                  ? ""
                                  : "," + std::to_string(
                                        entity->volume_flow_rate)))
            : "",
        ed(65),
        "depth,density,seed,steps,scatter[,flow,distort[,blend[,image2[,occlude[,flowRate]]]]] - emission volume; 0 clears");
  field(shell.hit3_exposure, "exposure",
        std::to_string(doc.exposure), ed(34), "linear HDR multiplier");
  field(shell.hit3_bloom, "bloom s,t",
        fmt_pair(doc.bloom, doc.bloom_threshold), ed(35),
        "strength,threshold - 0 off");
  field(shell.hit3_grade, "c,s,sharp",
        std::to_string(doc.contrast) + "," + std::to_string(doc.saturation) +
            "," + std::to_string(doc.sharpen),
        ed(36), "contrast,saturation,sharpen");
  field(shell.hit3_quality, "quality", doc.quality, ed(37),
        "low|medium|high|ultra");
  field(shell.hit3_plights, "pointLights",
        [&] {
          std::string v;
          for (const auto &l : doc.point_lights) {
            if (!v.empty()) v += "; ";
            v += std::to_string(l.x) + "," + std::to_string(l.y) + "," +
                 std::to_string(l.z) + "," + std::to_string(l.r) + "," +
                 std::to_string(l.g) + "," + std::to_string(l.b) + "," +
                 std::to_string(l.intensity) + "," +
                 std::to_string(l.range);
            if (l.spot_x != 0.f || l.spot_y != 0.f || l.spot_z != 0.f)
              v += "," + std::to_string(l.spot_x) + "," +
                   std::to_string(l.spot_y) + "," + std::to_string(l.spot_z) +
                   "," + std::to_string(l.spot_inner) + "," +
                   std::to_string(l.spot_outer);
          }
          return v;
        }(),
        ed(38), "x,y,z,r,g,b,intensity,range[,dx,dy,dz,inner,outer]; ... - max 4, empty clears");
  field(shell.hit3_debug, "debugView", doc.debug_view, ed(39),
        "lit|unlit|albedo|normals|roughness|metallic|emissive|lighting|lod|residency");
  field(shell.hit3_shadow, "shadowMap",
        doc.shadow_extent > 0.f
            ? std::to_string(doc.shadow_extent) + "," +
                  std::to_string(doc.shadow_distance) + "," +
                  std::to_string(doc.shadow_depth)
            : "",
        ed(52), "extent,dist,depth[,strength,bias[,res]] - empty disables");
}

std::vector<std::size_t> scene_draw_order(const engine::SceneDocument &doc) {
  std::vector<std::size_t> order(doc.entities.size());
  for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
    return doc.entities[a].layer < doc.entities[b].layer;
  });
  return order;
}

// Writes the brush value into the selected tilemap's cell under a
// scene-space point; painting below existing rows grows the grid.
void paint_tile_at(Shell &shell, float wx, float wy) {
  auto *tmap = scene_tile(shell);
  if (tmap == nullptr) return;
  auto &tm = *tmap;
  if (tm.tile_w <= 0 || tm.tile_h <= 0 || tm.columns <= 0) return;
  const int cx =
      static_cast<int>(std::floor((wx - tm.x) / tm.tile_w));
  const int cy =
      static_cast<int>(std::floor((wy - tm.y) / tm.tile_h));
  // Brush footprint: an NxN block centered on the clicked cell (1 = the
  // single-cell stamp). The same one-undo-step-per-stroke rule applies.
  const int brush = std::clamp(shell.scene_paint_brush, 1, 8);
  const int lo = -(brush / 2), hi = brush - brush / 2;
  for (int dy = lo; dy < hi; ++dy)
    for (int dx = lo; dx < hi; ++dx) {
      const int px = cx + dx, py = cy + dy;
      if (px < 0 || py < 0 || px >= tm.columns) continue;
      const std::size_t idx =
          static_cast<std::size_t>(py) * tm.columns + px;
      if (idx >= tm.cells.size())
        tm.cells.resize(static_cast<std::size_t>(py + 1) * tm.columns, -1);
      if (tm.cells[idx] == shell.scene_paint_cell) continue;
      tm.cells[idx] = shell.scene_paint_cell;
      shell.scene_modified = true;
    }
}

// Flood-fills the 4-connected region of same-valued cells under the click
// with the brush value — the FILL toggle in paint mode. Bounded to the
// map's existing cells (fills never extend the grid).
void fill_tile_at(Shell &shell, float wx, float wy) {
  auto *tmap = scene_tile(shell);
  if (tmap == nullptr) return;
  auto &tm = *tmap;
  if (tm.tile_w <= 0 || tm.tile_h <= 0 || tm.columns <= 0 ||
      tm.cells.empty())
    return;
  const int cx =
      static_cast<int>(std::floor((wx - tm.x) / tm.tile_w));
  const int cy =
      static_cast<int>(std::floor((wy - tm.y) / tm.tile_h));
  const int rows = static_cast<int>(tm.cells.size()) / tm.columns;
  if (cx < 0 || cy < 0 || cx >= tm.columns || cy >= rows) return;
  const int target = tm.cells[static_cast<std::size_t>(cy) * tm.columns +
                              cx];
  if (target == shell.scene_paint_cell) return;
  std::vector<char> seen(tm.cells.size(), 0);
  std::vector<int> stack{cy * tm.columns + cx};
  while (!stack.empty()) {
    const int flat = stack.back();
    stack.pop_back();
    if (seen[static_cast<std::size_t>(flat)]) continue;
    seen[static_cast<std::size_t>(flat)] = 1;
    if (tm.cells[static_cast<std::size_t>(flat)] != target) continue;
    tm.cells[static_cast<std::size_t>(flat)] = shell.scene_paint_cell;
    shell.scene_modified = true;
    const int px = flat % tm.columns, py = flat / tm.columns;
    for (const auto [ox, oy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
      const int nx = px + ox, ny = py + oy;
      if (nx < 0 || ny < 0 || nx >= tm.columns || ny >= rows) continue;
      const int nflat = ny * tm.columns + nx;
      if (static_cast<std::size_t>(nflat) < tm.cells.size() &&
          !seen[static_cast<std::size_t>(nflat)])
        stack.push_back(nflat);
    }
  }
}

void render_scene(DrawList &out, Shell &shell, UiRect body, float s) {
  float x = body.x + 22 * s;
  float y = body.y + 18 * s;
  const int font = static_cast<int>(13 * s);
  heading(out, x, y, "SCENE AUTHORING");
  if (!shell.project) {
    line(out, x, y, "open project", "none - open one in Projects", font);
    shell.hit_scene_add = shell.hit_scene_del = shell.hit_scene_save =
        shell.hit_scene_undo = shell.hit_scene_redo = shell.hit_scene_dup = {};
    shell.hit_scene_name = shell.hit_scene_pos = shell.hit_scene_vel =
        shell.hit_scene_sprite = shell.hit_scene_size =
            shell.hit_scene_color = shell.hit_scene_layer =
                shell.hit_scene_parallax = shell.hit_scene_text =
                    shell.hit_scene_grav = shell.hit_scene_gravity =
                        shell.hit_scene_solid = shell.hit_scene_bg =
                            shell.hit_scene_frames = shell.hit_scene_fps =
                                shell.hit_scene_rot = shell.hit_scene_ttl =
                                    shell.hit_scene_flipx =
                                        shell.hit_scene_flipy =
                                            shell.hit_scene_visible =
                                                shell.hit_scene_oneway =
                                                    shell.hit_scene_up =
                                                        shell.hit_scene_down =
                                                            shell.hit_scene_data =
                                                                shell.hit_scene_opacity =
                                                                    {};
    shell.hit_scene_tilemap = shell.hit_scene_tilesel =
        shell.hit_scene_tiledel = shell.hit_scene_tileset =
        shell.hit_scene_tilesize = shell.hit_scene_tilecols =
            shell.hit_scene_tilecollide = shell.hit_scene_tilelayer =
                shell.hit_scene_tilepar = shell.hit_scene_tilecells =
                shell.hit_scene_tileorigin = shell.hit_scene_tilename =
                    shell.hit_scene_paint = shell.hit_scene_paintcell =
                        shell.hit_scene_brushsz = shell.hit_scene_fill =
                        shell.hit_scene_music = shell.hit_scene_spin =
                            shell.hit_scene_worldsize =
                                shell.hit_scene_bounce =
                                    shell.hit_scene_parent =
                                        shell.hit_scene_fcols =
                                            shell.hit_scene_animloop =
                                                shell.hit_scene_vfx = {};
    shell.scene_preview = shell.scene_rows = {};
    return;
  }
  if (shell.scene_dirty.exchange(false)) load_scene(shell);
  line(out, x, y, "project", shell.project->name, font);
  line(out, x, y, "document", "editor/scene.json", font);
  line(out, x, y, "entities",
       std::to_string(shell.scene_doc.entities.size()), font);
  y += 4 * s;

  // Action row.
  const float bh = (font + 14) * s;
  shell.hit_scene_add = {x, y, 148 * s, bh};
  shell_button(out, shell.hit_scene_add, "ADD ENTITY", false, font, s);
  shell.hit_scene_del = {x + 158 * s, y, 100 * s, bh};
  shell_button(out, shell.hit_scene_del, "DELETE",
               shell.selected_entity < shell.scene_doc.entities.size(), font,
               s);
  shell.hit_scene_save = {x + 268 * s, y, 90 * s, bh};
  shell_button(out, shell.hit_scene_save,
               shell.scene_modified ? "SAVE *" : "SAVE", shell.scene_modified,
               font, s);
  shell.hit_scene_undo = {x + 368 * s, y, 90 * s, bh};
  shell_button(out, shell.hit_scene_undo, "UNDO",
               shell.scene_history.can_undo(), font, s);
  shell.hit_scene_redo = {x + 468 * s, y, 90 * s, bh};
  shell_button(out, shell.hit_scene_redo, "REDO",
               shell.scene_history.can_redo(), font, s);
  shell.hit_scene_dup = {x + 568 * s, y, 128 * s, bh};
  shell_button(out, shell.hit_scene_dup, "DUPLICATE",
               shell.selected_entity < shell.scene_doc.entities.size(), font,
               s);
  // Doc order is the same-layer draw order; these nudge the selection.
  const bool can_up = shell.selected_entity > 0 &&
                      shell.selected_entity < shell.scene_doc.entities.size();
  const bool can_down =
      shell.selected_entity + 1 < shell.scene_doc.entities.size();
  shell.hit_scene_up = {x + 706 * s, y, 70 * s, bh};
  shell_button(out, shell.hit_scene_up, "UP", !can_up, font, s);
  shell.hit_scene_down = {x + 786 * s, y, 80 * s, bh};
  shell_button(out, shell.hit_scene_down, "DOWN", !can_down, font, s);
  // Tilemap stack controls: TILES + adds a layer, MAP cycles which one
  // the fields/PAINT edit, TILES - removes the selected layer.
  const auto ntiles = shell.scene_doc.tilemaps.size();
  shell.hit_scene_tilemap = {x + 876 * s, y, 86 * s, bh};
  shell_button(out, shell.hit_scene_tilemap, "TILES +", false, font, s);
  shell.hit_scene_tilesel = {x + 970 * s, y, 104 * s, bh};
  const auto sel_tile = std::min(shell.scene_tile_index, ntiles - 1);
  const std::string map_label =
      ntiles ? "MAP " + std::to_string(sel_tile + 1) + "/" +
                   std::to_string(ntiles) +
                   (shell.scene_doc.tilemaps[sel_tile].name.empty()
                        ? ""
                        : ":" + shell.scene_doc.tilemaps[sel_tile].name)
             : "MAP -";
  shell_button(out, shell.hit_scene_tilesel, map_label.c_str(),
               ntiles == 0, font, s);
  shell.hit_scene_tiledel = {x + 1082 * s, y, 86 * s, bh};
  shell_button(out, shell.hit_scene_tiledel, "TILES -", ntiles == 0, font,
               s);
  shell.hit_scene_paint = {x + 1176 * s, y, 100 * s, bh};
  shell_button(out, shell.hit_scene_paint,
               shell.scene_paint ? "PAINT *" : "PAINT", ntiles == 0, font,
               s);
  shell.hit_scene_fill = {x + 1284 * s, y, 70 * s, bh};
  shell_button(out, shell.hit_scene_fill,
               shell.scene_paint_fill ? "FILL *" : "FILL",
               ntiles == 0 || !shell.scene_paint, font, s);
  y += bh + 14 * s;

  // Entity list (left) + scene preview (right).
  const UiRect list_rect{x, y, body.width * 0.34f, body.height * 0.5f};
  out.overlay.push_back(FilledRectangle{list_rect, {6, 16, 26, 255}});
  out.overlay.push_back(StrokedRectangle{list_rect, panel_edge});
  shell.scene_rows = list_rect;
  shell.entity_list.configure(shell.scene_doc.entities.size(), 22 * s,
                              list_rect.height);
  const auto range = shell.entity_list.visible_range();
  float ry = list_rect.y - shell.entity_list.scroll_offset +
             range.first * shell.entity_list.row_height;
  for (std::size_t i = range.first; i < range.last;
       ++i, ry += shell.entity_list.row_height) {
    const auto &e = shell.scene_doc.entities[i];
    const UiRect row{list_rect.x, ry, list_rect.width,
                     shell.entity_list.row_height};
    if (i == shell.selected_entity)
      out.overlay.push_back(FilledRectangle{row, row_selected});
    else if (row.contains(Point{shell.pointer_x, shell.pointer_y}))
      out.overlay.push_back(FilledRectangle{row, row_hover});
    out.overlay.push_back(
        Text{{row.x + 8 * s, row.y + 4 * s},
             e.name + "  (" + std::to_string(static_cast<int>(e.x)) + "," +
                 std::to_string(static_cast<int>(e.y)) + ")",
             ink, font, 0, list_rect});
  }

  // Preview: a 1280x720 scene area scaled into the pane; click to place the
  // selected entity.
  const float px = list_rect.x + list_rect.width + 16 * s;
  const float pw = body.x + body.width - px - 22 * s;
  const float ph = pw * 720.f / 1280.f;
  shell.scene_preview = {px, y, pw, std::min(ph, body.height * 0.44f)};
  const auto &pv = shell.scene_preview;
  out.overlay.push_back(FilledRectangle{
      pv, {shell.scene_doc.bg_r, shell.scene_doc.bg_g, shell.scene_doc.bg_b,
           255}});
  out.overlay.push_back(StrokedRectangle{pv, panel_edge});
  const float sx = pv.width / 1280.f, sy = pv.height / 720.f;
  const auto preview_now = std::chrono::duration<float>(
      std::chrono::steady_clock::now().time_since_epoch())
                               .count();
  // Tilemaps paint at their layers, splitting the entity pass — mirrors
  // the runtime's draw ordering (stable doc order within a layer).
  std::vector<std::size_t> tm_order(shell.scene_doc.tilemaps.size());
  for (std::size_t i = 0; i < tm_order.size(); ++i) tm_order[i] = i;
  std::stable_sort(tm_order.begin(), tm_order.end(), [&](auto a, auto b) {
    return shell.scene_doc.tilemaps[a].layer <
           shell.scene_doc.tilemaps[b].layer;
  });
  std::size_t next_tm = 0;
  // Content-cached tileset decode shared by the map painter and the
  // paint-mode tile picker.
  const auto load_tileset = [&](const std::string &path)
      -> std::shared_ptr<const RgbaImage> {
    if (path.empty()) return nullptr;
    const auto full = (shell.project->root / "packages" /
                       shell.project->id / "content" / path)
                          .lexically_normal();
    const auto key = full.generic_string();
    if (const auto it = shell.scene_sprites.find(key);
        it != shell.scene_sprites.end())
      return it->second;
    std::shared_ptr<const RgbaImage> img;
    try {
      img = decode_rgba_image(full, 1024);
    } catch (const std::exception &) {
    }
    shell.scene_sprites[key] = img;
    return img;
  };
  const auto draw_tilemap = [&](std::size_t which) {
    const auto &tm = shell.scene_doc.tilemaps[which];
    if (tm.columns <= 0 || tm.tile_w <= 0 || tm.tile_h <= 0) return;
    const auto tiles = load_tileset(tm.tileset);
    const int set_cols =
        tiles ? tiles->width() / tm.tile_w : 0;
    const int rows = static_cast<int>(tm.cells.size() / tm.columns);
    for (int cy = 0; cy < rows; ++cy)
      for (int cx = 0; cx < tm.columns; ++cx) {
        const int cell = tm.cells[cy * tm.columns + cx];
        if (cell < 0) continue;
        const UiRect rect{pv.x + (tm.x + cx * tm.tile_w) * sx,
                          pv.y + (tm.y + cy * tm.tile_h) * sy,
                          tm.tile_w * sx, tm.tile_h * sy};
        if (tiles && set_cols > 0) {
          out.overlay.push_back(Image{
              tiles, rect,
              UiRect{static_cast<float>((cell % set_cols) * tm.tile_w),
                     static_cast<float>((cell / set_cols) * tm.tile_h),
                     static_cast<float>(tm.tile_w),
                     static_cast<float>(tm.tile_h)}});
        } else {
          // No tileset: checkerboard so the grid remains visible.
          const std::uint8_t v = static_cast<std::uint8_t>(
              ((cx + cy) & 1) ? 70 : 110);
          out.overlay.push_back(FilledRectangle{
              rect,
              {v, v, static_cast<std::uint8_t>(v + 30), 200}});
        }
      }
  };
  // Every tilemap at or below this entity layer draws before it.
  const auto draw_tilemaps_below = [&](int layer) {
    while (next_tm < tm_order.size() &&
           shell.scene_doc.tilemaps[tm_order[next_tm]].layer <= layer)
      draw_tilemap(tm_order[next_tm++]);
  };
  // Parent links: child center -> parent center, under the entity pass.
  for (const auto &e : shell.scene_doc.entities) {
    if (e.parent.empty()) continue;
    for (const auto &p : shell.scene_doc.entities)
      if (p.name == e.parent) {
        out.overlay.push_back(
            Line{{pv.x + (e.x + e.w * .5f) * sx,
                  pv.y + (e.y + e.h * .5f) * sy},
                 {pv.x + (p.x + p.w * .5f) * sx,
                  pv.y + (p.y + p.h * .5f) * sy},
                 {140, 200, 255, 110}});
        break;
      }
  }
  for (const auto i : scene_draw_order(shell.scene_doc)) {
    const auto &e = shell.scene_doc.entities[i];
    draw_tilemaps_below(e.layer);
    const UiRect rect{pv.x + e.x * sx, pv.y + e.y * sy, e.w * sx, e.h * sy};
    std::shared_ptr<const RgbaImage> sprite;
    if (!e.sprite.empty()) {
      const auto full = (shell.project->root / "packages" /
                         shell.project->id / "content" / e.sprite)
                            .lexically_normal();
      const auto key = full.generic_string();
      if (const auto it = shell.scene_sprites.find(key);
          it != shell.scene_sprites.end()) {
        sprite = it->second;
      } else {
        try {
          sprite = decode_rgba_image(full, 1024);
        } catch (const std::exception &) {
        }
        shell.scene_sprites[key] = sprite;
      }
    }
    if (sprite) {
      Image img{sprite, rect};
      if (e.frames > 1) {
        const int cols =
            e.fcols > 0 ? std::min(e.fcols, e.frames) : e.frames;
        const int rows = (e.frames + cols - 1) / cols;
        const float cell_w = static_cast<float>(sprite->width()) / cols;
        const float cell_h = static_cast<float>(sprite->height()) / rows;
        const int elapsed =
            static_cast<int>(preview_now * e.fps);
        const int frame =
            e.fps > 0.f
                ? (e.anim_loop ? elapsed % e.frames
                               : std::min(elapsed, e.frames - 1))
                : 0;
        img.source = UiRect{(frame % cols) * cell_w,
                            (frame / cols) * cell_h, cell_w, cell_h};
      }
      img.rotation_degrees = e.rotation + e.spin * preview_now;
      img.flip_horizontal = e.flip_x;
      img.flip_vertical = e.flip_y;
      const auto a = static_cast<std::uint8_t>(
          std::clamp(e.opacity, 0.f, 1.f) * (e.visible ? 255.f : 70.f));
      img.tint = {e.r, e.g, e.b, a};
      out.overlay.push_back(std::move(img));
    } else {
      out.overlay.push_back(FilledRectangle{
          rect, {e.r, e.g, e.b,
                 static_cast<std::uint8_t>(
                     std::clamp(e.opacity, 0.f, 1.f) *
                     (e.visible ? 200.f : 60.f))}});
    }
    if (!e.text.empty()) {
      const int font_px = std::max(8, (int)(rect.height * .5f));
      out.overlay.push_back(Text{
          {rect.x + rect.width * .5f,
           rect.y + (rect.height - font_px) * .5f},
          e.text, {255, 255, 255, 255}, font_px, rect.width, std::nullopt,
          TextAlign::Center});
    }
    if (i == shell.selected_entity)
      out.overlay.push_back(StrokedRectangle{rect, accent});
  }
  // Tilemaps layered above every entity draw last (foreground grids).
  while (next_tm < tm_order.size()) draw_tilemap(tm_order[next_tm++]);
  // Paint mode overlays the selected tilemap's cell grid and highlights
  // the hovered cell.
  if (shell.scene_paint && scene_tile(shell) != nullptr) {
    const auto &tm = *scene_tile(shell);
    if (tm.tile_w > 0 && tm.tile_h > 0 && tm.columns > 0) {
      const Color grid{120, 140, 160, 60};
      for (int cx = 0; cx <= tm.columns; ++cx)
        out.overlay.push_back(FilledRectangle{
            {pv.x + (tm.x + cx * tm.tile_w) * sx, pv.y + tm.y * sy,
             1.f, pv.height - tm.y * sy},
            grid});
      const int rows =
          std::max((int)(tm.cells.size() / tm.columns),
                   (int)((720.f - tm.y) / tm.tile_h));
      for (int cy = 0; cy <= rows; ++cy)
        out.overlay.push_back(FilledRectangle{
            {pv.x + tm.x * sx, pv.y + (tm.y + cy * tm.tile_h) * sy,
             pv.width - tm.x * sx, 1.f},
            grid});
      const int hx = static_cast<int>(std::floor(
          ((shell.pointer_x - pv.x) / sx - tm.x) / tm.tile_w));
      const int hy = static_cast<int>(std::floor(
          ((shell.pointer_y - pv.y) / sy - tm.y) / tm.tile_h));
      if (hx >= 0 && hx < tm.columns && hy >= 0) {
        // Outline the whole brush footprint, not just the hovered cell.
        const int brush = std::clamp(shell.scene_paint_brush, 1, 8);
        const int lo = -(brush / 2);
        out.overlay.push_back(StrokedRectangle{
            {pv.x + (tm.x + (hx + lo) * tm.tile_w) * sx,
             pv.y + (tm.y + (hy + lo) * tm.tile_h) * sy,
             brush * tm.tile_w * sx, brush * tm.tile_h * sy},
            accent});
      }
    }
    // Tile picker: the decoded sheet as a strip along the preview's top —
    // clicking a cell selects it as the brush instead of painting.
    shell.scene_sheet_rect = {};
    if (const auto tiles = load_tileset(tm.tileset)) {
      const int set_cols = std::max(1, tiles->width() / tm.tile_w);
      const float fit =
          std::min(2.f, std::min(pv.width / tiles->width(),
                                 pv.height * .3f / tiles->height()));
      const float sw = tiles->width() * fit, sh = tiles->height() * fit;
      shell.scene_sheet_rect = {pv.x, pv.y, sw, sh};
      shell.scene_sheet_scale = fit;
      shell.scene_sheet_cols = set_cols;
      out.overlay.push_back(
          FilledRectangle{shell.scene_sheet_rect, {10, 14, 22, 220}});
      out.overlay.push_back(
          Image{tiles, shell.scene_sheet_rect});
      if (shell.scene_paint_cell >= 0) {
        const int bc = shell.scene_paint_cell;
        out.overlay.push_back(StrokedRectangle{
            {pv.x + (bc % set_cols) * tm.tile_w * fit,
             pv.y + (bc / set_cols) * tm.tile_h * fit,
             tm.tile_w * fit, tm.tile_h * fit},
            accent});
      }
    }
  } else {
    shell.scene_sheet_rect = {};
  }
  out.overlay.push_back(Text{
      {pv.x + 6 * s, pv.y + pv.height - 16 * s},
      shell.scene_paint
          ? (shell.scene_paint_fill
                 ? "fill mode - click floods a same-value region"
                 : "paint mode - click/drag writes cells")
          : "click selects - drag moves entities",
      muted, static_cast<int>(11 * s), 0, pv});

  // Property fields for the selected entity. Column count adapts to the
  // rows that fit below the preview so tilemap fields stay on-window.
  const auto *entity = selected_scene_entity(shell);
  const float fy0 = pv.y + pv.height + 14 * s;
  const float row_pitch = (font + 12) * s + 4 * s;
  const int rows_per_col = std::max(
      1, static_cast<int>((body.y + body.height - fy0) / row_pitch));
  const int field_count = 40;
  const int cols =
      std::max(2, (field_count + rows_per_col - 1) / rows_per_col);
  const float col_w = pv.width / cols - 8 * s;
  float fy = fy0, fx = px;
  auto field = [&](UiRect &hit, const char *label, const std::string &value,
                   bool editing, const char *hint) {
    // Wrap to the next column when the list reaches the window bottom.
    if (fy + (font + 20) * s > body.y + body.height) {
      fy = fy0;
      fx += col_w + 16 * s;
    }
    out.overlay.push_back(Text{{fx, fy}, label, muted, font});
    hit = {fx + 90 * s, fy - 4 * s, col_w - 90 * s, (font + 8) * s};
    field_box(out, hit, editing ? shell.scene_buffer : value, editing, hint,
              font, s);
    fy += hit.height + 4 * s;
  };
  const auto fmt_pair = [](float a, float b) {
    return std::to_string(static_cast<int>(a)) + "," +
           std::to_string(static_cast<int>(b));
  };
  field(shell.hit_scene_name, "name", entity ? entity->name : "",
        shell.editing_scene && shell.scene_field == 1, "entity name");
  field(shell.hit_scene_pos, "x,y",
        entity ? fmt_pair(entity->x, entity->y) : "",
        shell.editing_scene && shell.scene_field == 2, "e.g. 320,240");
  field(shell.hit_scene_vel, "vx,vy",
        entity ? fmt_pair(entity->vx, entity->vy) : "",
        shell.editing_scene && shell.scene_field == 3, "e.g. 240,150");
  field(shell.hit_scene_sprite, "sprite", entity ? entity->sprite : "",
        shell.editing_scene && shell.scene_field == 4,
        "content-relative image path");
  field(shell.hit_scene_size, "w,h",
        entity ? fmt_pair(entity->w, entity->h) : "",
        shell.editing_scene && shell.scene_field == 5, "e.g. 96,96");
  field(shell.hit_scene_color, "color",
        entity ? std::to_string(entity->r) + "," + std::to_string(entity->g) +
                     "," + std::to_string(entity->b)
               : "",
        shell.editing_scene && shell.scene_field == 6, "e.g. 255,200,60");
  field(shell.hit_scene_layer, "layer",
        entity ? std::to_string(entity->layer) : "",
        shell.editing_scene && shell.scene_field == 7,
        "draw order - higher draws on top");
  field(shell.hit_scene_parallax, "parallax",
        entity ? std::to_string(entity->parallax) : "",
        shell.editing_scene && shell.scene_field == 8,
        "camera scroll factor - 0 pins to screen");
  field(shell.hit_scene_text, "text", entity ? entity->text : "",
        shell.editing_scene && shell.scene_field == 9,
        "centered label drawn in the rect");
  field(shell.hit_scene_grav, "grav",
        entity ? std::to_string(entity->gravity_scale) : "",
        shell.editing_scene && shell.scene_field == 10,
        "gravity multiplier - 0 ignores scene gravity");
  field(shell.hit_scene_solid, "solid",
        entity ? (entity->solid ? "true" : "false") : "",
        shell.editing_scene && shell.scene_field == 12,
        "platform/ground - things land on it");
  field(shell.hit_scene_gravity, "gravity",
        std::to_string(shell.scene_doc.gravity),
        shell.editing_scene && shell.scene_field == 11,
        "scene px/s^2 - 0 disables");
  field(shell.hit_scene_bg, "background",
        std::to_string((int)shell.scene_doc.bg_r) + "," +
            std::to_string((int)shell.scene_doc.bg_g) + "," +
            std::to_string((int)shell.scene_doc.bg_b),
        shell.editing_scene && shell.scene_field == 13,
        "scene clear color r,g,b");
  field(shell.hit_scene_frames, "frames",
        entity ? std::to_string(entity->frames) : "",
        shell.editing_scene && shell.scene_field == 14,
        "sprite strip cells");
  field(shell.hit_scene_fps, "fps",
        entity ? std::to_string(entity->fps) : "",
        shell.editing_scene && shell.scene_field == 15,
        "anim frames/sec - 0 still");
  field(shell.hit_scene_rot, "rotation",
        entity ? std::to_string(entity->rotation) : "",
        shell.editing_scene && shell.scene_field == 16,
        "degrees clockwise - sprites only");
  field(shell.hit_scene_ttl, "ttl",
        entity ? std::to_string(entity->ttl) : "",
        shell.editing_scene && shell.scene_field == 17,
        "seconds until despawn - 0 immortal");
  field(shell.hit_scene_flipx, "flipX",
        entity ? (entity->flip_x ? "true" : "false") : "",
        shell.editing_scene && shell.scene_field == 18,
        "mirror sprite horizontally");
  field(shell.hit_scene_flipy, "flipY",
        entity ? (entity->flip_y ? "true" : "false") : "",
        shell.editing_scene && shell.scene_field == 19,
        "mirror sprite vertically");
  field(shell.hit_scene_visible, "visible",
        entity ? (entity->visible ? "true" : "false") : "",
        shell.editing_scene && shell.scene_field == 20,
        "false simulates but hides");
  field(shell.hit_scene_oneway, "oneway",
        entity ? (entity->oneway ? "true" : "false") : "",
        shell.editing_scene && shell.scene_field == 21,
        "land on top, pass through");
  field(shell.hit_scene_data, "data",
        entity ? entity->data : "",
        shell.editing_scene && shell.scene_field == 22,
        "freeform game payload");
  field(shell.hit_scene_opacity, "opacity",
        entity ? std::to_string(entity->opacity) : "",
        shell.editing_scene && shell.scene_field == 23,
        "0-1 draw alpha");
  field(shell.hit_scene_spin, "spin",
        entity ? std::to_string(entity->spin) : "",
        shell.editing_scene && shell.scene_field == 24,
        "deg/s rotation velocity");
  field(shell.hit_scene_bounce, "bounce",
        entity ? (entity->bounce ? "true" : "false") : "",
        shell.editing_scene && shell.scene_field == 25,
        "false stops dead at level edges");
  field(shell.hit_scene_parent, "parent",
        entity ? entity->parent : "",
        shell.editing_scene && shell.scene_field == 26,
        "follow this entity at authored offset");
  field(shell.hit_scene_fcols, "fcols",
        entity ? std::to_string(entity->fcols) : "",
        shell.editing_scene && shell.scene_field == 27,
        "sheet columns/row - 0 = strip");
  field(shell.hit_scene_animloop, "animloop",
        entity ? (entity->anim_loop ? "true" : "false") : "",
        shell.editing_scene && shell.scene_field == 28,
        "false holds the last frame");
  field(shell.hit_scene_vfx, "vfx",
        entity ? entity->vfx : "",
        shell.editing_scene && shell.scene_field == 29,
        "named emitter attached on spawn");
  // Tilemap fields (doc-level, selected layer) — editing creates the
  // tilemap on demand.
  const auto *tm = scene_tile(shell);
  auto cell_list = [&] {
    std::string out_s;
    if (tm)
      for (const int c : tm->cells) {
        if (!out_s.empty()) out_s += ',';
        out_s += std::to_string(c);
      }
    return out_s;
  };
  field(shell.hit_scene_tileset, "tileset", tm ? tm->tileset : "",
        shell.editing_scene && shell.scene_field == 30,
        "content-relative tile sheet");
  field(shell.hit_scene_tilesize, "tilesize",
        tm ? fmt_pair(static_cast<float>(tm->tile_w),
                      static_cast<float>(tm->tile_h))
           : "",
        shell.editing_scene && shell.scene_field == 31, "cell w,h in px");
  field(shell.hit_scene_tilecols, "tilecols",
        tm ? std::to_string(tm->columns) : "",
        shell.editing_scene && shell.scene_field == 32, "grid columns");
  field(shell.hit_scene_tilecollide, "tilecol",
        tm ? (tm->collide ? "true" : "false") : "",
        shell.editing_scene && shell.scene_field == 33,
        "cells block and catch entities");
  field(shell.hit_scene_tilelayer, "tilelayer",
        tm ? std::to_string(tm->layer) : "",
        shell.editing_scene && shell.scene_field == 34,
        "draw layer vs entities");
  field(shell.hit_scene_tilepar, "tilepara",
        tm ? std::to_string(tm->parallax) : "",
        shell.editing_scene && shell.scene_field == 35,
        "camera scroll factor");
  field(shell.hit_scene_tileorigin, "tileorigin",
        tm ? fmt_pair(tm->x, tm->y) : "",
        shell.editing_scene && shell.scene_field == 40,
        "grid origin x,y in world px");
  field(shell.hit_scene_tilename, "tilename", tm ? tm->name : "",
        shell.editing_scene && shell.scene_field == 41,
        "layer name for tilemap_index lookups");
  field(shell.hit_scene_tilecells, "tilecells", cell_list(),
        shell.editing_scene && shell.scene_field == 36,
        "csv cells, -1 empty");
  field(shell.hit_scene_paintcell, "paintcell",
        std::to_string(shell.scene_paint_cell),
        shell.editing_scene && shell.scene_field == 37,
        "brush tile id, -1 erases");
  field(shell.hit_scene_brushsz, "brushsz",
        std::to_string(shell.scene_paint_brush),
        shell.editing_scene && shell.scene_field == 42,
        "NxN stamp per click, 1..8");
  field(shell.hit_scene_music, "music", shell.scene_doc.music,
        shell.editing_scene && shell.scene_field == 38,
        "content-relative track played on scene load");
  field(shell.hit_scene_worldsize, "worldsize",
        shell.scene_doc.world_w > 0.f || shell.scene_doc.world_h > 0.f
            ? std::to_string((int)shell.scene_doc.world_w) + "," +
                  std::to_string((int)shell.scene_doc.world_h)
            : "",
        shell.editing_scene && shell.scene_field == 39,
        "level bounds w,h - 0 uses viewport");
  if (entity == nullptr)
    line(out, px, fy, "", "select or add an entity", font);
}

void render_dashboard(DrawList &out, const Shell &shell, UiRect body, float s,
                      const engine::JobStats &stats, int demo_jobs,
                      std::size_t locale_keys, const std::string &locale_status,
                      const Window &window, const InputSnapshot &snapshot,
                      double fps, const std::string &last_input) {
  float x = body.x + 22 * s;
  float y = body.y + 18 * s;
  const int font = static_cast<int>(14 * s);

  heading(out, x, y, "PLATFORM");
  line(out, x, y, "engine", STELLAR_ENGINE_VERSION, font);
  line(out, x, y, "source", STELLAR_SOURCE_COMMIT, font);
  line(out, x, y, "gpu", window.graphics_adapter(), font);
  line(out, x, y, "presentation", window.presentation_mode(), font);
  line(out, x, y, "display",
       std::to_string(snapshot.drawable_width) + " x " +
           std::to_string(snapshot.drawable_height),
       font);
  y += 10 * s;

  heading(out, x, y, "RUNTIME");
  line(out, x, y, "fps", std::to_string(static_cast<int>(fps)), font);
  line(out, x, y, "job workers", std::to_string(stats.workers), font);
  line(out, x, y, "jobs completed",
       std::to_string(stats.completed) + " / " +
           std::to_string(stats.submitted) + " submitted",
       font);
  line(out, x, y, "jobs failed/cancelled",
       std::to_string(stats.failed) + " / " + std::to_string(stats.cancelled),
       font);
  line(out, x, y, "demo jobs run", std::to_string(demo_jobs), font);
  line(out, x, y, "text cache",
       std::to_string(window.text_cache_entries()) + " entries / " +
           std::to_string(window.text_cache_bytes() / 1024) + " KiB",
       font);
  line(out, x, y, "image cache",
       std::to_string(window.image_cache_entries()) + " entries / " +
           std::to_string(window.image_cache_resident_bytes() / 1024 / 1024) +
           " MiB",
       font);
  y += 10 * s;

  heading(out, x, y, "SERVICES");
  line(out, x, y, "localization", locale_status, font);
  line(out, x, y, "catalog keys", std::to_string(locale_keys), font);
  line(out, x, y, "pointer",
       std::to_string(static_cast<int>(snapshot.pointer.x)) + ", " +
           std::to_string(static_cast<int>(snapshot.pointer.y)),
       font);
  line(out, x, y, "last input", last_input, font);
  line(out, x, y, "assets indexed",
       shell.asset_root.empty()
           ? "assets directory not found"
           : std::to_string(shell.asset_files.size()) + " files",
       font);
}

// List geometry shared by the asset render pass and the input pass so hit
// testing always matches what was drawn.
UiRect tool_list_rect(UiRect body, float s, float width_fraction) {
  const float font = 13 * s;
  const float header = 18 * s + 26.f + 2 * (font + 8.f) + 6 * s;
  return {body.x + 22 * s, body.y + header,
          body.width * width_fraction - 22 * s,
          body.height - header - 16 * s};
}

void render_assets(DrawList &out, Shell &shell, UiRect body, float s) {
  float x = body.x + 22 * s;
  float y = body.y + 18 * s;
  const int font = static_cast<int>(13 * s);

  heading(out, x, y, "ASSET LIBRARY");
  line(out, x, y, "root", shell.asset_root.string(), font);
  const std::size_t row_count =
      shell.show_cooked ? shell.cooked_records.size()
                        : shell.asset_files.size();
  line(out, x, y, shell.show_cooked ? "records" : "files",
       std::to_string(row_count), font);
  y += 6 * s;

  // Source/cooked toggle while a project is open.
  if (shell.project) {
    shell.hit_cooked_toggle = {body.x + body.width - 22 * s - 150 * s,
                               body.y + 14 * s, 150 * s, (font + 10) * s};
    shell_button(out, shell.hit_cooked_toggle,
                 shell.show_cooked ? "VIEW: COOKED" : "VIEW: SOURCE",
                 shell.show_cooked, font, s);
  } else {
    shell.hit_cooked_toggle = {};
  }
  if (shell.show_cooked && shell.cooked_dirty.exchange(false))
    load_cooked(shell);
  const UiRect list_rect = tool_list_rect(body, s, 0.48f);
  out.overlay.push_back(FilledRectangle{list_rect, {6, 16, 26, 255}});
  out.overlay.push_back(StrokedRectangle{list_rect, panel_edge});

  shell.asset_list.configure(row_count, shell.asset_list.row_height,
                             list_rect.height);
  const auto range = shell.asset_list.visible_range();
  const float row_h = shell.asset_list.row_height;
  float ry = list_rect.y - shell.asset_list.scroll_offset +
             range.first * row_h;
  for (std::size_t i = range.first; i < range.last; ++i, ry += row_h) {
    const UiRect row{list_rect.x, ry, list_rect.width, row_h};
    if (i == shell.selected_asset)
      out.overlay.push_back(FilledRectangle{row, row_selected});
    else if (row.contains(
                 Point{shell.pointer_x, shell.pointer_y}))
      out.overlay.push_back(FilledRectangle{row, row_hover});
    if (shell.show_cooked) {
      const auto &record = shell.cooked_records[i];
      out.overlay.push_back(
          Text{{row.x + 8 * s, row.y + 4 * s},
               record.id + "  " + record.format + "  " +
                   human_bytes(record.source_bytes),
               ink, font, 0, list_rect});
    } else {
      out.overlay.push_back(
          Text{{row.x + 8 * s, row.y + 4 * s},
               shell.asset_files[i].generic_string(), ink, font, 0,
               list_rect});
    }
  }

  // Scrollbar thumb.
  if (shell.asset_list.max_scroll() > 0) {
    const float track = list_rect.height;
    const float thumb = std::max(
        24.f, track * track / (track + shell.asset_list.max_scroll()));
    const float t = shell.asset_list.scroll_offset /
                    shell.asset_list.max_scroll();
    const UiRect bar{list_rect.x + list_rect.width - 5.f,
                     list_rect.y + t * (track - thumb), 4.f, thumb};
    out.overlay.push_back(FilledRectangle{bar, accent});
  }

  // Preview pane.
  const UiRect preview_rect{list_rect.x + list_rect.width + 16 * s, y,
                            body.x + body.width - list_rect.x - list_rect.width -
                                38 * s,
                            list_rect.height};
  out.overlay.push_back(FilledRectangle{preview_rect, {6, 16, 26, 255}});
  out.overlay.push_back(StrokedRectangle{preview_rect, panel_edge});
  if (shell.preview) {
    const float fit = std::min(
        (preview_rect.width - 24 * s) / shell.preview->width(),
        (preview_rect.height - 56 * s) / shell.preview->height());
    const float pw = shell.preview->width() * std::min(fit, 1.f);
    const float ph = shell.preview->height() * std::min(fit, 1.f);
    out.overlay.push_back(
        Image{shell.preview,
              {preview_rect.x + (preview_rect.width - pw) * .5f,
               preview_rect.y + 12 * s, pw, ph}});
    out.overlay.push_back(Text{{preview_rect.x + 10 * s,
                             preview_rect.y + preview_rect.height - 36 * s},
                            shell.preview_label, muted, font, 0, preview_rect});
  } else {
    out.overlay.push_back(
        Text{{preview_rect.x + 10 * s, preview_rect.y + 10 * s},
             shell.selected_asset == static_cast<std::size_t>(-1)
                 ? "Select a file"
                 : shell.preview_label,
             muted, font, 0, preview_rect});
  }
}

std::filesystem::path profiler_capture_path(int slot) {
  return engine::executable_directory() / "profiler_captures" /
         ("capture_" + std::string(slot == 0 ? "a" : "b") + ".json");
}

void render_profiler(DrawList &out, Shell &shell, UiRect body, float s,
                     engine::Profiler &profiler) {
  float x = body.x + 22 * s;
  float y = body.y + 18 * s;
  const int font = static_cast<int>(13 * s);
  heading(out, x, y, "PROFILER");
  const float bw = 92 * s, bh = 24 * s, gap = 8 * s;
  shell.hit_prof_cap_a = {x, y, bw, bh};
  shell.hit_prof_cap_b = {x + (bw + gap), y, bw, bh};
  shell.hit_prof_save_a = {x + (bw + gap) * 2, y, bw, bh};
  shell.hit_prof_load_a = {x + (bw + gap) * 3, y, bw, bh};
  shell.hit_prof_save_b = {x + (bw + gap) * 4, y, bw, bh};
  shell.hit_prof_load_b = {x + (bw + gap) * 5, y, bw, bh};
  shell_button(out, shell.hit_prof_cap_a, "CAPTURE A", false, font, s);
  shell_button(out, shell.hit_prof_cap_b, "CAPTURE B", false, font, s);
  shell_button(out, shell.hit_prof_save_a, "SAVE A", false, font, s);
  shell_button(out, shell.hit_prof_load_a, "LOAD A", false, font, s);
  shell_button(out, shell.hit_prof_save_b, "SAVE B", false, font, s);
  shell_button(out, shell.hit_prof_load_b, "LOAD B", false, font, s);
  y += bh + 8 * s;
  if (!shell.prof_status.empty())
    line(out, x, y, "status", shell.prof_status, font);
  for (const auto &row : profiler.overlay_lines(18)) {
    out.overlay.push_back(Text{{x, y}, row, ink, font});
    y += font + 8.f;
  }
  const auto aggregates = profiler.aggregates();
  heading(out, x, y, "AGGREGATES");
  const auto count = std::min<std::size_t>(aggregates.size(), 10);
  for (std::size_t i = 0; i < count; ++i) {
    const auto &a = aggregates[i];
    line(out, x, y,
         a.category.empty() ? a.name : a.category + "/" + a.name,
         std::to_string(a.calls) + " calls, " +
             ms(static_cast<double>(a.total_nanoseconds) / 1e6) + " total",
         font);
  }
  if (shell.prof_capture_a && shell.prof_capture_b) {
    heading(out, x, y, "A vs B (largest mean deltas)");
    const auto rows = engine::compare_captures(*shell.prof_capture_a,
                                               *shell.prof_capture_b);
    for (std::size_t i = 0; i < std::min<std::size_t>(rows.size(), 12); ++i) {
      const auto &r = rows[i];
      const double delta = (r.mean_ns_b - r.mean_ns_a) / 1e6;
      char buf[160];
      std::snprintf(buf, sizeof(buf), "A %.2f ms -> B %.2f ms  (%+.2f ms)",
                    r.mean_ns_a / 1e6, r.mean_ns_b / 1e6, delta);
      line(out, x, y,
           r.category.empty() ? r.name : r.category + "/" + r.name, buf,
           font);
    }
  }
}

void render_localization(DrawList &out, Shell &shell, UiRect body, float s,
                         const engine::LocalizationTable &locale) {
  float x = body.x + 22 * s;
  float y = body.y + 18 * s;
  const int font = static_cast<int>(13 * s);
  heading(out, x, y, "LOCALIZATION");
  line(out, x, y, "catalog keys", std::to_string(locale.size()), font);
  line(out, x, y, "sample size",
       std::to_string(shell.sample_keys.size()) + " keys", font);
  y += 6 * s;

  const UiRect list_rect = tool_list_rect(body, s, 1.0f);
  out.overlay.push_back(FilledRectangle{list_rect, {6, 16, 26, 255}});
  out.overlay.push_back(StrokedRectangle{list_rect, panel_edge});
  shell.key_list.configure(shell.sample_keys.size(), 22.f,
                           list_rect.height);
  const auto range = shell.key_list.visible_range();
  float ry = list_rect.y - shell.key_list.scroll_offset +
             range.first * shell.key_list.row_height;
  for (std::size_t i = range.first; i < range.last;
       ++i, ry += shell.key_list.row_height) {
    const auto &key = shell.sample_keys[i];
    const auto value = locale.translate(key);
    out.overlay.push_back(Text{{list_rect.x + 8 * s, ry + 4 * s},
                            key + "  =  " + std::string(value), ink, font, 0,
                            list_rect});
  }
}

void shell_button(DrawList &out, const UiRect &rect, const char *label,
                  bool active, int font, float s) {
  out.overlay.push_back(
      FilledRectangle{rect, active ? row_selected : Color{16, 40, 56, 255}});
  out.overlay.push_back(StrokedRectangle{rect, panel_edge});
  out.overlay.push_back(Text{{rect.x + rect.width * .5f, rect.y + 7 * s},
                             label, ink, font, rect.width, rect,
                             TextAlign::Center});
}

void field_box(DrawList &out, const UiRect &rect, const std::string &value,
               bool editing, const char *hint, int font, float s) {
  out.overlay.push_back(FilledRectangle{rect, {4, 12, 20, 255}});
  out.overlay.push_back(
      StrokedRectangle{rect, editing ? accent : panel_edge});
  out.overlay.push_back(Text{
      {rect.x + 8 * s, rect.y + 7 * s},
      value.empty() ? hint : value,
      value.empty() ? muted : ink, font, rect.width - 12 * s, rect});
}

// ---- Simulation tool: live executor + framework demo ------------------

void init_sim(Shell::SimDemo &sim) {
  namespace eng = engine;
  // Three settlements — real cohort populations, not counters.
  for (int i = 0; i < 3; ++i) {
    eng::Population pop;
    pop.define_profile({.id = "human"});
    pop.add({.profile = "human", .occupation = "miner"},
            800.0 + i * 300.0);
    pop.add({.profile = "human", .occupation = "farmer"}, 200.0);
    sim.settlements.push_back(std::move(pop));
  }
  sim.power.add_node(1, 12.0, 0.0, 40.0);
  sim.power.add_node(2, 0.0, 5.0, 10.0);
  sim.power.add_node(3, 0.0, 3.0, 10.0);
  sim.power.add_edge(1, 1, 2, 8.0);
  sim.power.add_edge(2, 1, 3, 6.0);
  sim.freight.add_node(1);
  sim.freight.add_node(2);
  sim.freight.add_route(10, {1, 2}, {2.0}, 20.0);

  for (std::size_t i = 0; i < sim.settlements.size(); ++i) {
    sim.executor.add(
        static_cast<eng::SimulationExecutor::Key>(10 + i),
        {.run = [&sim, i](const eng::SimulationTickContext &ctx) {
           eng::SettlementConditions conditions;
           sim.settlements[i].advance(
               static_cast<double>(ctx.elapsed_ticks), conditions);
         },
         .tier = i == 2 ? eng::SimulationTier::Background
                        : eng::SimulationTier::Normal,
         .domain = "colony"});
  }
  sim.executor.add(20, {.run = [&sim](const eng::SimulationTickContext &ctx) {
                          sim.power.advance(
                              static_cast<double>(ctx.elapsed_ticks));
                        },
                        .tier = eng::SimulationTier::Active,
                        .domain = "infrastructure"});
  sim.executor.add(21, {.run = [&sim](const eng::SimulationTickContext &ctx) {
                          static std::uint64_t next_shipment = 100;
                          sim.freight.dispatch(next_shipment++, 10, "ore",
                                               6.0);
                          sim.delivered += static_cast<double>(
                              sim.freight
                                  .advance(static_cast<double>(
                                      ctx.elapsed_ticks))
                                  .deliveries.size());
                        },
                        .tier = eng::SimulationTier::Active,
                        .domain = "logistics"});
  sim.executor.add(30, {.run = [&sim](const eng::SimulationTickContext &) {
                          sim.relay_pings += 1.0;
                        },
                        .tier = eng::SimulationTier::Dormant,
                        .domain = "exploration"});
  sim.initialized = true;
}

const char *tier_name(engine::SimulationTier tier) {
  switch (tier) {
  case engine::SimulationTier::Active: return "Active";
  case engine::SimulationTier::Nearby: return "Nearby";
  case engine::SimulationTier::Normal: return "Normal";
  case engine::SimulationTier::Background: return "Background";
  case engine::SimulationTier::Dormant: return "Dormant";
  case engine::SimulationTier::Count: break;
  }
  return "?";
}

struct SimTaskRow {
  engine::SimulationExecutor::Key key;
  const char *name;
  const char *domain;
};
constexpr std::array<SimTaskRow, 6> kSimTasks{{
    {10, "settlement.alpha", "colony"},
    {11, "settlement.beta", "colony"},
    {12, "settlement.gamma", "colony"},
    {20, "power.grid", "infrastructure"},
    {21, "freight.ore_line", "logistics"},
    {30, "relay.deep_space", "exploration"},
}};

void render_simulation(DrawList &out, Shell &shell, UiRect body, float s) {
  auto &sim = shell.sim;
  if (!sim.initialized) init_sim(sim);

  float x = body.x + 22 * s;
  float y = body.y + 18 * s;
  const int font = static_cast<int>(13 * s);
  heading(out, x, y, "SIMULATION");

  shell.hit_sim_step = {x, y, 74 * s, 24 * s};
  shell_button(out, shell.hit_sim_step, "STEP", !sim.running, font, s);
  shell.hit_sim_run = {x + 82 * s, y, 82 * s, 24 * s};
  shell_button(out, shell.hit_sim_run, sim.running ? "PAUSE" : "RUN",
               sim.running, font, s);
  shell.hit_sim_wake = {x + 172 * s, y, 106 * s, 24 * s};
  shell_button(out, shell.hit_sim_wake, "WAKE RELAY", false, font, s);
  const auto sel_key = kSimTasks[sim.selected].key;
  const auto sel_tier = sim.executor.tier(sel_key);
  shell.hit_sim_tier = {x + 286 * s, y, 170 * s, 24 * s};
  shell_button(out, shell.hit_sim_tier,
               ("TIER: " + std::string(tier_name(sel_tier))).c_str(),
               false, font, s);
  y += 32 * s;

  line(out, x, y, "tick", std::to_string(sim.executor.tick()), font);
  line(out, x, y, "last step",
       "eligible " + std::to_string(sim.last.eligible) + "  ran " +
           std::to_string(sim.last.ran) + "  deferred " +
           std::to_string(sim.last.deferred) + "  wakes " +
           std::to_string(sim.last.dirty_wakeups + sim.last.event_wakeups),
       font);
  {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f us",
                  static_cast<double>(sim.last.wall_ns) / 1000.0);
    line(out, x, y, "wall time", buf, font);
  }
  double people = 0.0;
  for (const auto &pop : sim.settlements) people += pop.total();
  line(out, x, y, "population", std::to_string(static_cast<long long>(people)),
       font);
  double stored = 0.0;
  for (const auto &node : sim.power.capture_state().nodes)
    stored += node.storage;
  line(out, x, y, "power stored", std::to_string(static_cast<int>(stored)),
       font);
  line(out, x, y, "ore delivered",
       std::to_string(static_cast<int>(sim.delivered)), font);
  line(out, x, y, "relay pings",
       std::to_string(static_cast<int>(sim.relay_pings)), font);
  y += 4 * s;

  // Task table: left column list, domain stats on the right.
  const float row_h = 22 * s;
  const UiRect list_rect{x, y, body.width * 0.52f - 22 * s,
                         kSimTasks.size() * row_h + 12 * s};
  shell.hit_sim_list = list_rect;
  out.overlay.push_back(FilledRectangle{list_rect, {6, 16, 26, 255}});
  out.overlay.push_back(StrokedRectangle{list_rect, panel_edge});
  for (std::size_t i = 0; i < kSimTasks.size(); ++i) {
    const auto &row = kSimTasks[i];
    const float ry = list_rect.y + 6 * s + i * row_h;
    const bool selected = i == sim.selected;
    if (selected)
      out.overlay.push_back(FilledRectangle{
          {list_rect.x + 2 * s, ry, list_rect.width - 4 * s, row_h},
          row_selected});
    const auto tier = sim.executor.tier(row.key);
    const auto elapsed =
        sim.executor.scheduler().elapsed_since_run(row.key);
    out.overlay.push_back(Text{
        {list_rect.x + 8 * s, ry + 4 * s},
        std::string(row.name), selected ? ink : muted, font});
    out.overlay.push_back(Text{
        {list_rect.x + list_rect.width - 150 * s, ry + 4 * s},
        std::string(tier_name(tier)) + "  +" + std::to_string(elapsed),
        selected ? ink : muted, font});
  }

  float rx = list_rect.x + list_rect.width + 16 * s;
  float ry2 = y + 4 * s;
  const auto tiers = sim.executor.tier_counts();
  line(out, rx, ry2, "tiers",
       "act " + std::to_string(tiers[0]) + "  nrm " +
           std::to_string(tiers[2]) + "  bkg " +
           std::to_string(tiers[3]) + "  dor " +
           std::to_string(tiers[4]),
       font);
  for (const auto &domain : sim.executor.domains()) {
    const auto *stats = sim.executor.domain_stats(domain);
    if (!stats) continue;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%llu runs  %.1f us total",
                  static_cast<unsigned long long>(stats->runs),
                  static_cast<double>(stats->total_ns) / 1000.0);
    line(out, rx, ry2, domain, buf, font);
  }
}

// ---- Colony tool: live settlement designer over engine::Colony -------

void init_colony(Shell::ColonyDemo &col) {
  namespace eng = engine;
  col.colony.define_district(
      {.id = "district.residential",
       .category = "residential",
       .structure_slots = 4,
       .build_cost = {{"res.alloys", 20}},
       .build_days = 15,
       .utility_demand_per_day = {{"power", 1}},
       .upkeep_per_day = {{"res.alloys", 0.02}}});
  col.colony.define_district(
      {.id = "district.industrial",
       .category = "industrial",
       .structure_slots = 6,
       .build_cost = {{"res.alloys", 40}},
       .build_days = 25,
       .utility_demand_per_day = {{"power", 2}},
       .upkeep_per_day = {{"res.alloys", 0.05}}});
  col.colony.define_structure(
      {.id = "structure.hab_block",
       .category = "civic",
       .district = "district.residential",
       .build_cost = {{"res.alloys", 10}},
       .build_days = 10,
       .utility_demand_per_day = {{"power", 1}},
       .jobs = 10,
       .housing = 500});
  col.colony.define_structure(
      {.id = "structure.hydroponics",
       .category = "food",
       .district = "district.residential",
       .build_cost = {{"res.alloys", 12}},
       .build_days = 10,
       .utility_demand_per_day = {{"power", 2}},
       .inputs_per_day = {{"res.water", 1}},
       .outputs_per_day = {{"res.food", 3}},
       .jobs = 25});
  col.colony.define_structure(
      {.id = "structure.mine",
       .category = "industry",
       .district = "district.industrial",
       .build_cost = {{"res.alloys", 15}},
       .build_days = 12,
       .utility_demand_per_day = {{"power", 3}},
       .upkeep_per_day = {{"res.alloys", 0.1}},
       .outputs_per_day = {{"res.ore", 2}},
       .jobs = 60});
  col.colony.define_structure(
      {.id = "structure.smelter",
       .category = "industry",
       .district = "district.industrial",
       .build_cost = {{"res.alloys", 25}},
       .build_days = 20,
       .utility_demand_per_day = {{"power", 5}},
       .inputs_per_day = {{"res.ore", 2}},
       .outputs_per_day = {{"res.alloys", 1}},
       .jobs = 80});
  col.colony.define_structure(
      {.id = "structure.solar_array",
       .category = "power",
       .build_cost = {{"res.alloys", 8}},
       .build_days = 5,
       .utility_supply_per_day = {{"power", 6}},
       .jobs = 2});
  col.colony.define_structure(
      {.id = "structure.fusion_plant",
       .category = "power",
       .build_cost = {{"res.alloys", 50}},
       .build_days = 30,
       .utility_supply_per_day = {{"power", 20}},
       .upkeep_per_day = {{"res.fuel", 0.2}},
       .jobs = 40});
  col.colony.set_standalone_slots(6);
  col.stockpile.add("res.alloys", 200);
  col.stockpile.add("res.food", 50);
  col.stockpile.add("res.fuel", 100);
  col.stockpile.add("res.water", 100);
  // A starter settlement so the tool opens live.
  col.colony.build_district(col.next_id++, "district.residential");
  col.colony.build_district(col.next_id++, "district.industrial");
  col.colony.build_structure(col.next_id++, "structure.solar_array");
  col.initialized = true;
}

struct ColonyBuildRow {
  const char *spec;
  const char *label;
  bool district;
};
constexpr std::array<ColonyBuildRow, 8> kColonyBuild{{
    {"district.residential", "Residential district", true},
    {"district.industrial", "Industrial district", true},
    {"structure.hab_block", "Hab block", false},
    {"structure.hydroponics", "Hydroponics", false},
    {"structure.mine", "Mine", false},
    {"structure.smelter", "Smelter", false},
    {"structure.solar_array", "Solar array", false},
    {"structure.fusion_plant", "Fusion plant", false},
}};

bool pay_build_cost(engine::Inventory &stockpile,
                    const std::vector<engine::ResourceAmount> &cost) {
  std::vector<engine::ResourceAmount> paid;
  for (const auto &[resource, amount] : cost) {
    const double got = stockpile.remove(resource, amount);
    if (got < amount - 1e-9) {
      for (const auto &[r, a] : paid) stockpile.add(r, a);
      if (got > 0) stockpile.add(resource, got);
      return false;
    }
    paid.push_back({resource, got});
  }
  return true;
}

std::string cost_text(const std::vector<engine::ResourceAmount> &cost) {
  std::string out;
  for (const auto &[resource, amount] : cost) {
    if (!out.empty()) out += " ";
    const auto slash = resource.find_last_of('.');
    out += resource.substr(slash == std::string::npos ? 0 : slash + 1);
    out += " " + std::to_string(static_cast<int>(amount));
  }
  return out.empty() ? std::string("free") : out;
}

void render_colony(DrawList &out, Shell &shell, UiRect body, float s) {
  auto &col = shell.col;
  if (!col.initialized) init_colony(col);

  float x = body.x + 22 * s;
  float y = body.y + 18 * s;
  const int font = static_cast<int>(13 * s);
  heading(out, x, y, "COLONY DESIGNER");

  shell.hit_col_step = {x, y, 92 * s, 24 * s};
  shell_button(out, shell.hit_col_step, "ADV 1D", false, font, s);
  shell.hit_col_run30 = {x + 100 * s, y, 92 * s, 24 * s};
  shell_button(out, shell.hit_col_run30, "ADV 30D", false, font, s);
  shell.hit_col_workers_dn = {x + 200 * s, y, 30 * s, 24 * s};
  shell_button(out, shell.hit_col_workers_dn, "-", false, font, s);
  shell.hit_col_workers_up = {x + 234 * s, y, 30 * s, 24 * s};
  shell_button(out, shell.hit_col_workers_up, "+", false, font, s);
  shell.hit_col_maint = {x + 272 * s, y, 128 * s, 24 * s};
  shell_button(out, shell.hit_col_maint,
               col.maintenance > 0.5 ? "UPKEEP: FULL" : "UPKEEP: LOW",
               col.maintenance > 0.5, font, s);
  shell.hit_col_resupply = {x + 408 * s, y, 104 * s, 24 * s};
  shell_button(out, shell.hit_col_resupply, "RESUPPLY", false, font, s);
  y += 32 * s;

  char buf[96];
  std::snprintf(buf, sizeof(buf), "day %.0f", col.day);
  line(out, x, y, "date", buf, font);
  line(out, x, y, "workers", std::to_string(static_cast<int>(col.workers)),
       font);
  line(out, x, y, "jobs",
       std::to_string(static_cast<int>(col.last.jobs_filled)) + " / " +
           std::to_string(static_cast<int>(col.colony.jobs_total())),
       font);
  line(out, x, y, "housing",
       std::to_string(static_cast<int>(col.colony.housing_capacity())),
       font);
  if (!col.notice.empty()) {
    out.overlay.push_back(
        Text{{x, y}, col.notice, {255, 180, 90, 255}, font});
    y += 18 * s;
  }
  y += 4 * s;

  // Left column: build catalog.
  const float row_h = 22 * s;
  const UiRect build_rect{x, y, 240 * s,
                          kColonyBuild.size() * row_h + 26 * s};
  out.overlay.push_back(FilledRectangle{build_rect, {6, 16, 26, 255}});
  out.overlay.push_back(StrokedRectangle{build_rect, panel_edge});
  out.overlay.push_back(Text{{build_rect.x + 8 * s, build_rect.y + 5 * s},
                             "BUILD", muted, font});
  shell.hit_col_build.resize(kColonyBuild.size());
  for (std::size_t i = 0; i < kColonyBuild.size(); ++i) {
    const float by = build_rect.y + 24 * s + i * row_h;
    shell.hit_col_build[i] = {build_rect.x + 4 * s, by,
                              build_rect.width - 8 * s, row_h - 2 * s};
    const auto &row = kColonyBuild[i];
    const auto &cost =
        row.district ? col.colony.district_spec(row.spec)->build_cost
                     : col.colony.structure_spec(row.spec)->build_cost;
    std::string label =
        std::string(row.label) + "  (" + cost_text(cost) + ")";
    out.overlay.push_back(
        Text{{build_rect.x + 10 * s, by + 4 * s}, label, ink, font});
  }

  // Middle: settlement rows (districts, their structures, standalones).
  const float mx = build_rect.x + build_rect.width + 16 * s;
  const UiRect rows_rect{mx, y, body.x + body.width - mx - 20 * s,
                         300 * s};
  out.overlay.push_back(FilledRectangle{rows_rect, {6, 16, 26, 255}});
  out.overlay.push_back(StrokedRectangle{rows_rect, panel_edge});
  col.rows.clear();
  for (const auto *district : col.colony.districts()) {
    col.rows.push_back({true, district->id});
    for (const auto *structure :
         col.colony.structures_in(district->id))
      col.rows.push_back({false, structure->id});
  }
  for (const auto *structure : col.colony.structures())
    if (structure->district_id == 0)
      col.rows.push_back({false, structure->id});
  if (col.selected >= col.rows.size()) col.selected = 0;

  shell.hit_col_rows.resize(col.rows.size());
  float ry = rows_rect.y + 6 * s;
  const float row_step = 20 * s;
  for (std::size_t i = 0; i < col.rows.size(); ++i) {
    if (ry + row_step > rows_rect.y + rows_rect.height) break;
    const auto [is_district, id] = col.rows[i];
    shell.hit_col_rows[i] = {rows_rect.x + 2 * s, ry,
                             rows_rect.width - 4 * s, row_step};
    const bool selected = i == col.selected;
    if (selected)
      out.overlay.push_back(FilledRectangle{shell.hit_col_rows[i],
                                            row_selected});
    std::string text;
    if (is_district) {
      const auto *district = col.colony.district(id);
      text = "[" + std::to_string(id) + "] " +
             district->spec_id +
             (district->construction_remaining > 0
                  ? "  building " +
                        std::to_string(
                            static_cast<int>(
                                district->construction_remaining)) +
                        "d"
                  : "") +
             (district->enabled ? "" : "  DISABLED");
    } else {
      const auto *structure = col.colony.structure(id);
      char line_buf[160];
      std::snprintf(
          line_buf, sizeof(line_buf),
          "  %s[%llu] %s  cond %.0f%%  op %.0f%%%s%s",
          structure->district_id == 0 ? "" : "    ",
          static_cast<unsigned long long>(id), structure->spec_id.c_str(),
          structure->condition * 100.0, structure->operating * 100.0,
          structure->construction_remaining > 0 ? "  building" : "",
          structure->enabled ? "" : "  DISABLED");
      text = line_buf;
    }
    out.overlay.push_back(Text{{rows_rect.x + 8 * s, ry + 3 * s}, text,
                               selected ? ink : muted, font});
    ry += row_step;
  }
  if (col.rows.empty())
    out.overlay.push_back(Text{{rows_rect.x + 8 * s, ry + 3 * s},
                               "empty settlement — build a district",
                               muted, font});

  // Selection actions.
  const float ay = rows_rect.y + rows_rect.height + 10 * s;
  if (!col.rows.empty()) {
    const auto [is_district, id] = col.rows[col.selected];
    bool enabled = true;
    if (is_district) {
      const auto *district = col.colony.district(id);
      enabled = district && district->enabled;
    } else {
      const auto *structure = col.colony.structure(id);
      enabled = structure && structure->enabled;
    }
    shell.hit_col_enable = {mx, ay, 110 * s, 24 * s};
    shell_button(out, shell.hit_col_enable,
                 enabled ? "DISABLE" : "ENABLE", false, font, s);
    shell.hit_col_demolish = {mx + 118 * s, ay, 110 * s, 24 * s};
    shell_button(out, shell.hit_col_demolish, "DEMOLISH", false, font, s);
  } else {
    shell.hit_col_enable = {};
    shell.hit_col_demolish = {};
  }

  // Right-of-build column: last step report + stockpile + utilities.
  float sy = y + build_rect.height + 14 * s;
  heading(out, x, sy, "LAST STEP");
  if (col.last.elapsed_days > 0) {
    line(out, x, sy, "elapsed",
         std::to_string(static_cast<int>(col.last.elapsed_days)) + " d",
         font);
    line(out, x, sy, "completed",
         std::to_string(col.last.districts_completed) + " district  " +
             std::to_string(col.last.structures_completed) + " structure",
         font);
    auto amounts_text = [](const std::vector<engine::ResourceAmount> &v) {
      std::string text;
      for (const auto &[resource, amount] : v) {
        if (!text.empty()) text += "  ";
        const auto slash = resource.find_last_of('.');
        text += resource.substr(slash == std::string::npos ? 0 : slash + 1);
        char num[32];
        std::snprintf(num, sizeof(num), " %+.1f", amount);
        text += num;
      }
      return text.empty() ? std::string("none") : text;
    };
    line(out, x, sy, "outputs", amounts_text(col.last.outputs_produced),
         font);
    line(out, x, sy, "upkeep short",
         amounts_text(col.last.upkeep_shortfall), font);
    line(out, x, sy, "input short",
         amounts_text(col.last.input_shortfall), font);
    for (const auto &[utility, sd] : col.last.utilities) {
      char ubuf[80];
      std::snprintf(ubuf, sizeof(ubuf), "supply %.1f  demand %.1f",
                    sd.first, sd.second);
      line(out, x, sy, utility, ubuf, font);
    }
  } else {
    line(out, x, sy, "state", "advance to run operations", font);
  }

  const auto snapshot = col.stockpile.snapshot();
  std::vector<std::pair<std::string, double>> stock(snapshot.begin(),
                                                   snapshot.end());
  std::sort(stock.begin(), stock.end());
  heading(out, x, sy, "STOCKPILE");
  for (const auto &[resource, amount] : stock) {
    char sbuf[64];
    std::snprintf(sbuf, sizeof(sbuf), "%.1f", amount);
    line(out, x, sy, resource, sbuf, font);
  }
}

// ---- Economy tool: catalog validation + live network diagnostics -----

void init_economy(Shell::EconomyDemo &eco) {
  namespace eng = engine;
  using Cat = eng::ResourceCategory;
  using Store = eng::StorageClass;
  auto def = [&eco](std::string id, Cat cat, Store store,
                    double mass, bool stockpiles) {
    eng::ResourceSpec spec;
    spec.id = std::move(id);
    spec.name_key = "RES_" + spec.id;
    spec.category = cat;
    spec.storage = store;
    spec.mass_per_unit = mass;
    eco.catalog.define(std::move(spec));
    eng::ResourceDefinition runtime;
    runtime.id = spec.id;
    runtime.name_key = spec.name_key;
    runtime.stockpiles = stockpiles;
    eco.network.define(std::move(runtime));
  };
  def("res.ore", Cat::Raw, Store::Bulk, 1.0, true);
  def("res.alloys", Cat::Refined, Store::Bulk, 0.8, true);
  def("res.fuel", Cat::Refined, Store::Liquid, 0.6, true);
  def("res.food", Cat::Consumable, Store::Bulk, 0.4, true);
  def("res.water", Cat::Raw, Store::Liquid, 1.0, true);

  auto recipe = [&eco](std::string id,
                       std::vector<eng::ResourceAmount> inputs,
                       std::vector<eng::ResourceAmount> outputs,
                       double days) {
    eco.catalog.add_recipe({.id = id,
                            .name_key = "RECIPE_" + id,
                            .inputs = std::move(inputs),
                            .outputs = std::move(outputs),
                            .duration_days = days});
    eco.network.add_recipe(
        eng::to_runtime_recipe(*eco.catalog.recipe(id)));
  };
  recipe("recipe.smelt", {{"res.ore", 2}}, {{"res.alloys", 1}}, 2.0);
  recipe("recipe.refine", {{"res.ore", 3}}, {{"res.fuel", 1}}, 3.0);
  recipe("recipe.hydroponics", {{"res.water", 1}}, {{"res.food", 2}}, 1.0);

  auto &homeworld = eco.network.add_node(1);
  homeworld.inventory.add("res.ore", 200);
  homeworld.inventory.add("res.water", 80);
  auto &colony = eco.network.add_node(2);
  colony.inventory.add("res.food", 10);
  eco.smelter_producer = eco.network.add_producer(1, "recipe.smelt");
  eco.network.add_producer(1, "recipe.hydroponics");
  eco.network.transfer(1, 2, "res.alloys", 40.0, 5.0);
  eco.initialized = true;
}

void render_economy(DrawList &out, Shell &shell, UiRect body, float s) {
  auto &eco = shell.eco;
  if (!eco.initialized) init_economy(eco);

  float x = body.x + 22 * s;
  float y = body.y + 18 * s;
  const int font = static_cast<int>(13 * s);
  heading(out, x, y, "ECONOMY");

  shell.hit_eco_validate = {x, y, 92 * s, 24 * s};
  shell_button(out, shell.hit_eco_validate, "VALIDATE", false, font, s);
  shell.hit_eco_break = {x + 100 * s, y, 150 * s, 24 * s};
  shell_button(out, shell.hit_eco_break,
               eco.break_catalog ? "DANGLING: ON" : "DANGLING: OFF",
               eco.break_catalog, font, s);
  shell.hit_eco_analyze = {x + 258 * s, y, 92 * s, 24 * s};
  shell_button(out, shell.hit_eco_analyze, "ANALYZE", false, font, s);
  shell.hit_eco_step = {x + 358 * s, y, 92 * s, 24 * s};
  shell_button(out, shell.hit_eco_step, "ADV 1D", false, font, s);
  shell.hit_eco_run10 = {x + 458 * s, y, 92 * s, 24 * s};
  shell_button(out, shell.hit_eco_run10, "ADV 10D", false, font, s);
  shell.hit_eco_producer = {x + 558 * s, y, 130 * s, 24 * s};
  shell_button(out, shell.hit_eco_producer, "SMELTER ON/OFF", false, font,
               s);
  y += 32 * s;

  char buf[96];
  std::snprintf(buf, sizeof(buf), "day %.0f", eco.day);
  line(out, x, y, "date", buf, font);
  line(out, x, y, "catalog",
       std::to_string(eco.catalog.resource_count()) + " resources  " +
           std::to_string(eco.catalog.recipe_count()) + " recipes",
       font);
  const auto &unproducible = eco.catalog.graph().unproducible_resources();
  std::string unp;
  for (const auto &id : unproducible) {
    if (!unp.empty()) unp += " ";
    unp += id;
  }
  line(out, x, y, "unproducible", unp.empty() ? "none" : unp, font);
  y += 4 * s;

  // Validation issues.
  heading(out, x, y, "VALIDATION");
  if (!eco.validated) {
    line(out, x, y, "state", "not run — VALIDATE checks the catalog",
         font);
  } else if (eco.issues.empty()) {
    line(out, x, y, "result", "no issues", font);
  } else {
    for (const auto &issue : eco.issues) {
      const char *sev =
          issue.severity == engine::ValidationSeverity::Error ? "ERR"
                                                              : "warn";
      out.overlay.push_back(Text{
          {x, y},
          sev + std::string("  ") + issue.record + "." + issue.field +
              "  " + issue.reason,
          issue.severity == engine::ValidationSeverity::Error
              ? Color{255, 120, 100, 255}
              : Color{255, 200, 90, 255},
          font});
      y += 16 * s;
    }
  }
  y += 4 * s;

  // Analysis table.
  heading(out, x, y, "ANALYSIS");
  if (!eco.analyzed) {
    line(out, x, y, "state",
         "not run — ANALYZE rolls demand + observed production into "
         "bottleneck diagnostics",
         font);
  } else {
    out.overlay.push_back(
        Text{{x, y}, "resource      demand  supply  unmet  reserve  util  flags",
             muted, font});
    y += 16 * s;
    for (const auto &d : eco.diagnostics) {
      std::string flags;
      if (d.bottleneck) flags += "BOTTLENECK ";
      if (d.import_dependent) flags += "IMPORT";
      char row[160];
      std::snprintf(
          row, sizeof(row), "%-13s %6.1f  %6.1f  %5.1f  %7.1f  %4.0f%%  %s",
          d.resource.c_str(), d.demand_per_day, d.supply_per_day,
          d.unmet_per_day,
          std::isinf(d.reserve_days) ? 9999.0 : d.reserve_days,
          d.utilization * 100.0, flags.c_str());
      out.overlay.push_back(
          Text{{x, y}, row, d.bottleneck ? Color{255, 140, 100, 255}
                                         : ink,
               font});
      y += 16 * s;
    }
  }

  // Right column: live network state.
  const float rx = body.x + body.width * 0.52f;
  float ry = body.y + 60 * s;
  heading(out, rx, ry, "NETWORK");
  for (const std::uint64_t node_id : {1ull, 2ull}) {
    const auto *node = eco.network.node(node_id);
    if (!node) continue;
    std::string inv;
    auto snap = node->inventory.snapshot();
    std::vector<std::pair<std::string, double>> rows(snap.begin(),
                                                   snap.end());
    std::sort(rows.begin(), rows.end());
    for (const auto &[res, qty] : rows) {
      if (!inv.empty()) inv += "  ";
      const auto slash = res.find_last_of('.');
      inv += res.substr(slash == std::string::npos ? 0 : slash + 1);
      char num[24];
      std::snprintf(num, sizeof(num), " %.0f", qty);
      inv += num;
    }
    line(out, rx, ry,
         "node " + std::to_string(node_id), inv.empty() ? "empty" : inv,
         font);
  }
  const auto state = eco.network.capture_state();
  for (const auto &p : state.producers) {
    std::string line_text =
        p.recipe_id + (p.enabled ? "" : "  DISABLED") + "  progress " +
        std::to_string(static_cast<int>(p.progress_days * 10) / 10) + "d";
    line(out, rx, ry, "producer " + std::to_string(p.id), line_text, font);
  }
  for (const auto &t : state.transfers) {
    char tbuf[80];
    std::snprintf(tbuf, sizeof(tbuf), "%.0f/%.0f %s @ %.0f/d",
                  t.shipped, t.amount, t.resource.c_str(),
                  t.rate_per_day);
    line(out, rx, ry, "lane " + std::to_string(t.id), tbuf, font);
  }
  ry += 4 * s;
  heading(out, rx, ry, "SHORTAGES");
  const auto shortages = eco.network.shortages();
  if (shortages.empty()) {
    line(out, rx, ry, "state", "none", font);
  } else {
    for (const auto &shortage : shortages) {
      char sbuf[96];
      std::snprintf(sbuf, sizeof(sbuf),
                    "producer %llu  %s  %.1f/%.1f",
                    static_cast<unsigned long long>(shortage.producer_id),
                    shortage.resource.c_str(), shortage.available,
                    shortage.required);
      out.overlay.push_back(Text{{rx, ry}, sbuf,
                                 {255, 140, 100, 255}, font});
      ry += 16 * s;
    }
  }
}

// ---- Planet tool: habitability evaluation + staged terraforming ------

const std::array<engine::HabitabilityProfile, 3> kPlanetProfiles{{
    {.id = "terran",
     .temperature_min_k = 273, .temperature_max_k = 310,
     .atmosphere_min = 0.5, .atmosphere_max = 2.0,
     .gravity_min_g = 0.5, .gravity_max_g = 1.5,
     .water_min = 0.3, .tolerance = 0.25,
     .forbidden_tags = {"hazard.vacuum", "hazard.high_radiation"}},
    {.id = "desert_adapted",
     .temperature_min_k = 300, .temperature_max_k = 345,
     .atmosphere_min = 0.2, .atmosphere_max = 1.5,
     .gravity_min_g = 0.3, .gravity_max_g = 2.0,
     .water_min = 0.0, .tolerance = 0.2,
     .forbidden_tags = {"hazard.vacuum"}},
    {.id = "cryo_dweller",
     .temperature_min_k = 180, .temperature_max_k = 270,
     .atmosphere_min = 0.0, .atmosphere_max = 1.0,
     .gravity_min_g = 0.1, .gravity_max_g = 1.0,
     .water_min = 0.0, .tolerance = 0.25},
}};

void init_planet(Shell::PlanetDemo &planet) {
  engine::PlanetEnvironment env;
  env.temperature_k = 220.0;
  env.atmosphere_atm = 0.01;
  env.gravity_g = 0.38;
  env.water_fraction = 0.0;
  env.tags = {"hazard.vacuum", "tidal_lock"};
  planet.terra.set_environment(env);

  engine::TerraformProject warm;
  warm.id = "terraform.warm";
  warm.stages = {
      {.id = "mirrors",
       .duration_days = 100,
       .temperature_delta_k = 55,
       .water_delta = 0.05},
      {.id = "melt",
       .duration_days = 100,
       .temperature_delta_k = 15,
       .water_delta = 0.35,
       .add_tags = {"hydrosphere.stable"}},
  };
  engine::TerraformProject atmosphere;
  atmosphere.id = "terraform.atmosphere";
  atmosphere.stages = {
      {.id = "seeding",
       .duration_days = 150,
       .atmosphere_delta = 0.6},
      {.id = "processing",
       .duration_days = 150,
       .atmosphere_delta = 0.4,
       .add_tags = {"atmosphere.breathable"},
       .remove_tags = {"hazard.vacuum"}},
  };
  planet.terra.define_project(std::move(warm));
  planet.terra.define_project(std::move(atmosphere));
  planet.initialized = true;
}

void render_planet(DrawList &out, Shell &shell, UiRect body, float s) {
  auto &planet = shell.planet;
  if (!planet.initialized) init_planet(planet);
  const auto &env = planet.terra.environment();

  float x = body.x + 22 * s;
  float y = body.y + 18 * s;
  const int font = static_cast<int>(13 * s);
  heading(out, x, y, "PLANET");

  shell.hit_plan_step = {x, y, 92 * s, 24 * s};
  shell_button(out, shell.hit_plan_step, "ADV 1D", false, font, s);
  shell.hit_plan_run30 = {x + 100 * s, y, 92 * s, 24 * s};
  shell_button(out, shell.hit_plan_run30, "ADV 30D", false, font, s);
  shell.hit_plan_profile = {x + 200 * s, y, 190 * s, 24 * s};
  shell_button(out, shell.hit_plan_profile,
               ("PROFILE: " +
                std::string(kPlanetProfiles[planet.profile].id))
                   .c_str(),
               false, font, s);
  y += 32 * s;

  char buf[96];
  std::snprintf(buf, sizeof(buf), "day %.0f", planet.day);
  line(out, x, y, "date", buf, font);
  y += 4 * s;

  // Environment parameters with -/+ adjusters (idle env only — while a
  // project runs the deltas are driven by Terraforming).
  struct ParamRow {
    const char *label;
    double value;
    const char *unit;
    double step;
  };
  const std::array<ParamRow, Shell::kPlanParamCount> params{{
      {"temperature", env.temperature_k, "K", 10.0},
      {"atmosphere", env.atmosphere_atm, "atm", 0.1},
      {"gravity", env.gravity_g, "g", 0.1},
      {"water", env.water_fraction, "frac", 0.05},
  }};
  for (std::size_t i = 0; i < params.size(); ++i) {
    const auto &p = params[i];
    shell.hit_plan_param[i * 2] = {x, y, 30 * s, 22 * s};
    shell_button(out, shell.hit_plan_param[i * 2], "-", false, font, s);
    shell.hit_plan_param[i * 2 + 1] = {x + 34 * s, y, 30 * s, 22 * s};
    shell_button(out, shell.hit_plan_param[i * 2 + 1], "+", false, font,
                 s);
    char vbuf[64];
    std::snprintf(vbuf, sizeof(vbuf), "%.2f %s", p.value, p.unit);
    out.overlay.push_back(
        Text{{x + 76 * s, y + 4 * s}, std::string(p.label), muted, font});
    out.overlay.push_back(
        Text{{x + 190 * s, y + 4 * s}, vbuf, ink, font});
    y += 26 * s;
  }
  std::string tagline;
  for (const auto &tag : env.tags) {
    if (!tagline.empty()) tagline += " ";
    tagline += tag;
  }
  line(out, x, y, "tags", tagline.empty() ? "none" : tagline, font);
  y += 6 * s;

  // Habitability for the selected profile.
  const auto &profile = kPlanetProfiles[planet.profile];
  const auto report = engine::evaluate_habitability(env, profile);
  heading(out, x, y, "HABITABILITY");
  std::snprintf(buf, sizeof(buf), "%.2f  %s", report.suitability,
                report.habitable ? "HABITABLE" : "uninhabitable");
  line(out, x, y, profile.id, buf, font);
  for (const auto &reason : report.unmet) {
    out.overlay.push_back(Text{{x, y}, "- " + reason,
                               {255, 140, 100, 255}, font});
    y += 16 * s;
  }

  // Terraforming.
  const float tx = body.x + body.width * 0.5f;
  float ty = body.y + 60 * s;
  heading(out, tx, ty, "TERRAFORMING");
  constexpr std::array<const char *, 2> kProjects{"terraform.warm",
                                                  "terraform.atmosphere"};
  shell.hit_plan_project = {tx, ty, 250 * s, 24 * s};
  shell_button(out, shell.hit_plan_project,
               ("PROJECT: " +
                std::string(kProjects[shell.planet_project]))
                   .c_str(),
               false, font, s);
  shell.hit_plan_start = {tx + 258 * s, ty, 92 * s, 24 * s};
  shell_button(out, shell.hit_plan_start, "START",
               !planet.terra.active(), font, s);
  shell.hit_plan_cancel = {tx + 358 * s, ty, 92 * s, 24 * s};
  shell_button(out, shell.hit_plan_cancel, "CANCEL",
               planet.terra.active(), font, s);
  ty += 32 * s;
  if (planet.terra.active()) {
    std::snprintf(buf, sizeof(buf),
                  "%s  stage %d (%.0f%%)  project %.0f%%",
                  planet.terra.active_project().c_str(),
                  static_cast<int>(planet.terra.stage_index()),
                  planet.terra.stage_progress() * 100.0,
                  planet.terra.project_progress() * 100.0);
    line(out, tx, ty, "active", buf, font);
  } else {
    line(out, tx, ty, "active", "none", font);
  }
  if (!planet.last.stages_completed.empty() || planet.last.project_completed) {
    std::string done;
    for (const auto &stage : planet.last.stages_completed) {
      if (!done.empty()) done += " ";
      done += stage;
    }
    if (planet.last.project_completed) done += "  [COMPLETE]";
    line(out, tx, ty, "last step", done, font);
  }
  ty += 6 * s;
  // Stage preview for the selected project.
  if (const auto *project =
          planet.terra.project(kProjects[shell.planet_project])) {
    for (const auto &stage : project->stages) {
      std::string deltas;
      auto push = [&deltas](const char *name, double v) {
        if (v == 0.0) return;
        char dbuf[40];
        std::snprintf(dbuf, sizeof(dbuf), "  %s %+.2f", name, v);
        deltas += dbuf;
      };
      push("temp", stage.temperature_delta_k);
      push("atm", stage.atmosphere_delta);
      push("water", stage.water_delta);
      push("grav", stage.gravity_delta);
      char sbuf[160];
      std::snprintf(sbuf, sizeof(sbuf), "%s  %.0fd%s%s%s", stage.id.c_str(),
                    stage.duration_days, deltas.c_str(),
                    stage.add_tags.empty() ? "" : "  +tags",
                    stage.remove_tags.empty() ? "" : "  -tags");
      out.overlay.push_back(Text{{tx, ty}, sbuf, muted, font});
      ty += 16 * s;
    }
  }
}

// ---- AI tool: StrategicMind utility decision debugger -----------------

constexpr std::array<const char *, 4> kAiActions{
    "expand.mining", "research.push", "build.fleet", "fortify"};

void init_ai(Shell::AiDemo &ai) {
  namespace eng = engine;
  auto reg = [&ai](const char *id, const char *domain,
                   std::function<double()> score,
                   std::function<void()> commit, double cooldown,
                   double weight = 1.0) {
    ai.mind.add_action({.id = id, .domain = domain,
                        .score = std::move(score),
                        .commit = std::move(commit),
                        .cooldown_days = cooldown,
                        .weight = weight});
  };
  reg("expand.mining", "economy",
      [&ai] { return ai.minerals < 100.0 ? 0.8 : 0.3; },
      [&ai] { ai.mines += 1.0; }, 15.0);
  reg("research.push", "economy",
      [&ai] { return ai.minerals >= 100.0 ? 0.7 : 0.2; },
      [&ai] { ai.tech += 1.0; ai.minerals -= 50.0; }, 20.0);
  reg("build.fleet", "military",
      [&ai] { return std::min(1.0, ai.threat / 50.0 + 0.1); },
      [&ai] { ai.fleets += 1.0; ai.minerals -= 40.0; ai.threat -= 15.0; },
      30.0);
  reg("fortify", "military",
      [&ai] { return ai.threat < 20.0 ? 0.4 : 0.1; },
      [&ai] { ai.forts += 1.0; ai.minerals -= 15.0; }, 15.0);
  ai.initialized = true;
}

void render_ai(DrawList &out, Shell &shell, UiRect body, float s) {
  auto &ai = shell.ai;
  if (!ai.initialized) init_ai(ai);

  float x = body.x + 22 * s;
  float y = body.y + 18 * s;
  const int font = static_cast<int>(13 * s);
  heading(out, x, y, "AI DEBUGGER");

  shell.hit_ai_decide = {x, y, 92 * s, 24 * s};
  shell_button(out, shell.hit_ai_decide, "DECIDE", !ai.running, font, s);
  shell.hit_ai_run = {x + 100 * s, y, 82 * s, 24 * s};
  shell_button(out, shell.hit_ai_run, ai.running ? "PAUSE" : "RUN",
               ai.running, font, s);
  shell.hit_ai_threat_dn = {x + 190 * s, y, 30 * s, 24 * s};
  shell_button(out, shell.hit_ai_threat_dn, "-", false, font, s);
  shell.hit_ai_threat_up = {x + 224 * s, y, 30 * s, 24 * s};
  shell_button(out, shell.hit_ai_threat_up, "+", false, font, s);
  out.overlay.push_back(Text{{x + 262 * s, y + 4 * s}, "threat", muted,
                             font});
  shell.hit_ai_reset = {x + 340 * s, y, 82 * s, 24 * s};
  shell_button(out, shell.hit_ai_reset, "RESET", false, font, s);
  y += 32 * s;

  char buf[96];
  std::snprintf(buf, sizeof(buf), "day %.0f", ai.day);
  line(out, x, y, "date", buf, font);
  std::snprintf(buf, sizeof(buf), "%.0f (+%.1f/d)", ai.minerals,
                ai.mines * 2.0 - ai.fleets * 0.5);
  line(out, x, y, "minerals", buf, font);
  std::snprintf(buf, sizeof(buf), "%.0f mines  %.0f fleets  %.0f forts",
                ai.mines, ai.fleets, ai.forts);
  line(out, x, y, "assets", buf, font);
  std::snprintf(buf, sizeof(buf), "%.0f  (tech %.0f)", ai.threat, ai.tech);
  line(out, x, y, "threat", buf, font);
  for (const char *domain : {"economy", "military"}) {
    const auto inc = ai.mind.incumbent(domain);
    line(out, x, y, domain,
         inc ? *inc + "  (day " +
                   std::to_string(static_cast<int>(
                       ai.mind.last_commit_day(domain))) +
                   ")"
             : "none committed", font);
  }
  y += 6 * s;

  // Action table — live utility scores, click toggles enabled.
  heading(out, x, y, "ACTIONS");
  const float row_h = 22 * s;
  shell.hit_ai_actions.resize(kAiActions.size());
  for (std::size_t i = 0; i < kAiActions.size(); ++i) {
    const auto *action = ai.mind.action(kAiActions[i]);
    if (!action) continue;
    shell.hit_ai_actions[i] = {x, y, body.width * 0.46f, row_h};
    out.overlay.push_back(FilledRectangle{shell.hit_ai_actions[i],
                                          {6, 16, 26, 255}});
    std::snprintf(buf, sizeof(buf),
                  "%-16s %-8s util %.2f  w%.1f  cd %.0fd%s",
                  action->id.c_str(), action->domain.c_str(),
                  action->score(), action->weight, action->cooldown_days,
                  action->enabled ? "" : "  DISABLED");
    out.overlay.push_back(
        Text{{x + 6 * s, y + 4 * s}, buf,
             action->enabled ? ink : muted, font});
    y += row_h + 2 * s;
  }
  y += 6 * s;

  // Decision journal — newest first.
  heading(out, x, y, "JOURNAL");
  const auto &journal = ai.mind.journal();
  if (journal.empty()) {
    line(out, x, y, "state", "no decisions — DECIDE or RUN", font);
  } else {
    std::size_t shown = 0;
    for (auto it = journal.rbegin();
         it != journal.rend() && shown < 12; ++it, ++shown) {
      std::snprintf(buf, sizeof(buf), "d%.0f  %-8s %-16s u%.2f  %u cand%s",
                    it->at_day, it->domain.c_str(), it->action_id.c_str(),
                    it->utility, it->candidates,
                    it->switched ? "  SWITCH" : "");
      out.overlay.push_back(Text{{x, y}, buf, muted, font});
      y += 15 * s;
    }
  }
}

void init_warfare(Shell::WarfareDemo &war) {
  auto &m = war.model;
  m.define_class({.id = "class.destroyer",
                  .role = "line",
                  .attack = 14.0,
                  .defense = 2.0,
                  .hull = 90.0,
                  .speed = 8.0,
                  .supply_per_day = 0.4,
                  .interdiction = 0.0});
  m.define_class({.id = "class.escort",
                  .role = "escort",
                  .attack = 6.0,
                  .defense = 5.0,
                  .hull = 45.0,
                  .speed = 10.0,
                  .supply_per_day = 0.2,
                  .interdiction = 3.0});
  m.define_class({.id = "class.transport",
                  .role = "transport",
                  .attack = 0.0,
                  .defense = 0.0,
                  .hull = 120.0,
                  .speed = 5.0,
                  .supply_per_day = 0.6,
                  .interdiction = 0.0});
  m.add_fleet(1, 1, 0.0, 0.0);
  m.add_ships(1, "class.destroyer", 40.0, 1.0, 0.2);
  m.add_ships(1, "class.escort", 12.0, 0.9, 0.1);
  m.add_fleet(2, 2, 90.0, 60.0);
  m.add_ships(2, "class.destroyer", 32.0, 0.85, 0.0);
  m.add_ships(2, "class.transport", 8.0, 1.0, 0.0);
  m.add_fleet(3, 1, 45.0, 30.0);
  m.add_ships(3, "class.escort", 20.0, 1.0, 0.4);
  m.set_order(3, {engine::FleetOrderKind::Interdict, 45.0, 30.0});
  m.set_order(2, {engine::FleetOrderKind::Move, 0.0, 0.0});
  war.day = 0.0;
  war.running = false;
  war.run_accum = 0.0;
  war.selected = 1;
  war.last_engagement = "none";
  war.initialized = true;
}

const char *war_order_name(engine::FleetOrderKind kind) {
  switch (kind) {
    case engine::FleetOrderKind::Hold: return "HOLD";
    case engine::FleetOrderKind::Move: return "MOVE";
    case engine::FleetOrderKind::Interdict: return "INTERDICT";
    case engine::FleetOrderKind::Retreat: return "RETREAT";
  }
  return "?";
}

void render_warfare(DrawList &out, Shell &shell, UiRect body, float s) {
  auto &war = shell.war;
  if (!war.initialized) init_warfare(war);
  auto &m = war.model;

  float x = body.x + 22 * s;
  float y = body.y + 18 * s;
  const int font = static_cast<int>(13 * s);
  heading(out, x, y, "WARFARE THEATER");

  shell.hit_war_step = {x, y, 86 * s, 24 * s};
  shell_button(out, shell.hit_war_step, "STEP 5D", !war.running, font, s);
  shell.hit_war_run = {x + 94 * s, y, 82 * s, 24 * s};
  shell_button(out, shell.hit_war_run, war.running ? "PAUSE" : "RUN",
               war.running, font, s);
  shell.hit_war_order = {x + 184 * s, y, 96 * s, 24 * s};
  shell_button(out, shell.hit_war_order, "ORDER", !war.running, font, s);
  shell.hit_war_engage = {x + 288 * s, y, 96 * s, 24 * s};
  shell_button(out, shell.hit_war_engage, "ENGAGE", !war.running, font, s);
  shell.hit_war_reset = {x + 392 * s, y, 82 * s, 24 * s};
  shell_button(out, shell.hit_war_reset, "RESET", false, font, s);
  y += 32 * s;

  char buf[160];
  std::snprintf(buf, sizeof(buf), "day %.0f", war.day);
  line(out, x, y, "date", buf, font);
  line(out, x, y, "last engagement", war.last_engagement, font);
  {
    const auto *sel = m.fleet(war.selected);
    std::snprintf(buf, sizeof(buf), "fleet %llu %s",
                  static_cast<unsigned long long>(war.selected),
                  sel ? war_order_name(sel->order.kind) : "(gone)");
    line(out, x, y, "selected", buf, font);
  }
  y += 6 * s;

  // Fleet table — click a row to select it for ORDER/ENGAGE.
  heading(out, x, y, "FLEETS");
  const float row_h = 22 * s;
  const auto fleets = m.fleets();
  shell.hit_war_fleets.clear();
  for (const auto *fleet : fleets) {
    const auto report = m.report(fleet->id);
    UiRect row{x, y, body.width * 0.62f, row_h};
    shell.hit_war_fleets.push_back(row);
    out.overlay.push_back(FilledRectangle{
        row, fleet->id == war.selected ? Color{26, 48, 76, 255}
                                       : Color{6, 16, 26, 255}});
    std::snprintf(buf, sizeof(buf),
                  "fleet %-2llu owner %-2llu pos (%5.0f,%5.0f)  %-9s%s",
                  static_cast<unsigned long long>(fleet->id),
                  static_cast<unsigned long long>(fleet->owner),
                  fleet->x, fleet->y, war_order_name(fleet->order.kind),
                  fleet->engaged ? "  ENGAGED" : "");
    out.overlay.push_back(Text{{x + 8 * s, y + 4 * s}, buf, ink, font});
    std::snprintf(buf, sizeof(buf),
                  "ships %.0f  atk %.0f/d  hull %.0f  spd %.1f  int %.1f  "
                  "sup %.1f/d",
                  report.ships, report.attack, report.hull, report.speed,
                  report.interdiction_radius, report.supply_per_day);
    out.overlay.push_back(
        Text{{x + 8 * s, y + 4 * s + row_h}, buf, muted, font});
    y += row_h * 2 + 4 * s;
  }
  if (fleets.empty()) {
    out.overlay.push_back(Text{{x, y}, "no fleets in theater", muted,
                               font});
    y += row_h;
  }
  y += 8 * s;

  // Cohort detail for the selected fleet.
  heading(out, x, y, "SELECTED FLEET COHORTS");
  const auto cohorts = m.cohorts(war.selected);
  for (const auto *cohort : cohorts) {
    std::snprintf(buf, sizeof(buf),
                  "%-16s ships %6.0f  cond %.2f  exp %.2f",
                  cohort->ship_class.c_str(), cohort->count,
                  cohort->condition, cohort->experience);
    out.overlay.push_back(Text{{x, y}, buf, muted, font});
    y += 16 * s;
  }
  if (cohorts.empty()) {
    out.overlay.push_back(Text{{x, y}, "no cohorts", muted, font});
    y += 16 * s;
  }

  // Interdiction check at the hostile fleet's position.
  if (const auto *hostile = m.fleet(2)) {
    const bool gated =
        m.interdicted(hostile->x, hostile->y, hostile->owner, hostile->id);
    std::snprintf(buf, sizeof(buf), "fleet 2 position %s by hostile "
                  "interdiction", gated ? "GATED" : "clear of");
    y += 8 * s;
    out.overlay.push_back(Text{{x, y}, buf, gated ? ink : muted, font});
    y += 16 * s;
  }
}

void mission_log(Shell::MissionDemo &mis, std::string entry) {
  mis.log.push_back(std::move(entry));
  if (mis.log.size() > 14) mis.log.erase(mis.log.begin());
}

void init_missions(Shell::MissionDemo &mis) {
  mis.runtime.clear(); // drops definitions + instances; keeps the bus
  mis.effects_sub.unsubscribe();
  mis.log.clear();
  // Data-driven definitions parsed from JSON — the same path a game's
  // content packages would take.
  constexpr const char *kSurvey = R"json({
    "id": "survey.helion",
    "triggers": [{"event": "SurveyComplete",
                  "conditions": [{"field": "system_id", "equals": "7"}],
                  "stage": "briefing"}],
    "stages": {
      "briefing": {"title_key": "M_HEL_B_T", "body_key": "M_HEL_B_B",
                   "timer_days": 20, "timeout_stage": "expired",
                   "choices": [{"id": "investigate", "next": "dig",
                                "effects": ["spawn_excavation"]},
                               {"id": "ignore", "next": "", "effects": []}]},
      "dig": {"title_key": "M_HEL_D_T", "body_key": "M_HEL_D_B",
              "choices": [{"id": "open_vault", "next": "",
                           "effects": ["reveal_vault", "grant_artifact"]}]},
      "expired": {"title_key": "M_HEL_E_T", "body_key": "M_HEL_E_B"}
    }
  })json";
  constexpr const char *kAnomaly = R"json({
    "id": "anomaly.whisper",
    "triggers": [{"event": "AnomalyFound", "conditions": [],
                  "stage": "arrival"}],
    "stages": {
      "arrival": {"title_key": "M_W_A_T", "body_key": "M_W_A_B",
                  "timer_days": 30, "timeout_stage": "expired",
                  "choices": [{"id": "investigate", "next": "dig",
                               "effects": ["spawn_excavation"]},
                              {"id": "dismiss", "next": "", "effects": []}]},
      "dig": {"title_key": "M_W_D_T", "body_key": "M_W_D_B",
              "choices": [{"id": "catalog", "next": "",
                           "effects": ["grant_data"]}]},
      "expired": {"title_key": "M_W_E_T", "body_key": "M_W_E_B"}
    }
  })json";
  std::string error;
  auto survey = engine::MissionDefinition::parse(kSurvey, &error);
  if (survey) mis.runtime.add_definition(std::move(*survey), &error);
  auto anomaly = engine::MissionDefinition::parse(kAnomaly, &error);
  if (anomaly) mis.runtime.add_definition(std::move(*anomaly), &error);
  if (!error.empty()) mission_log(mis, "definition error: " + error);
  mis.effects_sub = mis.bus.subscribe<engine::MissionEffectEvent>(
      [&mis](const engine::MissionEffectEvent &e) {
        std::string entry = "#" + std::to_string(e.instance_id) + " " +
                            e.mission_id + " -> " + e.stage_id;
        for (const auto &fx : e.effects) entry += "  [" + fx + "]";
        mission_log(mis, std::move(entry));
      });
  mis.day = 0.0;
  mis.event_cursor = 0;
  mis.selected = 0;
  mis.running = false;
  mis.run_accum = 0.0;
  mis.saved_state.clear();
  mis.initialized = true;
}

// Canned domain events FIRE cycles through: one matching each definition
// and one that matches nothing, so filtering is visible.
void fire_mission_event(Shell::MissionDemo &mis) {
  static constexpr std::array<std::pair<const char *, const char *>, 3>
      kEvents{{{"SurveyComplete", "{\"system_id\":7}"},
               {"AnomalyFound", "{\"sector\":\"drift\"}"},
               {"SurveyComplete", "{\"system_id\":12}"}}};
  const auto &[name, payload] = kEvents[mis.event_cursor % kEvents.size()];
  ++mis.event_cursor;
  mission_log(mis, std::string("event ") + name + " " + payload);
  mis.runtime.handle_event(name, payload);
}

void render_missions(DrawList &out, Shell &shell, UiRect body, float s) {
  auto &mis = shell.missions;
  if (!mis.initialized) init_missions(mis);
  auto &rt = mis.runtime;

  float x = body.x + 22 * s;
  float y = body.y + 18 * s;
  const int font = static_cast<int>(13 * s);
  heading(out, x, y, "MISSION RUNTIME");

  shell.hit_mis_fire = {x, y, 100 * s, 24 * s};
  shell_button(out, shell.hit_mis_fire, "FIRE EVENT", !mis.running, font, s);
  shell.hit_mis_step = {x + 108 * s, y, 92 * s, 24 * s};
  shell_button(out, shell.hit_mis_step, "STEP 10D", !mis.running, font, s);
  shell.hit_mis_run = {x + 208 * s, y, 82 * s, 24 * s};
  shell_button(out, shell.hit_mis_run, mis.running ? "PAUSE" : "RUN",
               mis.running, font, s);
  shell.hit_mis_choose = {x + 298 * s, y, 92 * s, 24 * s};
  shell_button(out, shell.hit_mis_choose, "CHOOSE", !mis.running, font, s);
  shell.hit_mis_save = {x + 398 * s, y, 72 * s, 24 * s};
  shell_button(out, shell.hit_mis_save, "SAVE", false, font, s);
  shell.hit_mis_load = {x + 478 * s, y, 72 * s, 24 * s};
  shell_button(out, shell.hit_mis_load, "LOAD", mis.saved_state.empty(),
               font, s);
  shell.hit_mis_reset = {x + 558 * s, y, 82 * s, 24 * s};
  shell_button(out, shell.hit_mis_reset, "RESET", false, font, s);
  y += 32 * s;

  char buf[200];
  std::snprintf(buf, sizeof(buf), "day %.0f", mis.day);
  line(out, x, y, "date", buf, font);
  std::snprintf(buf, sizeof(buf), "%zu bytes",
                mis.saved_state.size());
  line(out, x, y, "saved snapshot",
       mis.saved_state.empty() ? "(none)" : buf, font);
  y += 6 * s;

  heading(out, x, y, "INSTANCES");
  const float row_h = 20 * s;
  const auto instances = rt.instances();
  shell.hit_mis_instances.clear();
  for (const auto &inst : instances) {
    UiRect row{x, y, body.width * 0.62f, row_h};
    shell.hit_mis_instances.push_back(row);
    out.overlay.push_back(FilledRectangle{
        row, inst.id == mis.selected ? Color{26, 48, 76, 255}
                                     : Color{6, 16, 26, 255}});
    std::string choices;
    if (const auto *def = rt.definition(inst.mission_id)) {
      const auto stage = def->stages.find(inst.stage_id);
      if (stage != def->stages.end())
        for (const auto &c : stage->second.choices)
          choices += "  <" + c.id + ">";
    }
    std::snprintf(buf, sizeof(buf), "#%-3llu %-16s stage %-10s %.0fd left%s",
                  static_cast<unsigned long long>(inst.id),
                  inst.mission_id.c_str(), inst.stage_id.c_str(),
                  inst.days_remaining, choices.c_str());
    out.overlay.push_back(Text{{x + 8 * s, y + 3 * s}, buf, ink, font});
    y += row_h + 3 * s;
  }
  if (instances.empty()) {
    out.overlay.push_back(Text{{x, y}, "no running instances - fire an "
                               "event to trigger one", muted, font});
    y += row_h;
  }
  y += 8 * s;

  heading(out, x, y, "EFFECT LOG");
  for (const auto &entry : mis.log) {
    out.overlay.push_back(Text{{x, y}, entry, muted, font});
    y += 16 * s;
  }
  if (mis.log.empty()) {
    out.overlay.push_back(Text{{x, y}, "no MissionEffectEvents yet",
                               muted, font});
    y += 16 * s;
  }
}

void phys_log(Shell::PhysicsDemo &phys, std::string entry) {
  phys.log.push_back(std::move(entry));
  if (phys.log.size() > 12) phys.log.erase(phys.log.begin());
}

void init_physics(Shell::PhysicsDemo &phys) {
  phys.world = engine::PhysicsWorld{128.0f};
  // A drifting mover that crosses a trigger volume, a wall AABB and a
  // static target the raycast/sweep queries aim at.
  phys.mover = phys.world.add_body(
      {engine::PhysicsShapeKind::Circle, 24.0f, 0.0f, 0.0f},
      40.0f, 200.0f, /*layer*/ 1, /*trigger*/ false);
  phys.world.set_velocity(phys.mover, 90.0f, 6.0f);
  phys.world.add_body({engine::PhysicsShapeKind::Aabb, 0.0f, 16.0f, 90.0f},
                      320.0f, 60.0f, 1, false); // wall
  phys.world.add_body(
      {engine::PhysicsShapeKind::Circle, 70.0f, 0.0f, 0.0f},
      430.0f, 160.0f, 2, true); // trigger zone (own layer)
  phys.target = phys.world.add_body(
      {engine::PhysicsShapeKind::Circle, 30.0f, 0.0f, 0.0f},
      560.0f, 260.0f, 1, false);
  phys.world.add_body({engine::PhysicsShapeKind::Aabb, 0.0f, 20.0f, 20.0f},
                      180.0f, 420.0f, 4, false); // off-mask platform
  phys.seconds = 0.0;
  phys.selected = phys.mover;
  phys.last_query = "none";
  phys.log.clear();
  phys.running = false;
  phys.run_accum = 0.0;
  phys.initialized = true;
}

void phys_step(Shell::PhysicsDemo &phys, double dt) {
  const auto events = phys.world.advance(static_cast<float>(dt));
  phys.seconds += dt;
  for (const auto &e : events) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "trigger %llu %s body %llu",
                  static_cast<unsigned long long>(e.trigger),
                  e.entered ? "ENTER" : "EXIT",
                  static_cast<unsigned long long>(e.other));
    phys_log(phys, buf);
  }
}

void render_physics(DrawList &out, Shell &shell, UiRect body, float s) {
  auto &phys = shell.phys;
  if (!phys.initialized) init_physics(phys);
  auto &w = phys.world;

  float x = body.x + 22 * s;
  float y = body.y + 18 * s;
  const int font = static_cast<int>(13 * s);
  heading(out, x, y, "PHYSICS WORLD");

  shell.hit_phys_step = {x, y, 96 * s, 24 * s};
  shell_button(out, shell.hit_phys_step, "STEP 0.5", !phys.running, font, s);
  shell.hit_phys_run = {x + 104 * s, y, 82 * s, 24 * s};
  shell_button(out, shell.hit_phys_run, phys.running ? "PAUSE" : "RUN",
               phys.running, font, s);
  shell.hit_phys_ray = {x + 194 * s, y, 96 * s, 24 * s};
  shell_button(out, shell.hit_phys_ray, "RAYCAST", !phys.running, font, s);
  shell.hit_phys_sweep = {x + 298 * s, y, 84 * s, 24 * s};
  shell_button(out, shell.hit_phys_sweep, "SWEEP", !phys.running, font, s);
  shell.hit_phys_reset = {x + 390 * s, y, 82 * s, 24 * s};
  shell_button(out, shell.hit_phys_reset, "RESET", false, font, s);
  y += 32 * s;

  char buf[200];
  std::snprintf(buf, sizeof(buf), "t=%.1fs  bodies %zu", phys.seconds,
                w.size());
  line(out, x, y, "world", buf, font);
  line(out, x, y, "last query", phys.last_query, font);
  y += 6 * s;

  heading(out, x, y, "BODIES");
  const float row_h = 20 * s;
  shell.hit_phys_bodies.clear();
  std::vector<const engine::PhysicsBody *> bodies;
  // PhysicsWorld exposes ids via queries, not an iterator — gather the
  // live set with a broad overlap query over the demo bounds.
  for (const auto id : w.overlap_aabb(-1000.0f, -1000.0f, 2000.0f,
                                      2000.0f)) {
    if (const auto *b = w.body(id)) bodies.push_back(b);
  }
  std::sort(bodies.begin(), bodies.end(),
            [](const auto *a, const auto *b) { return a->id < b->id; });
  for (const auto *b : bodies) {
    UiRect row{x, y, body.width * 0.62f, row_h};
    shell.hit_phys_bodies.push_back(row);
    out.overlay.push_back(FilledRectangle{
        row, b->id == phys.selected ? Color{26, 48, 76, 255}
                                    : Color{6, 16, 26, 255}});
    std::snprintf(buf, sizeof(buf),
                  "#%-3llu %-6s pos (%5.0f,%5.0f) vel (%4.0f,%4.0f) "
                  "layer %u%s",
                  static_cast<unsigned long long>(b->id),
                  b->shape.kind == engine::PhysicsShapeKind::Circle
                      ? "circle" : "aabb",
                  b->x, b->y, b->velocity_x, b->velocity_y, b->layer,
                  b->trigger ? "  TRIGGER" : "");
    out.overlay.push_back(Text{{x + 8 * s, y + 3 * s}, buf, ink, font});
    y += row_h + 3 * s;
  }
  y += 8 * s;

  heading(out, x, y, "TRIGGER EVENT LOG");
  for (const auto &entry : phys.log) {
    out.overlay.push_back(Text{{x, y}, entry, muted, font});
    y += 16 * s;
  }
  if (phys.log.empty()) {
    out.overlay.push_back(
        Text{{x, y}, "no trigger events yet - the mover crosses a "
              "trigger volume as time advances", muted, font});
    y += 16 * s;
  }
}

// Builds the deterministic demo star chart: a golden-angle spiral disk of
// systems, each connected to its two nearest neighbors (deduplicated
// pairs), colony markers on every fifth system and three fleet travellers
// that hop along the lane graph while the demo runs.
void init_galaxy(Shell::GalaxyDemo &gal) {
  gal.map.clear();
  constexpr int count = 26;
  for (int i = 0; i < count; ++i) {
    const double radius = 3.2 * std::sqrt(static_cast<double>(i + 1));
    const double theta = i * 2.3999632297286533;
    const double jitter = ((i * 37) % 11) * 0.13;
    engine::GalaxySystem s;
    s.id = static_cast<std::uint64_t>(i + 1);
    s.name = "SYS-" + std::to_string(i + 1);
    s.x_light_years = std::cos(theta) * (radius + jitter);
    s.y_light_years = std::sin(theta) * (radius + jitter);
    s.classification = i % 4 == 0 ? "G yellow dwarf" :
                       i % 4 == 1 ? "M red dwarf"   :
                       i % 4 == 2 ? "K orange dwarf" : "A white star";
    if (i % 6 == 0) s.tags.push_back("habitable");
    if (i % 9 == 4) s.tags.push_back("anomaly");
    gal.map.add_system(std::move(s));
  }
  // Two nearest neighbors per system, deduplicated unordered pairs.
  std::set<std::pair<std::uint64_t, std::uint64_t>> pairs;
  const auto ids = gal.map.system_ids();
  for (const auto id : ids) {
    std::vector<std::pair<double, std::uint64_t>> nearest;
    for (const auto other : ids) {
      if (other == id) continue;
      nearest.emplace_back(gal.map.distance_light_years(id, other), other);
    }
    std::sort(nearest.begin(), nearest.end());
    for (int hop = 0; hop < 2 && hop < static_cast<int>(nearest.size());
         ++hop) {
      const auto other = nearest[hop].second;
      pairs.emplace(std::min(id, other), std::max(id, other));
    }
  }
  std::uint64_t lane_id = 1;
  for (const auto &[a, b] : pairs)
    gal.map.add_lane({lane_id++, a, b, gal.map.distance_light_years(a, b),
                      true});
  // Colony markers on every fifth system, owner cycles 1..3.
  std::uint64_t marker_id = 100;
  for (const auto id : ids) {
    if (id % 5 != 0) continue;
    const auto *s = gal.map.system(id);
    engine::GalaxyMarker colony;
    colony.id = marker_id++;
    colony.kind = engine::GalaxyMarker::Kind::Colony;
    colony.label = s->name + " colony";
    colony.owner_id = id % 3 + 1;
    colony.x_light_years = s->x_light_years;
    colony.y_light_years = s->y_light_years;
    colony.system_id = id;
    gal.map.add_marker(colony);
  }
  // Three fleet travellers on distinct starts.
  gal.travellers.clear();
  for (int t = 0; t < 3; ++t) {
    const auto home = ids[static_cast<std::size_t>(t * 8) % ids.size()];
    const auto hops = gal.map.neighbors(home);
    if (hops.empty()) continue;
    const auto *s = gal.map.system(home);
    engine::GalaxyMarker fleet;
    fleet.id = marker_id++;
    fleet.kind = engine::GalaxyMarker::Kind::Fleet;
    fleet.label = "FTL-" + std::to_string(t + 1);
    fleet.owner_id = static_cast<std::uint64_t>(t + 1);
    fleet.x_light_years = s->x_light_years;
    fleet.y_light_years = s->y_light_years;
    fleet.system_id = home;
    fleet.destination_system_id = hops.front();
    gal.map.add_marker(fleet);
    gal.travellers.push_back(
        {fleet.id, home, hops.front(), 0.0, 0});
  }
  gal.day = 0.0;
  gal.running = false;
  gal.run_accum = 0.0;
  gal.selected = ids.front();
  gal.destination = ids.back();
  gal.zoom = 1.0f;
  gal.initialized = true;
}

// Advances travellers along their current leg at 1.5 ly/day; on arrival
// the next enabled-lane neighbor becomes the destination (cycling), which
// exercises neighbors()/update_marker_position over real map state.
void gal_step(Shell::GalaxyDemo &gal, double days) {
  gal.day += days;
  for (auto &t : gal.travellers) {
    const auto *a = gal.map.system(t.from), *b = gal.map.system(t.to);
    if (!a || !b || !gal.map.marker(t.marker)) continue;
    const double leg = gal.map.distance_light_years(t.from, t.to);
    t.progress += 1.5 * days;
    if (t.progress >= leg) {
      const auto hops = gal.map.neighbors(t.to);
      t.from = t.to;
      t.next_hop = hops.empty() ? 0 : (t.next_hop + 1) % hops.size();
      t.to = hops.empty() ? t.from : hops[t.next_hop];
      t.progress = 0.0;
      gal.map.set_marker_system(t.marker, t.from);
      gal.map.set_marker_destination(t.marker, t.to);
      if (const auto *s = gal.map.system(t.from))
        gal.map.update_marker_position(t.marker, s->x_light_years,
                                       s->y_light_years);
    } else {
      const double f = t.progress / leg;
      gal.map.set_marker_system(t.marker, std::nullopt);
      gal.map.update_marker_position(
          t.marker, a->x_light_years + (b->x_light_years - a->x_light_years) * f,
          a->y_light_years + (b->y_light_years - a->y_light_years) * f);
    }
  }
}

// Inverse of the canvas transform: screen point -> light-year position
// under the same fit+zoom projection render_galaxy applies.
std::optional<std::pair<double, double>>
gal_unproject(const engine::GalaxyMap &map, const UiRect &canvas,
              const Point &position, float zoom) {
  double min_x = 1e30, min_y = 1e30, max_x = -1e30, max_y = -1e30;
  for (const auto id : map.system_ids()) {
    const auto *sys = map.system(id);
    min_x = std::min(min_x, sys->x_light_years);
    max_x = std::max(max_x, sys->x_light_years);
    min_y = std::min(min_y, sys->y_light_years);
    max_y = std::max(max_y, sys->y_light_years);
  }
  const double span_x = std::max(1e-6, max_x - min_x);
  const double span_y = std::max(1e-6, max_y - min_y);
  const double scale =
      std::min(canvas.width / span_x, canvas.height / span_y) * 0.86 * zoom;
  if (scale <= 0) return std::nullopt;
  return std::pair{(min_x + max_x) / 2.0 +
                       (position.x - canvas.x - canvas.width / 2.0) / scale,
                   (min_y + max_y) / 2.0 +
                       (position.y - canvas.y - canvas.height / 2.0) / scale};
}

void render_galaxy(DrawList &out, Shell &shell, UiRect body, float s) {
  auto &gal = shell.gal;
  if (!gal.initialized) init_galaxy(gal);
  auto &map = gal.map;

  float x = body.x + 22 * s;
  float y = body.y + 18 * s;
  const int font = static_cast<int>(13 * s);
  heading(out, x, y, "GALAXY MAP");

  shell.hit_gal_step = {x, y, 92 * s, 24 * s};
  shell_button(out, shell.hit_gal_step, "STEP DAY", !gal.running, font, s);
  shell.hit_gal_run = {x + 100 * s, y, 82 * s, 24 * s};
  shell_button(out, shell.hit_gal_run, gal.running ? "PAUSE" : "RUN",
               gal.running, font, s);
  shell.hit_gal_reset = {x + 190 * s, y, 82 * s, 24 * s};
  shell_button(out, shell.hit_gal_reset, "RESET", false, font, s);
  y += 32 * s;

  char buf[200];
  std::snprintf(buf, sizeof(buf), "day %.0f  systems %zu  lanes %zu  markers %zu",
                gal.day, map.system_count(), map.lane_count(),
                map.marker_count());
  line(out, x, y, "chart", buf, font);
  y += 4 * s;

  // Selected-system detail column.
  heading(out, x, y, "SELECTED SYSTEM");
  if (const auto *sel = map.system(gal.selected)) {
    line(out, x, y, "id", std::to_string(sel->id), font);
    line(out, x, y, "name", sel->name, font);
    line(out, x, y, "class", sel->classification.empty() ? "unclassified"
                                                       : sel->classification,
         font);
    std::snprintf(buf, sizeof(buf), "(%.1f, %.1f) ly",
                  sel->x_light_years, sel->y_light_years);
    line(out, x, y, "position", buf, font);
    std::string tags;
    for (const auto &tag : sel->tags) {
      if (!tags.empty()) tags += ", ";
      tags += tag;
    }
    line(out, x, y, "tags", tags.empty() ? "none" : tags, font);
    const auto hops = map.neighbors(gal.selected);
    std::string hop_text;
    for (const auto hop : hops) {
      if (!hop_text.empty()) hop_text += ", ";
      hop_text += std::to_string(hop);
    }
    line(out, x, y, "lane neighbors", hop_text.empty() ? "none" : hop_text,
         font);
    for (const auto lane_id : map.lanes_for(gal.selected)) {
      const auto *l = map.lane(lane_id);
      std::snprintf(buf, sizeof(buf), "lane %llu -> SYS %llu  %.1f ly%s",
                    static_cast<unsigned long long>(lane_id),
                    static_cast<unsigned long long>(
                        l->other(gal.selected)),
                    l->length_light_years,
                    l->enabled ? "" : "  DISABLED");
      line(out, x, y, "", buf, font);
    }
    y += 4 * s;
    heading(out, x, y, "MARKERS HERE");
    for (const auto mid : map.markers_in_system(gal.selected)) {
      const auto *m = map.marker(mid);
      const char *kind = m->kind == engine::GalaxyMarker::Kind::Colony
                             ? "colony" :
                         m->kind == engine::GalaxyMarker::Kind::Fleet ? "fleet"
                                                                      : "other";
      std::snprintf(buf, sizeof(buf), "%s %s  owner %llu%s", kind,
                    m->label.c_str(),
                    static_cast<unsigned long long>(m->owner_id),
                    m->destination_system_id ? "  EN ROUTE" : "");
      line(out, x, y, "", buf, font);
    }
  } else {
    line(out, x, y, "selection", "click a system on the chart", font);
  }
  // Lane-graph route from the selected system to the right-clicked
  // destination, via engine::GalaxyMap::find_route (weighted Dijkstra).
  y += 6 * s;
  heading(out, x, y, "ROUTE");
  std::vector<std::uint64_t> route;
  if (gal.destination && map.system(gal.destination))
    route = map.find_route(gal.selected, gal.destination);
  if (route.empty()) {
    line(out, x, y, "route", "unreachable", font);
  } else {
    const double length =
        map.route_length_light_years(gal.selected, gal.destination);
    std::snprintf(buf, sizeof(buf), "%zu legs  %.1f ly", route.size() - 1,
                  length);
    line(out, x, y, "route", buf, font);
    std::string path;
    for (const auto hop : route) {
      if (!path.empty()) path += " -> ";
      path += std::to_string(hop);
    }
    line(out, x, y, "", path, font);
  }

  // Chart canvas: right side of the body, dark field, lanes as lines,
  // systems as rings, colony markers filled, fleet markers accent.
  const UiRect canvas{body.x + body.width * 0.46f, body.y + 18 * s,
                      body.width * 0.52f, body.height - 60 * s};
  shell.hit_gal_map = canvas;
  out.overlay.push_back(FilledRectangle{canvas, Color{4, 8, 14, 255}});
  out.overlay.push_back(StrokedRectangle{canvas, Color{40, 64, 96, 255}});

  double min_x = 1e30, min_y = 1e30, max_x = -1e30, max_y = -1e30;
  for (const auto id : map.system_ids()) {
    const auto *sys = map.system(id);
    min_x = std::min(min_x, sys->x_light_years);
    max_x = std::max(max_x, sys->x_light_years);
    min_y = std::min(min_y, sys->y_light_years);
    max_y = std::max(max_y, sys->y_light_years);
  }
  const double span_x = std::max(1e-6, max_x - min_x);
  const double span_y = std::max(1e-6, max_y - min_y);
  const double scale = std::min(canvas.width / span_x,
                                canvas.height / span_y) *
                       0.86 * gal.zoom;
  const double cx = (min_x + max_x) / 2.0, cy = (min_y + max_y) / 2.0;
  const auto project = [&](double wx, double wy) -> Point {
    return {static_cast<float>(canvas.x + canvas.width / 2.0 +
                               (wx - cx) * scale),
            static_cast<float>(canvas.y + canvas.height / 2.0 +
                               (wy - cy) * scale)};
  };
  // Lanes on the computed route draw bright; everything else stays dim.
  std::set<std::pair<std::uint64_t, std::uint64_t>> route_legs;
  for (std::size_t i = 1; i < route.size(); ++i)
    route_legs.emplace(std::min(route[i - 1], route[i]),
                       std::max(route[i - 1], route[i]));
  for (const auto lane_id : map.lane_ids()) {
    const auto *l = map.lane(lane_id);
    const auto *a = map.system(l->first_system_id);
    const auto *b = map.system(l->second_system_id);
    const bool on_route =
        route_legs.contains({std::min(l->first_system_id,
                                      l->second_system_id),
                             std::max(l->first_system_id,
                                      l->second_system_id)});
    out.lines.push_back(
        {project(a->x_light_years, a->y_light_years),
         project(b->x_light_years, b->y_light_years),
         on_route ? Color{120, 200, 240, 255}
         : l->enabled ? Color{36, 58, 86, 255}
                      : Color{70, 30, 30, 255}});
  }
  for (const auto id : map.system_ids()) {
    const auto *sys = map.system(id);
    bool habitable = false, anomaly = false;
    for (const auto &tag : sys->tags) {
      habitable = habitable || tag == "habitable";
      anomaly = anomaly || tag == "anomaly";
    }
    const Color color = id == gal.selected   ? Color{140, 200, 255, 255}
                        : id == gal.destination ? Color{250, 180, 90, 255}
                        : anomaly            ? Color{220, 120, 120, 255}
                        : habitable          ? Color{110, 190, 140, 255}
                                             : Color{190, 200, 214, 255};
    out.circles.push_back({project(sys->x_light_years, sys->y_light_years),
                           id == gal.selected ? 7.f * s : 4.5f * s, color});
  }
  for (const auto mid : map.marker_ids()) {
    const auto *m = map.marker(mid);
    const auto at = project(m->x_light_years, m->y_light_years);
    out.circles.push_back(
        {at, 2.6f * s,
         m->kind == engine::GalaxyMarker::Kind::Colony
             ? Color{240, 210, 110, 255}
             : Color{130, 230, 230, 255}});
    if (m->destination_system_id) {
      if (const auto *d = map.system(*m->destination_system_id))
        out.lines.push_back(
            {at, project(d->x_light_years, d->y_light_years),
             Color{90, 140, 160, 160}});
    }
  }
  if (const auto *sel = map.system(gal.selected))
    out.text.push_back(
        {project(sel->x_light_years, sel->y_light_years), sel->name,
         Color{180, 220, 255, 255}, font});
  out.overlay.push_back(
      Text{{canvas.x + 8 * s, canvas.y + canvas.height - 20 * s},
           "L-click: select | R-click: route target | wheel: zoom", muted,
           font});
}

// Summarizes project content freshness: source file count and whether the
// newest change postdates the cooked manifest (i.e. needs a recook).
void update_content_status(Shell &shell) {
  if (!shell.project) {
    shell.content_status.clear();
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (now - shell.content_scan_at < std::chrono::seconds(1)) return;
  shell.content_scan_at = now;
  std::size_t files = 0;
  std::filesystem::file_time_type newest{};
  std::error_code ec;
  for (const auto &dir : shell.project->content_dirs) {
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(
             shell.project->root / dir, ec)) {
      if (!entry.is_regular_file(ec)) continue;
      ++files;
      newest = std::max(newest, entry.last_write_time(ec));
    }
  }
  const auto manifest = shell.project->root / "build" / "cooked" / "Content" /
                        "runtime.stmanifest";
  std::filesystem::file_time_type cooked_at{};
  if (std::filesystem::is_regular_file(manifest, ec))
    cooked_at = std::filesystem::last_write_time(manifest, ec);
  shell.content_status = std::to_string(files) + " source file(s)";
  if (cooked_at == std::filesystem::file_time_type{})
    shell.content_status += " - not cooked";
  else
    shell.content_status += newest > cooked_at ? " - CHANGED since cook"
                                               : " - cooked output current";
}

void render_projects(DrawList &out, Shell &shell, UiRect body, float s) {
  poll_run_process(shell);
  update_content_status(shell);
  float x = body.x + 22 * s;
  float y = body.y + 18 * s;
  const int font = static_cast<int>(13 * s);

  heading(out, x, y, "GAME PROJECTS");
  line(out, x, y, "projects root", shell.projects_root.string(), font);
  if (shell.project) {
    line(out, x, y, "open project",
         shell.project->name + "  (" + shell.project->id + ")", font);
    line(out, x, y, "path", shell.project->root.generic_string(), font);
    line(out, x, y, "engine",
         shell.project->engine_version +
             (shell.project->engine_version == STELLAR_ENGINE_VERSION
                  ? ""
                  : "  (this build is " STELLAR_ENGINE_VERSION ")"),
         font);
    const auto &plan = shell.load_plan;
    line(out, x, y, "load plan",
         std::to_string(plan.order.size()) + " packages, " +
             std::to_string(plan.conflicts.size()) + " conflicts, " +
             std::to_string(plan.errors.size() + shell.package_errors.size()) +
             " errors",
         font);
    for (const auto &pkg : plan.order)
      line(out, x, y, "  package",
           pkg.id + "  v" + pkg.version.to_string(), font);
    for (const auto &e : plan.errors)
      line(out, x, y, "  error", e, font);
    for (const auto &e : shell.package_errors)
      line(out, x, y, "  scan error", e, font);
    if (!shell.content_status.empty())
      line(out, x, y, "content", shell.content_status, font);
    {
      std::lock_guard lock(shell.project_mutex);
      if (!shell.cook_status.empty()) {
        std::string status = shell.cook_status;
        if (shell.cooking && shell.cook_total > 0)
          status += "  " + std::to_string(shell.cook_done.load()) + "/" +
                    std::to_string(shell.cook_total.load());
        line(out, x, y, "cook", status, font);
      }
      if (!shell.build_status.empty()) {
        std::string status = shell.build_status;
        if (shell.building) {
          const auto tail = last_log_line(shell.project->root / "build" /
                                          "host" / "build.log");
          if (!tail.empty()) status += "  | " + tail;
        }
        line(out, x, y, "build", status, font);
      }
      if (!shell.package_status.empty())
        line(out, x, y, "package", shell.package_status, font);
      if (!shell.test_status.empty())
        line(out, x, y, "test", shell.test_status, font);
    }
  } else {
    line(out, x, y, "open project", "none - browsing host assets", font);
  }
  y += 6 * s;

  // New-project row: name field + CREATE.
  const float field_w = body.width * .5f - 120 * s;
  shell.hit_project_name = {x, y, field_w, (font + 14) * s};
  field_box(out, shell.hit_project_name,
            shell.editing_project_name ? shell.project_name_buffer : "",
            shell.editing_project_name, "new project name...", font, s);
  shell.hit_project_create = {x + field_w + 8 * s, y, 108 * s,
                              shell.hit_project_name.height};
  shell_button(out, shell.hit_project_create, "CREATE", false, font, s);
  shell.hit_template_toggle = {body.x + body.width - 22 * s - 176 * s, y,
                               176 * s, shell.hit_project_name.height};
  shell_button(out, shell.hit_template_toggle,
               shell.blank_template ? "TPL: BLANK" : "TPL: WINDOWED",
               shell.blank_template, font, s);
  float bx = shell.hit_project_create.x + shell.hit_project_create.width +
             10 * s;
  if (shell.selected_project < shell.projects.size()) {
    shell.hit_project_open = {bx, y, 96 * s, shell.hit_project_name.height};
    shell_button(out, shell.hit_project_open, "OPEN", false, font, s);
    bx += 106 * s;
  } else {
    shell.hit_project_open = {};
  }
  if (shell.project) {
    shell.hit_project_close = {bx, y, 96 * s, shell.hit_project_name.height};
    shell_button(out, shell.hit_project_close, "CLOSE", false, font, s);
    bx += 106 * s;
    shell.hit_project_cook = {bx, y, 96 * s, shell.hit_project_name.height};
    shell_button(out, shell.hit_project_cook,
                 shell.cooking ? "COOKING" : "COOK", shell.cooking, font, s);

    // Pipeline row 2: build, run, edit, package.
    const float y2 = y + shell.hit_project_name.height + 8 * s;
    float bx2 = x;
    shell.hit_project_build = {bx2, y2, 96 * s,
                               shell.hit_project_name.height};
    shell_button(out, shell.hit_project_build,
                 shell.building ? "BUILDING" : "BUILD", shell.building, font,
                 s);
    bx2 += 106 * s;
    shell.hit_project_run = {bx2, y2, 80 * s, shell.hit_project_name.height};
    shell_button(out, shell.hit_project_run,
                 shell.run_process != nullptr ? "STOP" : "RUN",
                 shell.run_process != nullptr, font, s);
    bx2 += 90 * s;
    shell.hit_project_editor = {bx2, y2, 104 * s,
                                shell.hit_project_name.height};
    shell_button(out, shell.hit_project_editor, "EDITOR", false, font, s);
    bx2 += 114 * s;
    shell.hit_project_package = {bx2, y2, 116 * s,
                                 shell.hit_project_name.height};
    shell_button(out, shell.hit_project_package,
                 shell.packaging ? "PACKAGING" : "PACKAGE", shell.packaging,
                 font, s);
    bx2 += 126 * s;
    shell.hit_project_rename = {bx2, y2, 104 * s,
                                shell.hit_project_name.height};
    shell_button(out, shell.hit_project_rename, "RENAME", false, font, s);
    bx2 += 114 * s;
    shell.hit_project_test = {bx2, y2, 84 * s,
                              shell.hit_project_name.height};
    shell_button(out, shell.hit_project_test,
                 shell.testing ? "TESTING" : "TEST", shell.testing, font, s);
    y = y2;
  } else {
    shell.hit_project_close = {};
    shell.hit_project_cook = {};
    shell.hit_project_build = {};
    shell.hit_project_run = {};
    shell.hit_project_editor = {};
    shell.hit_project_package = {};
    shell.hit_project_rename = {};
    shell.hit_project_test = {};
  }
  y += shell.hit_project_name.height + 14 * s;

  // Discovered projects under the root.
  shell.project_rows = {x, y, body.width * .5f - 22 * s,
                        body.y + body.height - y - 16 * s};
  out.overlay.push_back(FilledRectangle{shell.project_rows, {6, 16, 26, 255}});
  out.overlay.push_back(StrokedRectangle{shell.project_rows, panel_edge});
  shell.project_list.configure(shell.projects.size(),
                               shell.project_list.row_height,
                               shell.project_rows.height);
  const auto range = shell.project_list.visible_range();
  float ry = shell.project_rows.y - shell.project_list.scroll_offset +
             range.first * shell.project_list.row_height;
  for (std::size_t i = range.first; i < range.last;
       ++i, ry += shell.project_list.row_height) {
    const UiRect row{shell.project_rows.x, ry, shell.project_rows.width,
                     shell.project_list.row_height};
    const bool is_open =
        shell.project && shell.project->root == shell.projects[i];
    if (i == shell.selected_project)
      out.overlay.push_back(FilledRectangle{row, row_selected});
    else if (row.contains(Point{shell.pointer_x, shell.pointer_y}))
      out.overlay.push_back(FilledRectangle{row, row_hover});
    out.overlay.push_back(
        Text{{row.x + 8 * s, row.y + 5 * s},
             shell.projects[i].filename().generic_string() +
                 (is_open ? "   [open]" : ""),
             is_open ? accent : ink, font, 0, shell.project_rows});
  }

  // Asset import: copies a file into the open project's base content package.
  const float ix = shell.project_rows.x + shell.project_rows.width + 20 * s;
  const float iw = body.x + body.width - ix - 22 * s;
  float iy = shell.project_rows.y;
  heading(out, ix, iy, "IMPORT ASSET");
  if (shell.project) {
    shell.hit_import_field = {ix, iy, iw, (font + 14) * s};
    field_box(out, shell.hit_import_field,
              shell.editing_import ? shell.import_buffer : "",
              shell.editing_import, "path to file...", font, s);
    iy += shell.hit_import_field.height + 8 * s;
    shell.hit_import_button = {ix, iy, 96 * s, (font + 14) * s};
    shell_button(out, shell.hit_import_button, "IMPORT", false, font, s);
    iy += shell.hit_import_button.height + 10 * s;
    out.overlay.push_back(Text{
        {ix, iy},
        "into packages/" + shell.project->id + "/content/", muted, font, iw,
        UiRect{ix, iy, iw, 60.f}});
    iy += 44 * s;
  } else {
    shell.hit_import_field = {};
    shell.hit_import_button = {};
    out.overlay.push_back(Text{{ix, iy}, "open a project to import", muted,
                               font, iw});
    iy += 44 * s;
  }

  // Additional content packages under the project's namespace.
  heading(out, ix, iy, "NEW PACKAGE");
  if (shell.project) {
    shell.hit_package_field = {ix, iy, iw, (font + 14) * s};
    field_box(out, shell.hit_package_field,
              shell.editing_package ? shell.package_buffer : "",
              shell.editing_package, "package name...", font, s);
    iy += shell.hit_package_field.height + 8 * s;
    shell.hit_package_button = {ix, iy, 96 * s, (font + 14) * s};
    shell_button(out, shell.hit_package_button, "ADD", false, font, s);
    iy += shell.hit_package_button.height + 10 * s;
    out.overlay.push_back(Text{
        {ix, iy},
        "creates packages/" + shell.project->id + ".<name>/", muted, font,
        iw, UiRect{ix, iy, iw, 60.f}});
  } else {
    shell.hit_package_field = {};
    shell.hit_package_button = {};
  }
}

void select_asset(Shell &shell, std::size_t index) {
  shell.selected_asset = index;
  shell.preview.reset();
  if (shell.show_cooked) {
    // Cooked records aren't loose files — show manifest metadata instead.
    const auto &record = shell.cooked_records[index];
    std::string info = record.id + "  —  " + record.type + "/" +
                       record.format + ", " +
                       human_bytes(record.source_bytes);
    if (record.width > 0)
      info += ", " + std::to_string(record.width) + "x" +
              std::to_string(record.height);
    if (!record.aliases.empty())
      info += "  |  alias: " + record.aliases.front();
    std::uint64_t stored = 0;
    for (const auto &chunk : record.chunks) stored += chunk.stored_bytes;
    info += "  |  packaged " + human_bytes(stored);
    shell.preview_label = info;
    return;
  }
  const auto &path = shell.asset_files[index];
  const auto full = shell.asset_root / path;
  shell.preview.reset();
  shell.preview_label =
      path.generic_string() + "  (" +
      human_bytes(std::filesystem::file_size(full)) + ")";
  if (is_image(full)) {
    try {
      shell.preview = decode_rgba_image(full, 1024);
      shell.previewed_path = full;
    } catch (const std::exception &error) {
      shell.preview_label = path.generic_string() + "  (decode failed: " +
                            error.what() + ")";
    }
  }
}

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
#else
int main(int argc, char **argv) {
#endif
  (void)argc;
  (void)argv;
  engine::RuntimeDiagnostics diagnostics("engine-shell",
                                         STELLAR_ENGINE_VERSION);
  try {
    engine::RuntimeDiagnostics::context(
        "stellar engine standalone shell");
    engine::JobSystem jobs;
    auto &profiler = engine::Profiler::instance();
    profiler.set_enabled(true);

    engine::LocalizationTable locale;
    std::string locale_status = "no catalog found";
    if (const auto path = find_path("data/locale/en.json");
        std::filesystem::is_regular_file(path)) {
      std::string error;
      if (locale.load_file(path.string(), &error))
        locale_status = path.filename().string() + " (" +
                        std::to_string(locale.size()) + " keys)";
      else
        locale_status = "load failed: " + error;
    }

    Shell shell;
    shell.jobs = &jobs;
    shell.projects_root = find_path("projects");
    std::filesystem::create_directories(shell.projects_root);
    refresh_projects(shell);
    // Headless pipeline ops run without creating the window — dispatched
    // before scan_assets so --create/--cook/--build/--package/--run/--test
    // never pay the full asset-tree walk they don't need.
    if (argc > 1) {
      std::vector<std::string> args;
      for (int i = 1; i < argc; ++i)
#ifdef _WIN32
        args.push_back(std::filesystem::path(argv[i]).generic_string());
#else
        args.emplace_back(argv[i]);
#endif
      const auto is_verb = [](const std::string &a) {
        return a == "--create" || a == "--cook" || a == "--build" ||
               a == "--package" || a == "--run" || a == "--test";
      };
      // The verb may follow "--project <root>" — normalize so the verb is
      // first and the project root becomes its positional argument.
      for (std::size_t i = 0; i < args.size(); ++i) {
        if (!is_verb(args[i])) continue;
        if (i > 0) {
          // Non-verb args first (a --create name stays positional), then
          // the --project roots as the positional project arguments.
          std::vector<std::string> reordered{args[i]}, roots;
          for (std::size_t j = 0; j < args.size(); ++j) {
            if (j == i) continue;
            if (args[j] == "--project" && j + 1 < args.size()) {
              roots.push_back(args[++j]);
              continue;
            }
            reordered.push_back(args[j]);
          }
          for (auto &r : roots) reordered.push_back(std::move(r));
          return run_headless(shell, reordered);
        }
        return run_headless(shell, args);
      }
    }
    scan_assets(shell, find_path("assets"));
    auto arg_str = [&](int i) {
#ifdef _WIN32
      return std::filesystem::path(argv[i]).generic_string();
#else
      return std::string(argv[i]);
#endif
    };
    // --project <root> opens a game project directly (e.g. launched on a
    // project directory, or from a project's own toolchain).
    for (int i = 1; i + 1 < argc; ++i) {
      if (arg_str(i) == "--project") {
        open_project(shell, arg_str(i + 1));
        shell.tool = Tool::Projects;
        break;
      }
    }
    // --tool <name> deep-links a tool tab (case-insensitive); applied after
    // --project so it wins.
    auto ieq = [](std::string_view a, std::string_view b) {
      return a.size() == b.size() &&
             std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
             });
    };
    for (int i = 1; i + 1 < argc; ++i)
      if (arg_str(i) == "--tool")
        for (std::size_t t = 0; t < kToolNames.size(); ++t)
          if (ieq(arg_str(i + 1), kToolNames[t])) {
            shell.tool = kTools[t];
            break;
          }
    // --frames N renders N frames then exits 0 — CI smoke coverage that
    // every tool initializes and renders without crashing.
    int frame_limit = 0;
    for (int i = 1; i + 1 < argc; ++i)
      if (arg_str(i) == "--frames")
        frame_limit = std::max(0, std::atoi(arg_str(i + 1).c_str()));
    for (const char *probe :
         {"GENERAL_TITLE", "STARTUP_TITLE", "MENU_RESUME", "ECONOMY_TITLE",
          "RESEARCH_TITLE", "FLEET_TITLE", "SYSTEM_BACK",
          "NOTIFY_CATEGORY_FLEET"})
      if (locale.contains(probe)) shell.sample_keys.push_back(probe);

    Window window("Stellar Engine", 1440, 900, false,
                  find_path("assets/visual/fonts/Rajdhani-SemiBold.ttf"));
    window.set_auto_frame_cap();

    std::shared_ptr<const RgbaImage> emblem;
    try {
      emblem = decode_rgba_image(
          find_path("assets/visual/branding/stellar-continuum-icon-v1.png"),
          256);
    } catch (const std::exception &) {
    }

    std::atomic<int> demo_jobs_done{};
    auto next_job = std::chrono::steady_clock::now();
    auto last_frame = std::chrono::steady_clock::now();
    double fps{};
    std::string last_input = "none";

    int frames_rendered = 0;
    for (;;) {
      if (frame_limit > 0 && frames_rendered >= frame_limit) break;
      const auto snapshot = window.poll();
      if (snapshot.quit_requested) break;
      shell.pointer_x = snapshot.pointer.x;
      shell.pointer_y = snapshot.pointer.y;
      for (const auto &event : snapshot.events) {
        if (event.type == InputEventType::EscapePressed) {
          if (shell.editing_project_name || shell.editing_import ||
              shell.editing_package || shell.editing_scene) {
            shell.editing_project_name = shell.editing_import =
                shell.editing_package = shell.editing_scene = false;
            window.set_text_input(false);
            continue;
          }
          return 0;
        }
        // Text-field editing takes precedence over tool clicks.
        if (shell.editing_project_name || shell.editing_import ||
            shell.editing_package || shell.editing_scene ||
            shell.editing_scene3) {
          std::string &buffer =
              shell.editing_import      ? shell.import_buffer
              : shell.editing_package   ? shell.package_buffer
              : shell.editing_scene     ? shell.scene_buffer
              : shell.editing_scene3    ? shell.scene3_buffer
                                        : shell.project_name_buffer;
          if (event.type == InputEventType::TextEntered) {
            if (buffer.size() < 240) buffer += event.text;
            continue;
          }
          if (event.type == InputEventType::BackspacePressed) {
            if (!buffer.empty()) buffer.pop_back();
            continue;
          }
          if (event.type == InputEventType::KeyPressed &&
              event.key == '\r') {
            const bool was_import = shell.editing_import;
            const bool was_package = shell.editing_package;
            const bool was_scene = shell.editing_scene;
            const bool was_scene3 = shell.editing_scene3;
            shell.editing_project_name = shell.editing_import =
                shell.editing_package = shell.editing_scene =
                    shell.editing_scene3 = false;
            window.set_text_input(false);
            if (was_import) import_asset(shell);
            else if (was_package) create_package(shell);
            else if (was_scene) commit_scene_field(shell);
            else if (was_scene3) commit_scene3_field(shell);
            else create_project_from_field(shell);
            continue;
          }
        }
        switch (event.type) {
        case InputEventType::PointerMove:
          last_input = "pointer move";
          if (shell.scene3_looking) {
            // Right-drag orbits the authored camera — deltas in px map
            // to degrees; pitch clamps away from the poles.
            auto &d = shell.scene3_doc;
            d.cam_yaw_deg += event.delta.x * 0.3f;
            d.cam_pitch_deg = std::clamp(
                d.cam_pitch_deg - event.delta.y * 0.3f, -89.f, 89.f);
            shell.scene3_modified = true;
          } else if (shell.scene3_dragging &&
                     shell.scene3_preview.contains(event.position)) {
            auto &d = shell.scene3_doc;
            if (auto *e = selected_scene3_entity(shell)) {
              if (shell.scene3_drag_mode == 1) {
                // Rotate: horizontal drag yaws, vertical pitches (roll
                // stays field-authored — two axes fit a 2D drag).
                e->yaw_deg = std::fmod(e->yaw_deg + event.delta.x * .5f,
                                       360.f);
                e->pitch_deg =
                    std::fmod(e->pitch_deg - event.delta.y * .5f, 360.f);
                shell.scene3_modified = true;
              } else if (shell.scene3_drag_mode == 2) {
                // Scale: horizontal drag multiplies the uniform scale.
                e->scale = std::clamp(
                    e->scale * (1.f + event.delta.x * .005f), .01f,
                    1000.f);
                shell.scene3_modified = true;
              } else {
                // Move: unproject the cursor ray onto the entity's
                // current-Y plane and write back x,z.
                const Vec3 dir =
                    scene3_ray(d, shell.scene3_preview, event.position);
                if (std::abs(dir.y) > 1e-5f) {
                  const float t = (e->y - d.cam_y) / dir.y;
                  if (t > 0.f) {
                    e->x = d.cam_x + dir.x * t;
                    e->z = d.cam_z + dir.z * t;
                    shell.scene3_modified = true;
                  }
                }
              }
            }
          } else if (shell.scene_painting && !shell.scene_paint_fill &&
              scene_tile(shell) != nullptr &&
              shell.scene_preview.contains(event.position)) {
            const auto &pv = shell.scene_preview;
            paint_tile_at(shell,
                          (event.position.x - pv.x) / pv.width * 1280.f,
                          (event.position.y - pv.y) / pv.height * 720.f);
          } else if (shell.scene_dragging &&
              shell.scene_preview.contains(event.position)) {
            if (auto *e = selected_scene_entity(shell); e != nullptr) {
              const auto &pv = shell.scene_preview;
              const float nx = std::clamp(
                  (event.position.x - pv.x) / pv.width * 1280.f, 0.f,
                  1280.f - e->w);
              const float ny = std::clamp(
                  (event.position.y - pv.y) / pv.height * 720.f, 0.f,
                  720.f - e->h);
              const float dx = nx - e->x, dy = ny - e->y;
              e->x = nx;
              e->y = ny;
              // Descendants follow so authored child offsets survive the
              // drag (they re-derive at spawn from the new positions).
              if (dx != 0.f || dy != 0.f)
                for (auto &c : shell.scene_doc.entities) {
                  std::unordered_set<std::string> seen{c.name};
                  for (std::string cur = c.parent; !cur.empty();) {
                    if (cur == e->name) {
                      c.x += dx;
                      c.y += dy;
                      break;
                    }
                    if (!seen.insert(cur).second) break;
                    const auto it = std::find_if(
                        shell.scene_doc.entities.begin(),
                        shell.scene_doc.entities.end(),
                        [&](const auto &p) { return p.name == cur; });
                    if (it == shell.scene_doc.entities.end()) break;
                    cur = it->parent;
                  }
                }
              shell.scene_modified = true;
            }
          }
          break;
        case InputEventType::RightPressed:
          last_input = "right press";
          if (shell.tool == Tool::Scene3D &&
              shell.scene3_preview.contains(event.position)) {
            // One undo step per orbit gesture.
            shell.scene3_history.commit(shell.scene3_doc);
            shell.scene3_looking = true;
          }
          break;
        case InputEventType::RightReleased:
          last_input = "right release";
          shell.scene3_looking = false;
          break;
        case InputEventType::LeftPressed:
          last_input = "left press";
          if (shell.tool == Tool::Scene3D &&
              shell.scene3_preview.contains(event.position)) {
            // Ray-pick the entity under the cursor (nearest triangle).
            const auto picked = scene3_pick(shell, event.position);
            shell.scene3_sel =
                picked.value_or(static_cast<std::size_t>(-1));
            if (picked)
              shell.status =
                  "picked " +
                  shell.scene3_doc.entities[*picked].name;
            shell.scene3_dragging =
                picked.has_value();
            if (shell.scene3_dragging)
              shell.scene3_history.commit(shell.scene3_doc);
            break;
          }
          if (shell.tool == Tool::Scene &&
              shell.scene_preview.contains(event.position)) {
            const auto &pv = shell.scene_preview;
            const float wx =
                (event.position.x - pv.x) / pv.width * 1280.f;
            const float wy =
                (event.position.y - pv.y) / pv.height * 720.f;
            if (shell.scene_paint && scene_tile(shell) != nullptr) {
              // A click inside the tile-picker strip selects the brush
              // cell instead of painting.
              if (const auto &tm = *scene_tile(shell);
                  tm.tile_w > 0 && tm.tile_h > 0 &&
                  shell.scene_sheet_scale > 0.f &&
                  shell.scene_sheet_rect.contains(event.position)) {
                const auto &sr = shell.scene_sheet_rect;
                const int cx = static_cast<int>(
                    (event.position.x - sr.x) /
                    (tm.tile_w * shell.scene_sheet_scale));
                const int cy = static_cast<int>(
                    (event.position.y - sr.y) /
                    (tm.tile_h * shell.scene_sheet_scale));
                const int rows = static_cast<int>(
                    sr.height / (tm.tile_h * shell.scene_sheet_scale));
                if (cx < 0 || cx >= shell.scene_sheet_cols || cy < 0 ||
                    cy >= rows)
                  break;
                shell.scene_paint_cell =
                    cy * shell.scene_sheet_cols + cx;
                shell.status =
                    "paint brush " + std::to_string(shell.scene_paint_cell);
                break;
              }
              // One undo step per stroke — a fill is a single stroke.
              shell.scene_history.commit(shell.scene_doc);
              shell.scene_painting = true;
              if (shell.scene_paint_fill)
                fill_tile_at(shell, wx, wy);
              else
                paint_tile_at(shell, wx, wy);
              break;
            }
            const auto order = scene_draw_order(shell.scene_doc);
            for (std::size_t n = order.size(); n-- > 0;) {
              const auto i = order[n];
              const auto &e = shell.scene_doc.entities[i];
              if (wx >= e.x && wx <= e.x + e.w && wy >= e.y &&
                  wy <= e.y + e.h) {
                shell.selected_entity = i;
                break;
              }
            }
            shell.scene_dragging =
                shell.selected_entity < shell.scene_doc.entities.size();
            if (shell.scene_dragging)
              shell.scene_history.commit(shell.scene_doc);
          }
          break;
        case InputEventType::LeftReleased: {
          last_input = "left release";
          shell.scene_painting = false;
          shell.scene3_dragging = false;
          if (!shell.scene_preview.contains(event.position))
            shell.scene_dragging = false;
          for (std::size_t i = 0; i < shell.tool_hits.size(); ++i)
            if (shell.tool_hits[i].contains(event.position))
              shell.tool = kTools[i];
          if (shell.tool == Tool::Projects) {
            if (shell.hit_project_name.contains(event.position)) {
              shell.editing_project_name = true;
              shell.editing_import = shell.editing_package = false;
              window.set_text_input(true);
            } else if (shell.hit_import_field.contains(event.position)) {
              shell.editing_import = true;
              shell.editing_project_name = shell.editing_package = false;
              window.set_text_input(true);
            } else if (shell.hit_package_field.contains(event.position)) {
              shell.editing_package = true;
              shell.editing_project_name = shell.editing_import = false;
              window.set_text_input(true);
            } else if (shell.editing_project_name || shell.editing_import ||
                       shell.editing_package) {
              shell.editing_project_name = shell.editing_import =
                  shell.editing_package = false;
              window.set_text_input(false);
            }
            if (shell.hit_import_button.contains(event.position))
              import_asset(shell);
            else if (shell.hit_package_button.contains(event.position))
              create_package(shell);
            else if (shell.hit_project_create.contains(event.position))
              create_project_from_field(shell);
            else if (shell.hit_project_open.contains(event.position) &&
                     shell.selected_project < shell.projects.size())
              open_project(shell, shell.projects[shell.selected_project]);
            else if (shell.hit_project_close.contains(event.position))
              close_project(shell);
            else if (shell.hit_project_cook.contains(event.position))
              start_cook(shell, jobs);
            else if (shell.hit_project_build.contains(event.position))
              start_build(shell, jobs);
            else if (shell.hit_project_run.contains(event.position)) {
              if (shell.run_process != nullptr)
                stop_project(shell);
              else
                run_project(shell);
            }
            else if (shell.hit_project_editor.contains(event.position))
              open_editor(shell);
            else if (shell.hit_project_package.contains(event.position))
              start_package(shell, jobs);
            else if (shell.hit_project_rename.contains(event.position))
              rename_project(shell);
            else if (shell.hit_project_test.contains(event.position))
              start_test(shell, jobs);
            else if (shell.hit_template_toggle.contains(event.position))
              shell.blank_template = !shell.blank_template;
            else if (shell.project_rows.contains(event.position)) {
              const auto row = static_cast<std::size_t>(std::max(
                  0.f, std::floor((event.position.y - shell.project_rows.y +
                                   shell.project_list.scroll_offset) /
                                  shell.project_list.row_height)));
              if (row < shell.projects.size()) shell.selected_project = row;
            }
          } else if (shell.tool == Tool::Scene) {
            auto edit_field = [&](int field) {
              shell.editing_scene = true;
              shell.scene_field = field;
              const auto *e = selected_scene_entity(shell);
              if (field == 1 && e) shell.scene_buffer = e->name;
              else if (field == 2 && e)
                shell.scene_buffer = std::to_string((int)e->x) + "," +
                                     std::to_string((int)e->y);
              else if (field == 3 && e)
                shell.scene_buffer = std::to_string((int)e->vx) + "," +
                                     std::to_string((int)e->vy);
              else if (field == 4 && e)
                shell.scene_buffer = e->sprite;
              else if (field == 5 && e)
                shell.scene_buffer = std::to_string((int)e->w) + "," +
                                     std::to_string((int)e->h);
              else if (field == 6 && e)
                shell.scene_buffer = std::to_string((int)e->r) + "," +
                                     std::to_string((int)e->g) + "," +
                                     std::to_string((int)e->b);
              else if (field == 7 && e)
                shell.scene_buffer = std::to_string(e->layer);
              else if (field == 8 && e)
                shell.scene_buffer = std::to_string(e->parallax);
              else if (field == 9 && e)
                shell.scene_buffer = e->text;
              else if (field == 10 && e)
                shell.scene_buffer = std::to_string(e->gravity_scale);
              else if (field == 11)
                shell.scene_buffer =
                    std::to_string(shell.scene_doc.gravity);
              else if (field == 12 && e)
                shell.scene_buffer = e->solid ? "true" : "false";
              else if (field == 13)
                shell.scene_buffer =
                    std::to_string((int)shell.scene_doc.bg_r) + "," +
                    std::to_string((int)shell.scene_doc.bg_g) + "," +
                    std::to_string((int)shell.scene_doc.bg_b);
              else if (field == 14 && e)
                shell.scene_buffer = std::to_string(e->frames);
              else if (field == 15 && e)
                shell.scene_buffer = std::to_string(e->fps);
              else if (field == 16 && e)
                shell.scene_buffer = std::to_string(e->rotation);
              else if (field == 17 && e)
                shell.scene_buffer = std::to_string(e->ttl);
              else if (field == 18 && e)
                shell.scene_buffer = e->flip_x ? "true" : "false";
              else if (field == 19 && e)
                shell.scene_buffer = e->flip_y ? "true" : "false";
              else if (field == 20 && e)
                shell.scene_buffer = e->visible ? "true" : "false";
              else if (field == 21 && e)
                shell.scene_buffer = e->oneway ? "true" : "false";
              else if (field == 22 && e)
                shell.scene_buffer = e->data;
              else if (field == 23 && e)
                shell.scene_buffer = std::to_string(e->opacity);
              else if (field == 24 && e)
                shell.scene_buffer = std::to_string(e->spin);
              else if (field == 25 && e)
                shell.scene_buffer = e->bounce ? "true" : "false";
              else if (field == 26 && e)
                shell.scene_buffer = e->parent;
              else if (field == 27 && e)
                shell.scene_buffer = std::to_string(e->fcols);
              else if (field == 28 && e)
                shell.scene_buffer = e->anim_loop ? "true" : "false";
              else if (field == 29 && e)
                shell.scene_buffer = e->vfx;
              else if (field == 38)
                shell.scene_buffer = shell.scene_doc.music;
              else if (field == 39)
                shell.scene_buffer =
                    std::to_string((int)shell.scene_doc.world_w) + "," +
                    std::to_string((int)shell.scene_doc.world_h);
              else if (field >= 30) {
                const auto *tm = scene_tile(shell);
                const engine::SceneTilemap dflt;
                if (tm == nullptr) tm = &dflt;
                if (field == 30)
                  shell.scene_buffer = tm->tileset;
                else if (field == 31)
                  shell.scene_buffer = std::to_string(tm->tile_w) + "," +
                                       std::to_string(tm->tile_h);
                else if (field == 32)
                  shell.scene_buffer = std::to_string(tm->columns);
                else if (field == 33)
                  shell.scene_buffer = tm->collide ? "true" : "false";
                else if (field == 34)
                  shell.scene_buffer = std::to_string(tm->layer);
                else if (field == 35)
                  shell.scene_buffer = std::to_string(tm->parallax);
                else if (field == 36) {
                  shell.scene_buffer.clear();
                  for (const int c : tm->cells) {
                    if (!shell.scene_buffer.empty())
                      shell.scene_buffer += ',';
                    shell.scene_buffer += std::to_string(c);
                  }
                } else if (field == 37)
                  shell.scene_buffer =
                      std::to_string(shell.scene_paint_cell);
                else if (field == 40)
                  shell.scene_buffer =
                      std::to_string((int)tm->x) + "," +
                      std::to_string((int)tm->y);
                else
                  shell.scene_buffer.clear();
              } else shell.scene_buffer.clear();
              window.set_text_input(true);
            };
            if (shell.hit_scene_name.contains(event.position))
              edit_field(1);
            else if (shell.hit_scene_pos.contains(event.position))
              edit_field(2);
            else if (shell.hit_scene_vel.contains(event.position))
              edit_field(3);
            else if (shell.hit_scene_sprite.contains(event.position))
              edit_field(4);
            else if (shell.hit_scene_size.contains(event.position))
              edit_field(5);
            else if (shell.hit_scene_color.contains(event.position))
              edit_field(6);
            else if (shell.hit_scene_layer.contains(event.position))
              edit_field(7);
            else if (shell.hit_scene_parallax.contains(event.position))
              edit_field(8);
            else if (shell.hit_scene_text.contains(event.position))
              edit_field(9);
            else if (shell.hit_scene_grav.contains(event.position))
              edit_field(10);
            else if (shell.hit_scene_gravity.contains(event.position))
              edit_field(11);
            else if (shell.hit_scene_solid.contains(event.position))
              edit_field(12);
            else if (shell.hit_scene_bg.contains(event.position))
              edit_field(13);
            else if (shell.hit_scene_frames.contains(event.position))
              edit_field(14);
            else if (shell.hit_scene_fps.contains(event.position))
              edit_field(15);
            else if (shell.hit_scene_rot.contains(event.position))
              edit_field(16);
            else if (shell.hit_scene_ttl.contains(event.position))
              edit_field(17);
            else if (shell.hit_scene_flipx.contains(event.position))
              edit_field(18);
            else if (shell.hit_scene_flipy.contains(event.position))
              edit_field(19);
            else if (shell.hit_scene_visible.contains(event.position))
              edit_field(20);
            else if (shell.hit_scene_oneway.contains(event.position))
              edit_field(21);
            else if (shell.hit_scene_data.contains(event.position))
              edit_field(22);
            else if (shell.hit_scene_opacity.contains(event.position))
              edit_field(23);
            else if (shell.hit_scene_tileset.contains(event.position))
              edit_field(30);
            else if (shell.hit_scene_tilesize.contains(event.position))
              edit_field(31);
            else if (shell.hit_scene_tilecols.contains(event.position))
              edit_field(32);
            else if (shell.hit_scene_tilecollide.contains(event.position))
              edit_field(33);
            else if (shell.hit_scene_tilelayer.contains(event.position))
              edit_field(34);
            else if (shell.hit_scene_tilepar.contains(event.position))
              edit_field(35);
            else if (shell.hit_scene_tileorigin.contains(event.position))
              edit_field(40);
            else if (shell.hit_scene_tilename.contains(event.position))
              edit_field(41);
            else if (shell.hit_scene_tilecells.contains(event.position))
              edit_field(36);
            else if (shell.hit_scene_paintcell.contains(event.position))
              edit_field(37);
            else if (shell.hit_scene_brushsz.contains(event.position))
              edit_field(42);
            else if (shell.hit_scene_music.contains(event.position))
              edit_field(38);
            else if (shell.hit_scene_spin.contains(event.position))
              edit_field(24);
            else if (shell.hit_scene_worldsize.contains(event.position))
              edit_field(39);
            else if (shell.hit_scene_bounce.contains(event.position))
              edit_field(25);
            else if (shell.hit_scene_parent.contains(event.position))
              edit_field(26);
            else if (shell.hit_scene_fcols.contains(event.position))
              edit_field(27);
            else if (shell.hit_scene_animloop.contains(event.position))
              edit_field(28);
            else if (shell.hit_scene_vfx.contains(event.position))
              edit_field(29);
            else if (shell.editing_scene) {
              shell.editing_scene = false;
              window.set_text_input(false);
            }
            if (shell.hit_scene_add.contains(event.position)) {
              engine::SceneEntity e;
              e.name = "entity" +
                       std::to_string(shell.scene_doc.entities.size() + 1);
              e.x = 200.f; e.y = 200.f; e.vx = 120.f; e.vy = 90.f;
              shell.scene_history.commit(shell.scene_doc);
              shell.scene_doc.entities.push_back(e);
              shell.selected_entity = shell.scene_doc.entities.size() - 1;
              shell.scene_modified = true;
            } else if (shell.hit_scene_del.contains(event.position)) {
              if (auto *e = selected_scene_entity(shell); e != nullptr) {
                shell.scene_history.commit(shell.scene_doc);
                shell.scene_doc.entities.erase(
                    shell.scene_doc.entities.begin() +
                    static_cast<std::ptrdiff_t>(shell.selected_entity));
                shell.selected_entity = static_cast<std::size_t>(-1);
                shell.scene_modified = true;
              }
            } else if (shell.hit_scene_save.contains(event.position)) {
              save_scene(shell);
            } else if (shell.hit_scene_undo.contains(event.position)) {
              if (auto prev =
                      shell.scene_history.undo(shell.scene_doc)) {
                shell.scene_doc = std::move(*prev);
                shell.selected_entity =
                    static_cast<std::size_t>(-1);
                shell.scene_modified = true;
              }
            } else if (shell.hit_scene_redo.contains(event.position)) {
              if (auto next =
                      shell.scene_history.redo(shell.scene_doc)) {
                shell.scene_doc = std::move(*next);
                shell.selected_entity =
                    static_cast<std::size_t>(-1);
                shell.scene_modified = true;
              }
            } else if (shell.hit_scene_dup.contains(event.position)) {
              if (auto *e = selected_scene_entity(shell); e != nullptr) {
                shell.scene_history.commit(shell.scene_doc);
                engine::SceneEntity copy = *e;
                copy.name += "_copy";
                copy.x += 24.f;
                copy.y += 24.f;
                shell.scene_doc.entities.push_back(std::move(copy));
                shell.selected_entity = shell.scene_doc.entities.size() - 1;
                shell.scene_modified = true;
              }
            } else if (shell.hit_scene_up.contains(event.position) ||
                       shell.hit_scene_down.contains(event.position)) {
              // Doc order is the same-layer draw order.
              const bool up = shell.hit_scene_up.contains(event.position);
              const auto i = shell.selected_entity;
              const auto j = up ? i - 1 : i + 1;
              if (i < shell.scene_doc.entities.size() &&
                  j < shell.scene_doc.entities.size() &&
                  (up ? i > 0 : true)) {
                shell.scene_history.commit(shell.scene_doc);
                std::swap(shell.scene_doc.entities[i],
                          shell.scene_doc.entities[j]);
                shell.selected_entity = j;
                shell.scene_modified = true;
              }
            } else if (shell.hit_scene_tilemap.contains(event.position)) {
              // TILES +: append a new grid layer and select it.
              shell.scene_history.commit(shell.scene_doc);
              engine::SceneTilemap tm;
              tm.columns = 16;
              tm.cells.assign(16 * 6, -1);
              shell.scene_doc.tilemaps.push_back(std::move(tm));
              shell.scene_tile_index =
                  shell.scene_doc.tilemaps.size() - 1;
              shell.scene_modified = true;
            } else if (shell.hit_scene_tilesel.contains(event.position)) {
              // MAP k/n: cycle which tilemap the fields and PAINT edit.
              const auto n = shell.scene_doc.tilemaps.size();
              if (n > 0)
                shell.scene_tile_index =
                    (shell.scene_tile_index + 1) % n;
            } else if (shell.hit_scene_tiledel.contains(event.position)) {
              // TILES -: remove the selected grid layer.
              if (auto *sel = scene_tile(shell); sel != nullptr) {
                shell.scene_history.commit(shell.scene_doc);
                shell.scene_doc.tilemaps.erase(
                    shell.scene_doc.tilemaps.begin() +
                    static_cast<std::ptrdiff_t>(shell.scene_tile_index));
                if (shell.scene_tile_index >=
                    shell.scene_doc.tilemaps.size())
                  shell.scene_tile_index =
                      shell.scene_doc.tilemaps.empty()
                          ? 0
                          : shell.scene_doc.tilemaps.size() - 1;
                shell.scene_modified = true;
              }
            } else if (shell.hit_scene_paint.contains(event.position)) {
              shell.scene_paint = !shell.scene_paint;
              if (!shell.scene_paint) shell.scene_paint_fill = false;
            } else if (shell.hit_scene_fill.contains(event.position) &&
                       shell.scene_paint) {
              shell.scene_paint_fill = !shell.scene_paint_fill;
            } else if (shell.scene_preview.contains(event.position)) {
              // Press already selected/placed; release ends the drag.
              shell.scene_dragging = false;
            } else if (shell.scene_rows.contains(event.position)) {
              const auto row = static_cast<std::size_t>(std::max(
                  0.f, std::floor((event.position.y - shell.scene_rows.y +
                                   shell.entity_list.scroll_offset) /
                                  shell.entity_list.row_height)));
              if (row < shell.scene_doc.entities.size())
                shell.selected_entity = row;
            }
          } else if (shell.tool == Tool::Scene3D) {
            auto &doc = shell.scene3_doc;
            auto edit3 = [&](int field, const std::string &seed) {
              shell.editing_scene3 = true;
              shell.scene3_field = field;
              shell.scene3_buffer = seed;
              window.set_text_input(true);
            };
            const auto *se = selected_scene3_entity(shell);
            if (shell.hit3_add.contains(event.position)) {
              shell.scene3_history.commit(doc);
              engine::Scene3dEntity e;
              e.name = "entity" + std::to_string(doc.entities.size());
              doc.entities.push_back(std::move(e));
              shell.scene3_sel = doc.entities.size() - 1;
              shell.scene3_modified = true;
            } else if (shell.hit3_del.contains(event.position) && se) {
              shell.scene3_history.commit(doc);
              doc.entities.erase(doc.entities.begin() + shell.scene3_sel);
              shell.scene3_sel = static_cast<std::size_t>(-1);
              shell.scene3_modified = true;
            } else if (shell.hit3_dup.contains(event.position) && se) {
              shell.scene3_history.commit(doc);
              doc.entities.insert(
                  doc.entities.begin() + shell.scene3_sel + 1, *se);
              ++shell.scene3_sel;
              shell.scene3_modified = true;
            } else if (shell.hit3_save.contains(event.position) &&
                       shell.scene3_modified) {
              std::filesystem::create_directories(
                  scene3_path(shell).parent_path());
              doc.save(scene3_path(shell));
              shell.scene3_modified = false;
              shell.status = "scene3d saved - " +
                             scene3_path(shell).filename().string();
            } else if (shell.hit3_undo.contains(event.position)) {
              if (auto d = shell.scene3_history.undo(doc)) {
                doc = std::move(*d);
                shell.scene3_modified = true;
              }
            } else if (shell.hit3_redo.contains(event.position)) {
              if (auto d = shell.scene3_history.redo(doc)) {
                doc = std::move(*d);
                shell.scene3_modified = true;
              }
            } else if (shell.hit3_mode_move.contains(event.position)) {
              shell.scene3_drag_mode = 0;
            } else if (shell.hit3_mode_rot.contains(event.position)) {
              shell.scene3_drag_mode = 1;
            } else if (shell.hit3_mode_scale.contains(event.position)) {
              shell.scene3_drag_mode = 2;
            } else if (shell.hit3_name.contains(event.position) && se)
              edit3(1, se->name);
            else if (shell.hit3_pos.contains(event.position) && se)
              edit3(2, std::to_string((int)se->x) + "," +
                           std::to_string((int)se->y) + "," +
                           std::to_string((int)se->z));
            else if (shell.hit3_vel.contains(event.position) && se)
              edit3(3, std::to_string((int)se->vx) + "," +
                           std::to_string((int)se->vy) + "," +
                           std::to_string((int)se->vz));
            else if (shell.hit3_mesh.contains(event.position) && se)
              edit3(4, se->mesh);
            else if (shell.hit3_rot.contains(event.position) && se)
              edit3(5, std::to_string((int)se->yaw_deg) + "," +
                           std::to_string((int)se->pitch_deg) + "," +
                           std::to_string((int)se->roll_deg));
            else if (shell.hit3_scale.contains(event.position) && se)
              edit3(6, std::to_string(se->scale));
            else if (shell.hit3_color.contains(event.position) && se)
              edit3(7, std::to_string(se->r) + "," +
                           std::to_string(se->g) + "," +
                           std::to_string(se->b));
            else if (shell.hit3_tex.contains(event.position) && se)
              edit3(8, se->texture);
            else if (shell.hit3_opacity.contains(event.position) && se)
              edit3(9, std::to_string(se->opacity));
            else if (shell.hit3_solid.contains(event.position) && se)
              edit3(11, se->solid ? "true" : "false");
            else if (shell.hit3_dbl.contains(event.position) && se)
              edit3(12, se->double_sided ? "true" : "false");
            else if (shell.hit3_gravs.contains(event.position) && se)
              edit3(10, std::to_string(se->gravity_scale));
            else if (shell.hit3_ttl.contains(event.position) && se)
              edit3(13, std::to_string(se->ttl));
            else if (shell.hit3_data.contains(event.position) && se)
              edit3(14, se->data);
            else if (shell.hit3_parent.contains(event.position) && se)
              edit3(15, se->parent);
            else if (shell.hit3_vfx.contains(event.position) && se)
              edit3(16, se->vfx);
            else if (shell.hit3_cam.contains(event.position))
              edit3(20, std::to_string((int)doc.cam_x) + "," +
                            std::to_string((int)doc.cam_y) + "," +
                            std::to_string((int)doc.cam_z));
            else if (shell.hit3_camrot.contains(event.position))
              edit3(21, std::to_string((int)doc.cam_yaw_deg) + "," +
                            std::to_string((int)doc.cam_pitch_deg));
            else if (shell.hit3_fov.contains(event.position))
              edit3(22, std::to_string((int)doc.fov_deg));
            else if (shell.hit3_clip.contains(event.position))
              edit3(71, std::to_string(doc.near_plane) + "," +
                            std::to_string(doc.far_plane));
            else if (shell.hit3_lightdir.contains(event.position))
              edit3(23, std::to_string(doc.light_x) + "," +
                            std::to_string(doc.light_y) + "," +
                            std::to_string(doc.light_z));
            else if (shell.hit3_lightint.contains(event.position))
              edit3(24, std::to_string(doc.light_intensity));
            else if (shell.hit3_grav.contains(event.position))
              edit3(25, std::to_string(doc.gravity));
            else if (shell.hit3_ground.contains(event.position))
              edit3(26, std::to_string(doc.ground_y));
            else if (shell.hit3_bounds.contains(event.position))
              edit3(27, std::to_string(doc.bounds));
            else if (shell.hit3_bg.contains(event.position))
              edit3(28, std::to_string((int)doc.bg_r) + "," +
                            std::to_string((int)doc.bg_g) + "," +
                            std::to_string((int)doc.bg_b));
            else if (shell.hit3_music.contains(event.position))
              edit3(29, doc.music);
            else if (shell.hit3_filla_dir.contains(event.position))
              edit3(30,
                    doc.lights.empty()
                        ? ""
                        : std::to_string(doc.lights[0].dir_x) + "," +
                              std::to_string(doc.lights[0].dir_y) + "," +
                              std::to_string(doc.lights[0].dir_z));
            else if (shell.hit3_filla_tint.contains(event.position))
              edit3(31,
                    doc.lights.empty()
                        ? ""
                        : std::to_string(doc.lights[0].r) + "," +
                              std::to_string(doc.lights[0].g) + "," +
                              std::to_string(doc.lights[0].b) + "," +
                              std::to_string(doc.lights[0].intensity));
            else if (shell.hit3_fillb_dir.contains(event.position))
              edit3(32,
                    doc.lights.size() < 2
                        ? ""
                        : std::to_string(doc.lights[1].dir_x) + "," +
                              std::to_string(doc.lights[1].dir_y) + "," +
                              std::to_string(doc.lights[1].dir_z));
            else if (shell.hit3_fillb_tint.contains(event.position))
              edit3(33,
                    doc.lights.size() < 2
                        ? ""
                        : std::to_string(doc.lights[1].r) + "," +
                              std::to_string(doc.lights[1].g) + "," +
                              std::to_string(doc.lights[1].b) + "," +
                              std::to_string(doc.lights[1].intensity));
            else if (shell.hit3_pbr.contains(event.position) && se)
              edit3(40, std::to_string(se->metallic) + "," +
                            std::to_string(se->roughness));
            else if (shell.hit3_mr.contains(event.position) && se)
              edit3(41, se->metallic_roughness);
            else if (shell.hit3_emis.contains(event.position) && se)
              edit3(42, se->emissive);
            else if (shell.hit3_emit.contains(event.position) && se)
              edit3(43, std::to_string(se->emissive_strength) + "," +
                            std::to_string(se->emissive_r) + "," +
                            std::to_string(se->emissive_g) + "," +
                            std::to_string(se->emissive_b));
            else if (shell.hit3_night.contains(event.position) && se)
              edit3(44, std::to_string(se->night_emissive));
            else if (shell.hit3_env.contains(event.position) && se)
              edit3(45, se->environment);
            else if (shell.hit3_envstr.contains(event.position) && se)
              edit3(46, std::to_string(se->environment_strength));
            else if (shell.hit3_cutout.contains(event.position) && se)
              edit3(47, std::to_string(se->alpha_cutout));
            else if (shell.hit3_tile.contains(event.position) && se)
              edit3(48, std::to_string(se->uv_tile_x) + "," +
                            std::to_string(se->uv_tile_y));
            else if (shell.hit3_atmo.contains(event.position) && se)
              edit3(49, std::to_string(se->atmo_strength) + "," +
                            std::to_string(se->atmo_power) + "," +
                            std::to_string(se->atmo_night));
            else if (shell.hit3_atmotint.contains(event.position) && se)
              edit3(50, std::to_string(se->atmo_r) + "," +
                            std::to_string(se->atmo_g) + "," +
                            std::to_string(se->atmo_b));
            else if (shell.hit3_exposure.contains(event.position))
              edit3(34, std::to_string(doc.exposure));
            else if (shell.hit3_bloom.contains(event.position))
              edit3(35, std::to_string(doc.bloom) + "," +
                            std::to_string(doc.bloom_threshold));
            else if (shell.hit3_grade.contains(event.position))
              edit3(36, std::to_string(doc.contrast) + "," +
                            std::to_string(doc.saturation) + "," +
                            std::to_string(doc.sharpen));
            else if (shell.hit3_quality.contains(event.position))
              edit3(37, doc.quality);
            else if (shell.hit3_plights.contains(event.position))
              edit3(38, "");
            else if (shell.hit3_debug.contains(event.position))
              edit3(39, doc.debug_view);
            else if (shell.hit3_shadow.contains(event.position))
              edit3(52, doc.shadow_extent > 0.f
                            ? std::to_string(doc.shadow_extent) + "," +
                                  std::to_string(doc.shadow_distance) + "," +
                                  std::to_string(doc.shadow_depth)
                            : "");
            else if (shell.hit3_range.contains(event.position) && se)
              edit3(51, std::to_string(se->visible_range));
            else if (shell.hit3_visfade.contains(event.position) && se)
              edit3(67, std::to_string(se->visible_fade));
            else if (shell.hit3_surfmaps.contains(event.position) && se)
              edit3(53, se->normal_map + "," + se->properties_map + "," +
                            se->cloud_map);
            else if (shell.hit3_surfshape.contains(event.position) && se)
              edit3(54, std::to_string(se->normal_strength) + "," +
                            std::to_string(se->relief));
            else if (shell.hit3_clouddeck.contains(event.position) && se)
              edit3(55, std::to_string(se->cloud_opacity) + "," +
                            std::to_string(se->cloud_albedo) + "," +
                            std::to_string(se->cloud_offset_x) + "," +
                            std::to_string(se->cloud_offset_y) +
                            (se->cloud_height != 0.f
                                 ? "," + std::to_string(se->cloud_height)
                                 : ""));
            else if (shell.hit3_termwrap.contains(event.position) && se)
              edit3(56, std::to_string(se->terminator_wrap));
            else if (shell.hit3_limbdark.contains(event.position) && se)
              edit3(57, std::to_string(se->limb_darkening) + "," +
                            std::to_string(se->limb_darkening_q));
            else if (shell.hit3_lods.contains(event.position) && se) {
              std::string v;
              for (const auto &spec : se->lod_meshes) {
                if (!v.empty()) v += ",";
                v += spec;
              }
              edit3(58, v);
            }
            else if (shell.hit3_lodpixels.contains(event.position) && se)
              edit3(59, std::to_string(se->lod_pixels));
            else if (shell.hit3_lodfade.contains(event.position) && se)
              edit3(66, std::to_string(se->lod_fade));
            else if (shell.hit3_lodgroup.contains(event.position) && se)
              edit3(69, se->lod_group + ";" + se->lod_proxy + ";" +
                            std::to_string(se->lod_proxy_pixels));
            else if (shell.hit3_bandshear.contains(event.position) && se)
              edit3(60, std::to_string(se->band_shear));
            else if (shell.hit3_bandwaves.contains(event.position) && se)
              edit3(68, std::to_string(se->band_waves));
            else if (shell.hit3_banddrift.contains(event.position) && se)
              edit3(70, std::to_string(se->band_drift) + "," +
                            std::to_string(se->band_turbulence));
            else if (shell.hit3_orbitbeam.contains(event.position) && se)
              edit3(61, std::to_string(se->orbital_beaming));
            else if (shell.hit3_starkelvin.contains(event.position) && se)
              edit3(62, std::to_string(static_cast<long long>(se->star_kelvin)));
            else if (shell.hit3_accretion.contains(event.position) && se)
              edit3(63, std::to_string(se->accretion[0]) + "," +
                            std::to_string(se->accretion[1]) + "," +
                            std::to_string(se->accretion[2]) + "," +
                            std::to_string(se->accretion[3]));
            else if (shell.hit3_fwdscatter.contains(event.position) && se)
              edit3(64, std::to_string(se->forward_scatter));
            else if (shell.hit3_volume.contains(event.position) && se)
              edit3(65, std::to_string(se->volume_depth) + "," +
                            std::to_string(se->volume_density) + "," +
                            std::to_string(se->volume_seed) + "," +
                            std::to_string(se->volume_steps) + "," +
                            std::to_string(se->volume_scatter) + "," +
                            std::to_string(se->volume_flow) + "," +
                            std::to_string(se->volume_distort) + "," +
                            std::to_string(se->volume_blend) +
                            (se->volume_image2.empty() &&
                                     se->volume_occlude == 0.f &&
                                     se->volume_flow_rate == 0.f
                                 ? ""
                                 : "," + se->volume_image2 + "," +
                                       std::to_string(se->volume_occlude) +
                                       (se->volume_flow_rate == 0.f
                                            ? ""
                                            : "," + std::to_string(
                                                  se->volume_flow_rate))));
            else if (shell.scene3_rows.contains(event.position)) {
              const auto row = static_cast<std::size_t>(std::max(
                  0.f, std::floor((event.position.y -
                                   shell.scene3_rows.y +
                                   shell.scene3_list.scroll_offset) /
                                  shell.scene3_list.row_height)));
              if (row < doc.entities.size()) shell.scene3_sel = row;
            }
          } else if (shell.editing_project_name || shell.editing_import ||
                     shell.editing_package || shell.editing_scene ||
                     shell.editing_scene3) {
            shell.editing_project_name = shell.editing_import =
                shell.editing_package = shell.editing_scene =
                    shell.editing_scene3 = false;
            window.set_text_input(false);
          }
          break;
        }
        case InputEventType::Wheel:
          last_input = "wheel";
          if (shell.tool == Tool::Projects &&
              shell.project_rows.contains(event.position))
            shell.project_list.scroll_to(shell.project_list.scroll_offset -
                                         event.wheel_y * 40.f);
          if (shell.tool == Tool::Scene &&
              shell.scene_rows.contains(event.position))
            shell.entity_list.scroll_to(shell.entity_list.scroll_offset -
                                        event.wheel_y * 40.f);
          if (shell.tool == Tool::Scene3D) {
            if (shell.scene3_rows.contains(event.position))
              shell.scene3_list.scroll_to(
                  shell.scene3_list.scroll_offset -
                  event.wheel_y * 40.f);
            else if (shell.scene3_preview.contains(event.position)) {
              // Wheel tunes the authored fov inside the preview.
              auto &d = shell.scene3_doc;
              d.fov_deg =
                  std::clamp(d.fov_deg - event.wheel_y * 3.f, 10.f,
                             140.f);
              shell.scene3_modified = true;
            }
          }
          break;
        case InputEventType::KeyPressed:
          // Scene tool undo: Ctrl+Z / Ctrl+Y when no field is being edited.
          if (event.control && shell.tool == Tool::Scene &&
              !shell.editing_scene) {
            std::optional<engine::SceneDocument> restored;
            if (event.key == 'z')
              restored = shell.scene_history.undo(shell.scene_doc);
            else if (event.key == 'y')
              restored = shell.scene_history.redo(shell.scene_doc);
            if (restored) {
              shell.scene_doc = std::move(*restored);
              shell.selected_entity = static_cast<std::size_t>(-1);
              shell.scene_modified = true;
              shell.status = "scene history restored - SAVE to persist";
            }
          }
          if (event.control && shell.tool == Tool::Scene3D &&
              !shell.editing_scene3) {
            std::optional<engine::Scene3dDocument> restored;
            if (event.key == 'z')
              restored = shell.scene3_history.undo(shell.scene3_doc);
            else if (event.key == 'y')
              restored = shell.scene3_history.redo(shell.scene3_doc);
            if (restored) {
              shell.scene3_doc = std::move(*restored);
              shell.scene3_sel = static_cast<std::size_t>(-1);
              shell.scene3_modified = true;
              shell.status =
                  "scene3d history restored - SAVE to persist";
            }
          }
          last_input = "key";
          break;
        case InputEventType::KeyReleased: last_input = "key"; break;
        default: break;
        }
      }
      if (!snapshot.renderable()) continue;

      if (std::chrono::steady_clock::now() >= next_job) {
        next_job =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        (void)jobs.submit("engine-shell.demo", engine::JobPriority::Normal, {},
                          [&demo_jobs_done] { demo_jobs_done.fetch_add(1); });
      }

      const float w = static_cast<float>(snapshot.drawable_width);
      const float h = static_cast<float>(snapshot.drawable_height);
      const float s = std::clamp(h / 900.f, 0.8f, 2.0f);

      DrawList draw;
      draw.overlay.push_back(FilledRectangle{{0, 0, w, h}, {4, 10, 16, 255}});
      for (float gx = 0; gx < w; gx += 64.f)
        draw.overlay.push_back(Line{{gx, 0}, {gx, h}, {14, 26, 36, 255}});
      for (float gy = 0; gy < h; gy += 64.f)
        draw.overlay.push_back(Line{{0, gy}, {w, gy}, {14, 26, 36, 255}});

      const UiRect panel{20 * s, 20 * s, w - 40 * s, h - 40 * s};
      draw.overlay.push_back(FilledRectangle{panel, panel_fill});
      draw.overlay.push_back(StrokedRectangle{panel, panel_edge});

      // Header.
      draw.overlay.push_back(
          Text{{panel.x + 22 * s, panel.y + 16 * s}, "STELLAR ENGINE", ink,
               static_cast<int>(26 * s), 0, std::nullopt, TextAlign::Left,
               FontFace::Heading});
      draw.overlay.push_back(
          Text{{panel.x + 22 * s, panel.y + 50 * s},
               "standalone engine tools host - no game module linked", muted,
               static_cast<int>(13 * s)});
      if (emblem) {
        const float emblem_size = 56 * s;
        draw.overlay.push_back(
            Image{emblem,
                  {panel.x + panel.width - 20 * s - emblem_size,
                   panel.y + 12 * s, emblem_size, emblem_size}});
      }

      // Tool sidebar.
      const UiRect sidebar{panel.x + 16 * s, panel.y + 84 * s, 170 * s,
                           panel.height - 104 * s};
      draw.overlay.push_back(FilledRectangle{sidebar, {6, 16, 26, 255}});
      draw.overlay.push_back(StrokedRectangle{sidebar, panel_edge});
      shell.tool_hits.clear();
      float ty = sidebar.y + 10 * s;
      for (std::size_t i = 0; i < kTools.size(); ++i) {
        const UiRect item{sidebar.x + 6 * s, ty, sidebar.width - 12 * s,
                          34 * s};
        shell.tool_hits.push_back(item);
        if (kTools[i] == shell.tool)
          draw.overlay.push_back(FilledRectangle{item, row_selected});
        else if (item.contains(snapshot.pointer))
          draw.overlay.push_back(FilledRectangle{item, row_hover});
        draw.overlay.push_back(
            Text{{item.x + 12 * s, item.y + 9 * s}, kToolNames[i],
                 kTools[i] == shell.tool ? ink : muted,
                 static_cast<int>(14 * s)});
        ty += 40 * s;
      }

      const UiRect body{sidebar.x + sidebar.width + 12 * s, sidebar.y,
                        panel.x + panel.width - sidebar.x - sidebar.width -
                            28 * s,
                        sidebar.height};

      profiler.begin_frame();
      const auto frame_scope = profiler.span("engine-shell.frame", "frame");

      const auto stats = jobs.stats();
      switch (shell.tool) {
      case Tool::Projects:
        render_projects(draw, shell, body, s);
        break;
      case Tool::Dashboard:
        render_dashboard(draw, shell, body, s, stats, demo_jobs_done.load(),
                         locale.size(), locale_status, window,
                         snapshot, fps, last_input);
        break;
      case Tool::Scene:
        render_scene(draw, shell, body, s);
        break;
      case Tool::Scene3D:
        render_scene3(draw, shell, body, s);
        break;
      case Tool::Assets:
        render_assets(draw, shell, body, s);
        break;
      case Tool::Profiler:
        render_profiler(draw, shell, body, s, profiler);
        break;
      case Tool::Localization:
        render_localization(draw, shell, body, s, locale);
        break;
      case Tool::Simulation:
        render_simulation(draw, shell, body, s);
        break;
      case Tool::Colony:
        render_colony(draw, shell, body, s);
        break;
      case Tool::Economy:
        render_economy(draw, shell, body, s);
        break;
      case Tool::Planet:
        render_planet(draw, shell, body, s);
        break;
      case Tool::Ai:
        render_ai(draw, shell, body, s);
        break;
      case Tool::Warfare:
        render_warfare(draw, shell, body, s);
        break;
      case Tool::Missions:
        render_missions(draw, shell, body, s);
        break;
      case Tool::Physics:
        render_physics(draw, shell, body, s);
        break;
      case Tool::Galaxy:
        render_galaxy(draw, shell, body, s);
        break;
      }

      draw.overlay.push_back(Text{{body.x + 6 * s, panel.y + panel.height - 26 * s},
                               shell.status + "  |  ESC to quit - F12 screenshots", muted,
                               static_cast<int>(11 * s)});

      // Deferred input handling that needs this frame's list geometry.
      for (const auto &event : snapshot.events) {
        if (shell.tool == Tool::Assets) {
          if (event.type == InputEventType::LeftReleased &&
              shell.hit_cooked_toggle.contains(event.position)) {
            shell.show_cooked = !shell.show_cooked;
            shell.asset_list.scroll_offset = 0;
            shell.selected_asset = static_cast<std::size_t>(-1);
            shell.preview.reset();
            shell.preview_label.clear();
            if (shell.show_cooked) shell.cooked_dirty = true;
          }
          const std::size_t rows =
              shell.show_cooked ? shell.cooked_records.size()
                                : shell.asset_files.size();
          if (rows > 0) {
            const UiRect list_rect = tool_list_rect(body, s, 0.48f);
            if (event.type == InputEventType::Wheel &&
                list_rect.contains(event.position))
              shell.asset_list.scroll_to(shell.asset_list.scroll_offset -
                                         event.wheel_y * 44.f);
            if (event.type == InputEventType::LeftReleased &&
                list_rect.contains(event.position)) {
              const float local = event.position.y - list_rect.y +
                                  shell.asset_list.scroll_offset;
              const auto row = static_cast<std::size_t>(std::max(
                  0.f, std::floor(local / shell.asset_list.row_height)));
              if (row < rows) select_asset(shell, row);
            }
          }
        }
        if (shell.tool == Tool::Localization &&
            event.type == InputEventType::Wheel) {
          const UiRect list_rect = tool_list_rect(body, s, 1.0f);
          if (list_rect.contains(event.position))
            shell.key_list.scroll_to(shell.key_list.scroll_offset -
                                     event.wheel_y * 44.f);
        }
        if (shell.tool == Tool::Profiler &&
            event.type == InputEventType::LeftReleased) {
          const auto capture_slot = [&](int slot) {
            auto parsed =
                engine::ProfileCapture::parse(profiler.export_json());
            if (!parsed) {
              shell.prof_status = "capture failed";
              return;
            }
            (slot == 0 ? shell.prof_capture_a : shell.prof_capture_b) =
                std::move(*parsed);
            shell.prof_status = std::string("captured ") +
                                (slot == 0 ? "A" : "B");
          };
          const auto save_slot = [&](int slot) {
            const auto &capture = slot == 0 ? shell.prof_capture_a
                                            : shell.prof_capture_b;
            if (!capture) {
              shell.prof_status = "nothing captured";
              return;
            }
            const auto path = profiler_capture_path(slot);
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << capture->to_json();
            shell.prof_status = out ? "saved " + path.filename().string()
                                    : "save failed";
          };
          const auto load_slot = [&](int slot) {
            const auto path = profiler_capture_path(slot);
            std::ifstream in(path, std::ios::binary);
            if (!in) {
              shell.prof_status = "no saved capture";
              return;
            }
            const std::string text{std::istreambuf_iterator<char>(in),
                                   std::istreambuf_iterator<char>()};
            auto parsed = engine::ProfileCapture::parse(text);
            if (!parsed) {
              shell.prof_status = "capture file corrupt";
              return;
            }
            (slot == 0 ? shell.prof_capture_a : shell.prof_capture_b) =
                std::move(*parsed);
            shell.prof_status = std::string("loaded ") +
                                (slot == 0 ? "A" : "B");
          };
          if (shell.hit_prof_cap_a.contains(event.position))
            capture_slot(0);
          else if (shell.hit_prof_cap_b.contains(event.position))
            capture_slot(1);
          else if (shell.hit_prof_save_a.contains(event.position))
            save_slot(0);
          else if (shell.hit_prof_save_b.contains(event.position))
            save_slot(1);
          else if (shell.hit_prof_load_a.contains(event.position))
            load_slot(0);
          else if (shell.hit_prof_load_b.contains(event.position))
            load_slot(1);
        }
        if (shell.tool == Tool::Simulation &&
            event.type == InputEventType::LeftReleased) {
          if (shell.hit_sim_step.contains(event.position)) {
            shell.sim.last = shell.sim.executor.advance();
          } else if (shell.hit_sim_run.contains(event.position)) {
            shell.sim.running = !shell.sim.running;
            shell.sim.run_accum = 0.0;
          } else if (shell.hit_sim_wake.contains(event.position)) {
            shell.sim.executor.wake(30);
          } else if (shell.hit_sim_tier.contains(event.position)) {
            const auto key = kSimTasks[shell.sim.selected].key;
            const auto tier = shell.sim.executor.tier(key);
            using eng_tier = engine::SimulationTier;
            const eng_tier next =
                tier == eng_tier::Normal      ? eng_tier::Background
                : tier == eng_tier::Background ? eng_tier::Dormant
                : tier == eng_tier::Dormant    ? eng_tier::Active
                                               : eng_tier::Normal;
            shell.sim.executor.set_tier(key, next);
          } else if (shell.hit_sim_list.contains(event.position)) {
            const float local = event.position.y - shell.hit_sim_list.y -
                                6 * s;
            const auto row =
                static_cast<std::size_t>(std::max(0.f, local / (22 * s)));
            if (row < kSimTasks.size()) shell.sim.selected = row;
          }
        }
        if (shell.tool == Tool::Colony &&
            event.type == InputEventType::LeftReleased) {
          auto &col = shell.col;
          auto advance_days = [&col](double days) {
            engine::ColonyInputs inputs;
            inputs.workers_available = col.workers;
            inputs.stockpile = &col.stockpile;
            inputs.maintenance = col.maintenance;
            col.last = col.colony.advance(days, inputs);
            col.day += days;
            col.notice.clear();
          };
          if (shell.hit_col_step.contains(event.position)) {
            advance_days(1.0);
          } else if (shell.hit_col_run30.contains(event.position)) {
            advance_days(30.0);
          } else if (shell.hit_col_workers_dn.contains(event.position)) {
            col.workers = std::max(0.0, col.workers - 100.0);
          } else if (shell.hit_col_workers_up.contains(event.position)) {
            col.workers += 100.0;
          } else if (shell.hit_col_maint.contains(event.position)) {
            col.maintenance = col.maintenance > 0.5 ? 0.2 : 1.0;
          } else if (shell.hit_col_resupply.contains(event.position)) {
            col.stockpile.add("res.alloys", 200);
            col.stockpile.add("res.food", 50);
            col.stockpile.add("res.fuel", 100);
            col.stockpile.add("res.water", 100);
          } else if (shell.hit_col_enable.contains(event.position) &&
                     !col.rows.empty()) {
            const auto [is_district, id] = col.rows[col.selected];
            if (is_district) {
              const auto *district = col.colony.district(id);
              if (district)
                col.colony.set_district_enabled(id, !district->enabled);
            } else {
              const auto *structure = col.colony.structure(id);
              if (structure)
                col.colony.set_enabled(id, !structure->enabled);
            }
          } else if (shell.hit_col_demolish.contains(event.position) &&
                     !col.rows.empty()) {
            const auto [is_district, id] = col.rows[col.selected];
            const bool ok =
                is_district ? col.colony.demolish_district(id)
                            : col.colony.demolish_structure(id);
            col.notice =
                ok ? "" : "demolish failed — district still hosts structures";
          } else {
            for (std::size_t i = 0; i < shell.hit_col_build.size(); ++i) {
              if (!shell.hit_col_build[i].contains(event.position))
                continue;
              const auto &row = kColonyBuild[i];
              const auto &cost =
                  row.district
                      ? col.colony.district_spec(row.spec)->build_cost
                      : col.colony.structure_spec(row.spec)->build_cost;
              if (!pay_build_cost(col.stockpile, cost)) {
                col.notice = std::string("insufficient stockpile for ") +
                             row.label + " (" + cost_text(cost) + ")";
                break;
              }
              const std::uint64_t id = col.next_id;
              bool ok = false;
              if (row.district) {
                ok = col.colony.build_district(id, row.spec);
              } else {
                const auto *spec = col.colony.structure_spec(row.spec);
                if (spec->district.empty()) {
                  ok = col.colony.build_structure(id, row.spec);
                } else {
                  // Prefer the selected district if compatible and open,
                  // else first matching district with a free slot.
                  std::uint64_t target = 0;
                  auto compatible = [&col, &spec](std::uint64_t did) {
                    const auto *district = col.colony.district(did);
                    return district && district->spec_id == spec->district &&
                           district->construction_remaining <= 0.0 &&
                           district->enabled &&
                           col.colony.structures_in(did).size() <
                               col.colony.district_spec(spec->district)
                                   ->structure_slots;
                  };
                  if (!col.rows.empty() &&
                      col.rows[col.selected].first &&
                      compatible(col.rows[col.selected].second))
                    target = col.rows[col.selected].second;
                  if (target == 0)
                    for (const auto *district : col.colony.districts())
                      if (compatible(district->id)) {
                        target = district->id;
                        break;
                      }
                  if (target != 0)
                    ok = col.colony.build_structure(id, row.spec, target);
                  else
                    col.notice = std::string("no open ") + spec->district +
                                 " slot for " + row.label;
                }
              }
              if (ok) {
                ++col.next_id;
                col.notice.clear();
              } else if (col.notice.empty()) {
                col.notice = std::string("cannot build ") + row.label;
              }
              if (!ok) // refund — nothing was constructed
                for (const auto &[resource, amount] : cost)
                  col.stockpile.add(resource, amount);
              break;
            }
            for (std::size_t i = 0; i < shell.hit_col_rows.size(); ++i) {
              if (shell.hit_col_rows[i].contains(event.position)) {
                col.selected = i;
                break;
              }
            }
          }
        }
        if (shell.tool == Tool::Economy &&
            event.type == InputEventType::LeftReleased) {
          auto &eco = shell.eco;
          if (shell.hit_eco_validate.contains(event.position)) {
            engine::EconomyCatalog check = eco.catalog;
            if (eco.break_catalog)
              // Injects a recipe consuming a resource nothing defines —
              // exercises the validation diagnostics live.
              check.add_recipe(
                  {.id = "recipe.broken",
                   .name_key = "RECIPE_BROKEN",
                   .inputs = {{"res.nonexistent", 1}},
                   .outputs = {{"res.alloys", 1}},
                   .duration_days = 1.0});
            eco.issues = check.validate();
            eco.validated = true;
          } else if (shell.hit_eco_break.contains(event.position)) {
            eco.break_catalog = !eco.break_catalog;
            eco.validated = false;
          } else if (shell.hit_eco_analyze.contains(event.position)) {
            // Demand + observed rollups derived from the live network:
            // enabled producers' recipe throughput per day.
            std::unordered_map<std::string, engine::ResourceObservation>
                observed;
            for (const auto &p : eco.network.capture_state().producers) {
              if (!p.enabled) continue;
              const auto *spec = eco.catalog.recipe(p.recipe_id);
              if (!spec || spec->duration_days <= 0.0) continue;
              const double runs_per_day = 1.0 / spec->duration_days;
              for (const auto &a : spec->inputs)
                observed[a.resource].consumed_per_day +=
                    a.amount * runs_per_day;
              for (const auto &a : spec->outputs)
                observed[a.resource].produced_per_day +=
                    a.amount * runs_per_day;
              for (const auto &a : spec->outputs)
                observed[a.resource].capacity_per_day +=
                    a.amount * runs_per_day;
            }
            for (const std::uint64_t node_id : {1ull, 2ull}) {
              const auto *node = eco.network.node(node_id);
              if (!node) continue;
              for (const auto &[res, qty] : node->inventory.snapshot())
                observed[res].stock += qty;
            }
            std::vector<engine::ResourceAmount> demand;
            for (const auto &[res, obs] : observed)
              if (obs.consumed_per_day > 0.0)
                demand.push_back({res, obs.consumed_per_day});
            eco.diagnostics =
                engine::analyze_economy(eco.catalog, demand, observed);
            eco.analyzed = true;
          } else if (shell.hit_eco_step.contains(event.position)) {
            eco.network.advance(1.0);
            eco.day += 1.0;
          } else if (shell.hit_eco_run10.contains(event.position)) {
            eco.network.advance(10.0);
            eco.day += 10.0;
          } else if (shell.hit_eco_producer.contains(event.position)) {
            const auto state = eco.network.capture_state();
            const auto p =
                std::find_if(state.producers.begin(), state.producers.end(),
                             [&](const auto &prod) {
                               return prod.id == eco.smelter_producer;
                             });
            if (p != state.producers.end())
              eco.network.set_producer_enabled(eco.smelter_producer,
                                               !p->enabled);
          }
        }
        if (shell.tool == Tool::Planet &&
            event.type == InputEventType::LeftReleased) {
          auto &planet = shell.planet;
          auto advance_days = [&planet](double days) {
            planet.last = planet.terra.advance(days);
            planet.day += days;
          };
          if (shell.hit_plan_step.contains(event.position)) {
            advance_days(1.0);
          } else if (shell.hit_plan_run30.contains(event.position)) {
            advance_days(30.0);
          } else if (shell.hit_plan_profile.contains(event.position)) {
            planet.profile =
                (planet.profile + 1) %
                static_cast<int>(kPlanetProfiles.size());
          } else if (shell.hit_plan_project.contains(event.position)) {
            shell.planet_project = (shell.planet_project + 1) % 2;
          } else if (shell.hit_plan_start.contains(event.position)) {
            constexpr std::array<const char *, 2> kProjects{
                "terraform.warm", "terraform.atmosphere"};
            planet.terra.start(kProjects[shell.planet_project]);
          } else if (shell.hit_plan_cancel.contains(event.position)) {
            planet.terra.cancel();
          } else {
            for (std::size_t i = 0; i < Shell::kPlanParamCount; ++i) {
              int delta = 0;
              if (shell.hit_plan_param[i * 2].contains(event.position))
                delta = -1;
              else if (shell.hit_plan_param[i * 2 + 1].contains(
                           event.position))
                delta = 1;
              if (delta == 0) continue;
              auto &env = planet.terra.environment();
              switch (i) {
              case Shell::kPlanTemp:
                env.temperature_k =
                    std::max(0.0, env.temperature_k + delta * 10.0);
                break;
              case Shell::kPlanAtm:
                env.atmosphere_atm =
                    std::max(0.0, env.atmosphere_atm + delta * 0.1);
                break;
              case Shell::kPlanGrav:
                env.gravity_g =
                    std::max(0.0, env.gravity_g + delta * 0.1);
                break;
              case Shell::kPlanWater:
                env.water_fraction =
                    std::clamp(env.water_fraction + delta * 0.05, 0.0,
                               1.0);
                break;
              default: break;
              }
            }
          }
        }
        if (shell.tool == Tool::Ai &&
            event.type == InputEventType::LeftReleased) {
          auto &ai = shell.ai;
          auto tick_day = [&ai] {
            ai.minerals = std::max(0.0, ai.minerals + ai.mines * 2.0 -
                                            ai.fleets * 0.5);
            ai.threat += 1.0;
            ai.day += 1.0;
            ai.mind.decide("economy", ai.day, 0.0, 1.1);
            ai.mind.decide("military", ai.day, 0.0, 1.1);
          };
          if (shell.hit_ai_decide.contains(event.position)) {
            for (int i = 0; i < 5; ++i) tick_day();
          } else if (shell.hit_ai_run.contains(event.position)) {
            ai.running = !ai.running;
            ai.run_accum = 0.0;
          } else if (shell.hit_ai_threat_dn.contains(event.position)) {
            ai.threat = std::max(0.0, ai.threat - 10.0);
          } else if (shell.hit_ai_threat_up.contains(event.position)) {
            ai.threat += 10.0;
          } else if (shell.hit_ai_reset.contains(event.position)) {
            ai = Shell::AiDemo{};
            init_ai(ai);
          } else {
            for (std::size_t i = 0; i < shell.hit_ai_actions.size(); ++i) {
              if (shell.hit_ai_actions[i].contains(event.position)) {
                const auto *action = ai.mind.action(kAiActions[i]);
                if (action)
                  ai.mind.set_enabled(action->id, !action->enabled);
                break;
              }
            }
          }
        }
        if (shell.tool == Tool::Warfare &&
            event.type == InputEventType::LeftReleased) {
          auto &war = shell.war;
          auto &m = war.model;
          if (shell.hit_war_step.contains(event.position)) {
            m.advance(5.0);
            war.day += 5.0;
          } else if (shell.hit_war_run.contains(event.position)) {
            war.running = !war.running;
            war.run_accum = 0.0;
          } else if (shell.hit_war_order.contains(event.position)) {
            if (const auto *fleet = m.fleet(war.selected)) {
              const auto next = static_cast<engine::FleetOrderKind>(
                  (static_cast<int>(fleet->order.kind) + 1) % 4);
              engine::FleetOrder order{next, 0.0, 0.0};
              if (next == engine::FleetOrderKind::Move) {
                // Steer at the other side's fleet.
                const std::uint64_t target =
                    war.selected == 1 ? 2 : 1;
                if (const auto *foe = m.fleet(target)) {
                  order.target_x = foe->x;
                  order.target_y = foe->y;
                }
              } else if (next == engine::FleetOrderKind::Interdict) {
                order.target_x = fleet->x;
                order.target_y = fleet->y;
              }
              m.set_order(war.selected, order);
            }
          } else if (shell.hit_war_engage.contains(event.position)) {
            const auto result = m.resolve(1, 2, 5.0);
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "%.0fd: -%.1f vs -%.1f ships%s%s",
                          result.elapsed_days, result.a_ships_lost,
                          result.b_ships_lost,
                          result.a_destroyed ? " [A destroyed]" : "",
                          result.b_destroyed ? " [B destroyed]" : "");
            war.last_engagement = buf;
          } else if (shell.hit_war_reset.contains(event.position)) {
            war = Shell::WarfareDemo{};
            init_warfare(war);
          } else {
            const auto fleets = m.fleets();
            for (std::size_t i = 0; i < shell.hit_war_fleets.size() &&
                                   i < fleets.size();
                 ++i) {
              if (shell.hit_war_fleets[i].contains(event.position)) {
                war.selected = fleets[i]->id;
                break;
              }
            }
          }
        }
        if (shell.tool == Tool::Missions &&
            event.type == InputEventType::LeftReleased) {
          auto &mis = shell.missions;
          if (shell.hit_mis_fire.contains(event.position)) {
            fire_mission_event(mis);
          } else if (shell.hit_mis_step.contains(event.position)) {
            mis.runtime.advance(10.0);
            mis.day += 10.0;
          } else if (shell.hit_mis_run.contains(event.position)) {
            mis.running = !mis.running;
            mis.run_accum = 0.0;
          } else if (shell.hit_mis_choose.contains(event.position)) {
            if (const auto *def = [&]() -> const engine::MissionDefinition * {
                  for (const auto &inst : mis.runtime.instances())
                    if (inst.id == mis.selected)
                      return mis.runtime.definition(inst.mission_id);
                  return nullptr;
                }()) {
              for (const auto &inst : mis.runtime.instances()) {
                if (inst.id != mis.selected) continue;
                const auto stage = def->stages.find(inst.stage_id);
                if (stage != def->stages.end() &&
                    !stage->second.choices.empty())
                  mis.runtime.choose(inst.id, stage->second.choices.front().id);
                break;
              }
            }
          } else if (shell.hit_mis_save.contains(event.position)) {
            mis.saved_state = mis.runtime.serialize();
            mission_log(mis, "snapshot saved (" +
                        std::to_string(mis.saved_state.size()) + " bytes)");
          } else if (shell.hit_mis_load.contains(event.position)) {
            std::string error;
            if (!mis.saved_state.empty() &&
                mis.runtime.restore(mis.saved_state, &error)) {
              mission_log(mis, "snapshot restored");
            } else if (!error.empty()) {
              mission_log(mis, "restore failed: " + error);
            }
          } else if (shell.hit_mis_reset.contains(event.position)) {
            init_missions(mis);
          } else {
            const auto instances = mis.runtime.instances();
            for (std::size_t i = 0;
                 i < shell.hit_mis_instances.size() && i < instances.size();
                 ++i) {
              if (shell.hit_mis_instances[i].contains(event.position)) {
                mis.selected = instances[i].id;
                break;
              }
            }
          }
        }
        if (shell.tool == Tool::Physics &&
            event.type == InputEventType::LeftReleased) {
          auto &phys = shell.phys;
          auto &world = phys.world;
          if (shell.hit_phys_step.contains(event.position)) {
            phys_step(phys, 0.5);
          } else if (shell.hit_phys_run.contains(event.position)) {
            phys.running = !phys.running;
            phys.run_accum = 0.0;
          } else if (shell.hit_phys_ray.contains(event.position)) {
            if (const auto *from = world.body(phys.selected)) {
              if (const auto *to = world.body(phys.target)) {
                const float dx = to->x - from->x;
                const float dy = to->y - from->y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (const auto hit =
                        world.raycast(from->x, from->y, dx, dy, dist)) {
                  char buf[160];
                  std::snprintf(buf, sizeof(buf),
                                "ray -> body %llu at %.0f (%.0f,%.0f)",
                                static_cast<unsigned long long>(hit->body),
                                hit->distance, hit->x, hit->y);
                  phys.last_query = buf;
                } else {
                  phys.last_query = "ray -> no hit";
                }
              }
            }
          } else if (shell.hit_phys_sweep.contains(event.position)) {
            if (const auto *from = world.body(phys.selected)) {
              if (const auto *to = world.body(phys.target)) {
                const float dx = to->x - from->x;
                const float dy = to->y - from->y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (const auto hit =
                        world.sweep_circle(from->x, from->y, 12.0f, dx, dy,
                                       dist)) {
                  char buf[160];
                  std::snprintf(buf, sizeof(buf),
                                "sweep r12 -> body %llu at %.0f",
                                static_cast<unsigned long long>(hit->body),
                                hit->distance);
                  phys.last_query = buf;
                } else {
                  phys.last_query = "sweep -> no hit";
                }
              }
            }
          } else if (shell.hit_phys_reset.contains(event.position)) {
            phys = Shell::PhysicsDemo{};
            init_physics(phys);
          } else {
            auto ids = world.overlap_aabb(-1000.0f, -1000.0f, 2000.0f,
                                          2000.0f);
            std::sort(ids.begin(), ids.end());
            for (std::size_t i = 0;
                 i < shell.hit_phys_bodies.size() && i < ids.size(); ++i) {
              if (shell.hit_phys_bodies[i].contains(event.position)) {
                phys.selected = ids[i];
                break;
              }
            }
          }
        }
        if (shell.tool == Tool::Galaxy) {
          auto &gal = shell.gal;
          if (event.type == InputEventType::Wheel &&
              shell.hit_gal_map.contains(event.position)) {
            gal.zoom =
                std::clamp(gal.zoom + event.wheel_y * 0.12f, 0.3f, 6.0f);
          }
          if (event.type == InputEventType::LeftReleased) {
            if (shell.hit_gal_step.contains(event.position)) {
              gal_step(gal, 1.0);
            } else if (shell.hit_gal_run.contains(event.position)) {
              gal.running = !gal.running;
              gal.run_accum = 0.0;
            } else if (shell.hit_gal_reset.contains(event.position)) {
              gal = Shell::GalaxyDemo{};
              init_galaxy(gal);
            } else if (shell.hit_gal_map.contains(event.position)) {
              if (const auto world =
                      gal_unproject(gal.map, shell.hit_gal_map,
                                    event.position, gal.zoom)) {
                if (const auto hit =
                        gal.map.nearest_system(world->first, world->second))
                  gal.selected = *hit;
              }
            }
          }
          if (event.type == InputEventType::RightReleased &&
              shell.hit_gal_map.contains(event.position)) {
            if (const auto world =
                    gal_unproject(gal.map, shell.hit_gal_map,
                                  event.position, gal.zoom)) {
              if (const auto hit =
                      gal.map.nearest_system(world->first, world->second))
                gal.destination = *hit;
            }
          }
        }
      }

      FrameTiming timing;
      window.draw(draw, std::nullopt, &timing);
      ++frames_rendered;
      profiler.set_gauge("frame.submission_ms", timing.submission_ms);
      profiler.set_gauge("frame.present_ms", timing.present_ms);
      profiler.set_gauge("frame.fps", fps);
      (void)profiler.end_frame();

      const auto now = std::chrono::steady_clock::now();
      const auto elapsed =
          std::chrono::duration<double>(now - last_frame).count();
      last_frame = now;
      if (elapsed > 0) fps = fps * 0.9 + (1.0 / elapsed) * 0.1;
      if (shell.tool == Tool::Simulation && shell.sim.running) {
        shell.sim.run_accum += elapsed;
        if (shell.sim.run_accum >= 0.5) {
          shell.sim.run_accum = 0.0;
          shell.sim.last = shell.sim.executor.advance();
        }
      }
      if (shell.tool == Tool::Warfare && shell.war.running) {
        shell.war.run_accum += elapsed;
        if (shell.war.run_accum >= 0.5) {
          shell.war.run_accum = 0.0;
          shell.war.model.advance(5.0);
          shell.war.day += 5.0;
        }
      }
      if (shell.tool == Tool::Physics && shell.phys.running) {
        shell.phys.run_accum += elapsed;
        if (shell.phys.run_accum >= 0.25) {
          shell.phys.run_accum = 0.0;
          phys_step(shell.phys, 0.25);
        }
      }
      if (shell.tool == Tool::Galaxy && shell.gal.running) {
        shell.gal.run_accum += elapsed;
        if (shell.gal.run_accum >= 0.4) {
          shell.gal.run_accum = 0.0;
          gal_step(shell.gal, 1.0);
        }
      }
      if (shell.tool == Tool::Missions && shell.missions.running) {
        shell.missions.run_accum += elapsed;
        if (shell.missions.run_accum >= 0.5) {
          shell.missions.run_accum = 0.0;
          shell.missions.runtime.advance(10.0);
          shell.missions.day += 10.0;
        }
      }
      if (shell.tool == Tool::Ai && shell.ai.running) {
        shell.ai.run_accum += elapsed;
        if (shell.ai.run_accum >= 0.25) {
          shell.ai.run_accum = 0.0;
          auto &ai = shell.ai;
          ai.minerals = std::max(
              0.0, ai.minerals + ai.mines * 2.0 - ai.fleets * 0.5);
          ai.threat += 1.0;
          ai.day += 1.0;
          ai.mind.decide("economy", ai.day, 0.0, 1.1);
          ai.mind.decide("military", ai.day, 0.0, 1.1);
        }
      }
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Stellar Engine " STELLAR_ENGINE_VERSION " error: "
              << error.what() << '\n';
    return 1;
  }
}
