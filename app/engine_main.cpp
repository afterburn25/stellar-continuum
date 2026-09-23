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

#include <stellar/engine/foundation.hpp>
#include <stellar/engine/localization.hpp>
#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/package.hpp>
#include <stellar/engine/profiler.hpp>
#include <stellar/engine/project.hpp>
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
#include <iostream>
#include <string>
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

enum class Tool { Projects, Dashboard, Assets, Profiler, Localization };
constexpr std::array kTools{Tool::Projects, Tool::Dashboard, Tool::Assets,
                            Tool::Profiler, Tool::Localization};
constexpr std::array<const char *, 5> kToolNames{"Projects", "Dashboard",
                                                 "Assets", "Profiler",
                                                 "Localization"};

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
  engine::PackageLoadPlan load_plan;
  std::vector<std::string> package_errors;
  // New-project name field and panel hit regions.
  bool editing_project_name{};
  std::string project_name_buffer;
  UiRect hit_project_name{}, hit_project_create{}, hit_project_open{},
      hit_project_close{};
  UiRect project_rows{};
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
  shell.asset_list.row_count = shell.asset_files.size();
  shell.asset_list.row_height = 22.f;
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
  shell.project_list.row_count = shell.projects.size();
  shell.project_list.row_height = 24.f;
  if (shell.selected_project >= shell.projects.size())
    shell.selected_project = shell.projects.empty()
                                 ? static_cast<std::size_t>(-1)
                                 : shell.projects.size() - 1;
}

void open_project(Shell &shell, const std::filesystem::path &root) {
  std::string error;
  auto loaded = engine::EngineProject::load(root, &error);
  if (!loaded) {
    shell.status = "open failed: " + error;
    return;
  }
  shell.project = std::move(*loaded);
  shell.package_registry = engine::PackageRegistry{};
  shell.package_registry.protect_namespace(shell.project->id);
  shell.package_errors.clear();
  std::size_t count = 0;
  for (const auto &dir : shell.project->content_dirs)
    count += engine::scan_packages(shell.package_registry,
                                   (root / dir).string(),
                                   &shell.package_errors);
  shell.load_plan = shell.package_registry.resolve();
  scan_assets(shell, root / shell.project->content_dirs.front());
  shell.status = "opened " + shell.project->name + " - " +
                 std::to_string(count) + " package(s), load plan " +
                 (shell.load_plan.ok ? "ok" : "FAILED");
}

void close_project(Shell &shell) {
  shell.project.reset();
  scan_assets(shell, find_path("assets"));
  shell.status = "project closed - browsing host assets";
}

void create_project_from_field(Shell &shell) {
  std::string error;
  const std::string name =
      shell.project_name_buffer.empty() ? "untitled" : shell.project_name_buffer;
  const auto id = engine::sanitize_project_id(name);
  const auto dir = shell.projects_root / id.substr(5);
  if (engine::create_project(dir, name, STELLAR_ENGINE_VERSION, &error)) {
    shell.status = "created " + dir.filename().string();
    shell.project_name_buffer.clear();
    refresh_projects(shell);
  } else {
    shell.status = "create failed: " + error;
  }
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
  line(out, x, y, "files", std::to_string(shell.asset_files.size()), font);
  y += 6 * s;

  const UiRect list_rect = tool_list_rect(body, s, 0.48f);
  out.overlay.push_back(FilledRectangle{list_rect, {6, 16, 26, 255}});
  out.overlay.push_back(StrokedRectangle{list_rect, panel_edge});

  shell.asset_list.viewport_height = list_rect.height;
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
    out.overlay.push_back(Text{{row.x + 8 * s, row.y + 4 * s},
                            shell.asset_files[i].generic_string(), ink, font, 0,
                            list_rect});
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

void render_profiler(DrawList &out, UiRect body, float s,
                     engine::Profiler &profiler) {
  float x = body.x + 22 * s;
  float y = body.y + 18 * s;
  const int font = static_cast<int>(13 * s);
  heading(out, x, y, "PROFILER");
  for (const auto &row : profiler.overlay_lines(18)) {
    out.overlay.push_back(Text{{x, y}, row, ink, font});
    y += font + 8.f;
  }
  const auto aggregates = profiler.aggregates();
  heading(out, x, y, "AGGREGATES");
  const auto count = std::min<std::size_t>(aggregates.size(), 14);
  for (std::size_t i = 0; i < count; ++i) {
    const auto &a = aggregates[i];
    line(out, x, y,
         a.category.empty() ? a.name : a.category + "/" + a.name,
         std::to_string(a.calls) + " calls, " +
             ms(static_cast<double>(a.total_nanoseconds) / 1e6) + " total",
         font);
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
  shell.key_list.row_count = shell.sample_keys.size();
  shell.key_list.row_height = 22.f;
  shell.key_list.viewport_height = list_rect.height;
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

void render_projects(DrawList &out, Shell &shell, UiRect body, float s) {
  float x = body.x + 22 * s;
  float y = body.y + 18 * s;
  const int font = static_cast<int>(13 * s);

  heading(out, x, y, "GAME PROJECTS");
  line(out, x, y, "projects root", shell.projects_root.string(), font);
  if (shell.project) {
    line(out, x, y, "open project",
         shell.project->name + "  (" + shell.project->id + ")", font);
    line(out, x, y, "path", shell.project->root.generic_string(), font);
    line(out, x, y, "engine", shell.project->engine_version, font);
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
  } else {
    shell.hit_project_close = {};
  }
  y += shell.hit_project_name.height + 14 * s;

  // Discovered projects under the root.
  shell.project_rows = {x, y, body.width * .5f - 22 * s,
                        body.y + body.height - y - 16 * s};
  out.overlay.push_back(FilledRectangle{shell.project_rows, {6, 16, 26, 255}});
  out.overlay.push_back(StrokedRectangle{shell.project_rows, panel_edge});
  shell.project_list.viewport_height = shell.project_rows.height;
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
}

void select_asset(Shell &shell, std::size_t index) {
  shell.selected_asset = index;
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
    shell.projects_root = find_path("projects");
    std::filesystem::create_directories(shell.projects_root);
    refresh_projects(shell);
    scan_assets(shell, find_path("assets"));
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

    for (;;) {
      const auto snapshot = window.poll();
      if (snapshot.quit_requested) break;
      shell.pointer_x = snapshot.pointer.x;
      shell.pointer_y = snapshot.pointer.y;
      for (const auto &event : snapshot.events) {
        if (event.type == InputEventType::EscapePressed) {
          if (shell.editing_project_name) {
            shell.editing_project_name = false;
            window.set_text_input(false);
            continue;
          }
          return 0;
        }
        // New-project name field editing takes precedence in the tool.
        if (shell.editing_project_name) {
          if (event.type == InputEventType::TextEntered) {
            if (shell.project_name_buffer.size() < 80)
              shell.project_name_buffer += event.text;
            continue;
          }
          if (event.type == InputEventType::BackspacePressed) {
            if (!shell.project_name_buffer.empty())
              shell.project_name_buffer.pop_back();
            continue;
          }
          if (event.type == InputEventType::KeyPressed &&
              event.key == '\r') {
            shell.editing_project_name = false;
            window.set_text_input(false);
            create_project_from_field(shell);
            continue;
          }
        }
        switch (event.type) {
        case InputEventType::PointerMove: last_input = "pointer move"; break;
        case InputEventType::LeftPressed: last_input = "left press"; break;
        case InputEventType::LeftReleased: {
          last_input = "left release";
          for (std::size_t i = 0; i < shell.tool_hits.size(); ++i)
            if (shell.tool_hits[i].contains(event.position))
              shell.tool = kTools[i];
          if (shell.tool == Tool::Projects) {
            if (shell.hit_project_name.contains(event.position)) {
              shell.editing_project_name = true;
              window.set_text_input(true);
            } else if (shell.editing_project_name) {
              shell.editing_project_name = false;
              window.set_text_input(false);
            }
            if (shell.hit_project_create.contains(event.position))
              create_project_from_field(shell);
            else if (shell.hit_project_open.contains(event.position) &&
                     shell.selected_project < shell.projects.size())
              open_project(shell, shell.projects[shell.selected_project]);
            else if (shell.hit_project_close.contains(event.position))
              close_project(shell);
            else if (shell.project_rows.contains(event.position)) {
              const auto row = static_cast<std::size_t>(std::max(
                  0.f, std::floor((event.position.y - shell.project_rows.y +
                                   shell.project_list.scroll_offset) /
                                  shell.project_list.row_height)));
              if (row < shell.projects.size()) shell.selected_project = row;
            }
          } else if (shell.editing_project_name) {
            shell.editing_project_name = false;
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
          break;
        case InputEventType::KeyPressed:
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
      case Tool::Assets:
        render_assets(draw, shell, body, s);
        break;
      case Tool::Profiler:
        render_profiler(draw, body, s, profiler);
        break;
      case Tool::Localization:
        render_localization(draw, shell, body, s, locale);
        break;
      }

      draw.overlay.push_back(Text{{body.x + 6 * s, panel.y + panel.height - 26 * s},
                               shell.status + "  |  ESC to quit - F12 screenshots", muted,
                               static_cast<int>(11 * s)});

      // Deferred input handling that needs this frame's list geometry.
      for (const auto &event : snapshot.events) {
        if (shell.tool == Tool::Assets && !shell.asset_files.empty()) {
          const UiRect list_rect = tool_list_rect(body, s, 0.48f);
          if (event.type == InputEventType::Wheel &&
              list_rect.contains(event.position))
            shell.asset_list.scroll_to(shell.asset_list.scroll_offset -
                                       event.wheel_y * 44.f);
          if (event.type == InputEventType::LeftReleased &&
              list_rect.contains(event.position)) {
            const float local = event.position.y - list_rect.y +
                                shell.asset_list.scroll_offset;
            const auto row = static_cast<std::size_t>(
                std::max(0.f, std::floor(local / shell.asset_list.row_height)));
            if (row < shell.asset_files.size()) select_asset(shell, row);
          }
        }
        if (shell.tool == Tool::Localization &&
            event.type == InputEventType::Wheel) {
          const UiRect list_rect = tool_list_rect(body, s, 1.0f);
          if (list_rect.contains(event.position))
            shell.key_list.scroll_to(shell.key_list.scroll_offset -
                                     event.wheel_y * 44.f);
        }
      }

      FrameTiming timing;
      window.draw(draw, std::nullopt, &timing);
      profiler.set_gauge("frame.submission_ms", timing.submission_ms);
      profiler.set_gauge("frame.present_ms", timing.present_ms);
      profiler.set_gauge("frame.fps", fps);
      (void)profiler.end_frame();

      const auto now = std::chrono::steady_clock::now();
      const auto elapsed =
          std::chrono::duration<double>(now - last_frame).count();
      last_frame = now;
      if (elapsed > 0) fps = fps * 0.9 + (1.0 / elapsed) * 0.1;
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Stellar Engine " STELLAR_ENGINE_VERSION " error: "
              << error.what() << '\n';
    return 1;
  }
}
