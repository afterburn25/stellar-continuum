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
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
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
  bool editing_project_name{}, editing_import{}, editing_package{};
  std::string project_name_buffer, import_buffer, package_buffer;
  UiRect hit_project_name{}, hit_project_create{}, hit_project_open{},
      hit_project_close{}, hit_project_cook{}, hit_project_build{},
      hit_project_run{}, hit_project_editor{}, hit_project_package{},
      hit_import_field{}, hit_import_button{}, hit_package_field{},
      hit_package_button{};
  UiRect project_rows{};
  // Content cooking, host builds and packaging run on the JobSystem; the
  // UI thread reads their status under the mutex.
  std::atomic<bool> cooking{}, building{}, packaging{};
  std::atomic<std::size_t> cook_done{}, cook_total{};
  std::mutex project_mutex;
  std::string cook_status, build_status, package_status;
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

void close_project(Shell &shell) {
  shell.project.reset();
  scan_assets(shell, find_path("assets"));
  {
    std::lock_guard lock(shell.project_mutex);
    shell.cook_status.clear();
    shell.build_status.clear();
    shell.package_status.clear();
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
                    });
}
#else
void start_cook(Shell &shell, engine::JobSystem &) {
  shell.status = "cook unavailable on this platform";
}
#endif

#if defined(_WIN32)
// Configures and builds the open project's host executable (src/main.cpp via
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
                      const auto sdk = find_path("engine-sdk");
                      const auto host = root / "build" / "host";
                      const auto log = (host / "build.log").string();
                      std::filesystem::create_directories(host);
                      const std::string cmake = "\"" STELLAR_CMAKE_COMMAND "\"";
                      auto run = [&](const std::string &command) {
                        return std::system((command + " >\"" + log + "\" 2>&1")
                                               .c_str());
                      };
                      std::string message;
                      if (run(cmake + " -S \"" + root.string() + "\" -B \"" +
                              host.string() + "\" -DCMAKE_BUILD_TYPE=Release "
                              "-DSTELLAR_ENGINE_SDK=\"" +
                              sdk.generic_string() + "\"") != 0)
                        message = "configure failed - see build/host/build.log";
                      else if (run(cmake + " --build \"" + host.string() +
                                   "\" --config Release") != 0)
                        message = "build failed - see build/host/build.log";
                      else
                        message = "build ok - " + exe_name + ".exe ready";
                      {
                        std::lock_guard lock(shell.project_mutex);
                        shell.build_status = std::move(message);
                      }
                      shell.building = false;
                    });
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
      ShellExecuteA(nullptr, "open", exe.string().c_str(), nullptr,
                    shell.project->root.string().c_str(), SW_SHOW);
      shell.status = "launched " + exe_name;
      return;
    }
  }
  shell.status = "no built host - run BUILD first";
}

// Assembles a distributable folder: the built host plus its runtime files,
// the cooked Content/ tree, and the source packages the host scans at
// startup — everything a player needs in dist/<name>/.
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
                        std::filesystem::path exe;
                        for (const auto dir :
                             {root / "build" / "host" / "Release",
                              root / "build" / "host"})
                          if (std::filesystem::is_regular_file(
                                  dir / (exe_name + ".exe"))) {
                            exe = dir / (exe_name + ".exe");
                            break;
                          }
                        if (exe.empty())
                          throw std::runtime_error(
                              "no built host - run BUILD first");
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
                               std::filesystem::recursive_directory_iterator(
                                   src)) {
                            if (!entry.is_regular_file()) continue;
                            const auto target =
                                dst /
                                std::filesystem::relative(entry.path(), src);
                            std::filesystem::create_directories(
                                target.parent_path(), ec);
                            std::filesystem::copy_file(
                                entry.path(), target,
                                std::filesystem::copy_options::
                                    overwrite_existing,
                                ec);
                            if (ec)
                              throw std::runtime_error("copy failed: " +
                                                       ec.message());
                            ++files;
                            bytes += entry.file_size();
                          }
                        };
                        copy_tree(exe.parent_path(), dist);
                        // The cooker writes <output>/Content/; copying the
                        // cooked root yields Content/ beside the exe, which
                        // is where the starter probes in packaged layout.
                        copy_tree(root / "build" / "cooked", dist);
                        copy_tree(root / "packages", dist / "packages");
                        message = "packaged " + std::to_string(files) +
                                  " files, " + human_bytes(bytes) +
                                  " - dist/" + exe_name;
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
void open_editor(Shell &shell) {
  shell.status = "editor launch unavailable on this platform";
}
void start_package(Shell &shell, engine::JobSystem &) {
  shell.status = "packaging unavailable on this platform";
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
    {
      std::lock_guard lock(shell.project_mutex);
      if (!shell.cook_status.empty()) {
        std::string status = shell.cook_status;
        if (shell.cooking && shell.cook_total > 0)
          status += "  " + std::to_string(shell.cook_done.load()) + "/" +
                    std::to_string(shell.cook_total.load());
        line(out, x, y, "cook", status, font);
      }
      if (!shell.build_status.empty())
        line(out, x, y, "build", shell.build_status, font);
      if (!shell.package_status.empty())
        line(out, x, y, "package", shell.package_status, font);
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
    shell_button(out, shell.hit_project_run, "RUN", false, font, s);
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
    y = y2;
  } else {
    shell.hit_project_close = {};
    shell.hit_project_cook = {};
    shell.hit_project_build = {};
    shell.hit_project_run = {};
    shell.hit_project_editor = {};
    shell.hit_project_package = {};
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
          if (shell.editing_project_name || shell.editing_import ||
              shell.editing_package) {
            shell.editing_project_name = shell.editing_import =
                shell.editing_package = false;
            window.set_text_input(false);
            continue;
          }
          return 0;
        }
        // Text-field editing takes precedence over tool clicks.
        if (shell.editing_project_name || shell.editing_import ||
            shell.editing_package) {
          std::string &buffer =
              shell.editing_import      ? shell.import_buffer
              : shell.editing_package   ? shell.package_buffer
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
            shell.editing_project_name = shell.editing_import =
                shell.editing_package = false;
            window.set_text_input(false);
            if (was_import) import_asset(shell);
            else if (was_package) create_package(shell);
            else create_project_from_field(shell);
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
            else if (shell.hit_project_run.contains(event.position))
              run_project(shell);
            else if (shell.hit_project_editor.contains(event.position))
              open_editor(shell);
            else if (shell.hit_project_package.contains(event.position))
              start_package(shell, jobs);
            else if (shell.project_rows.contains(event.position)) {
              const auto row = static_cast<std::size_t>(std::max(
                  0.f, std::floor((event.position.y - shell.project_rows.y +
                                   shell.project_list.scroll_offset) /
                                  shell.project_list.row_height)));
              if (row < shell.projects.size()) shell.selected_project = row;
            }
          } else if (shell.editing_project_name || shell.editing_import ||
                     shell.editing_package) {
            shell.editing_project_name = shell.editing_import =
                shell.editing_package = false;
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
