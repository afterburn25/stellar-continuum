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
#include <stellar/engine/foundation.hpp>
#include <stellar/engine/scene_document.hpp>
#include <stellar/engine/localization.hpp>
#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/package.hpp>
#include <stellar/engine/profiler.hpp>
#include <stellar/engine/project.hpp>
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
#include <string>
#include <unordered_map>
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

enum class Tool { Projects, Dashboard, Scene, Assets, Profiler, Localization };
constexpr std::array kTools{Tool::Projects, Tool::Dashboard, Tool::Scene,
                            Tool::Assets, Tool::Profiler, Tool::Localization};
constexpr std::array<const char *, 6> kToolNames{"Projects", "Dashboard",
                                                 "Scene", "Assets", "Profiler",
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
  UiRect hit_scene_undo{}, hit_scene_redo{};
  UiRect hit_scene_add{}, hit_scene_del{}, hit_scene_save{},
      hit_scene_name{}, hit_scene_pos{}, hit_scene_vel{}, hit_scene_sprite{},
      hit_scene_size{}, hit_scene_color{}, scene_preview{}, scene_rows{};
  // Decoded scene sprites keyed by resolved content path; cleared on
  // document reload so re-imported art refreshes.
  std::unordered_map<std::string, std::shared_ptr<const RgbaImage>>
      scene_sprites;
  bool scene_dragging{}; // pointer is dragging an entity in the preview
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
  shell.cooked_dirty = true;
  shell.scene_dirty = true;
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

// Smoke-tests the built host: launches it hidden with `--frames N`, waits up
// to 30s, and reports pass/fail. Shared by the UI TEST job and the headless
// --test path.
std::string test_project_sync(const std::filesystem::path &root,
                              const std::string &exe_name, int frames) {
  for (const auto dir : {root / "build" / "host" / "Release",
                         root / "build" / "host"}) {
    const auto exe = dir / (exe_name + ".exe");
    if (std::filesystem::is_regular_file(exe)) {
      const std::string frames_arg = "--frames " + std::to_string(frames);
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

void render_scene(DrawList &out, Shell &shell, UiRect body, float s) {
  float x = body.x + 22 * s;
  float y = body.y + 18 * s;
  const int font = static_cast<int>(13 * s);
  heading(out, x, y, "SCENE AUTHORING");
  if (!shell.project) {
    line(out, x, y, "open project", "none - open one in Projects", font);
    shell.hit_scene_add = shell.hit_scene_del = shell.hit_scene_save =
        shell.hit_scene_undo = shell.hit_scene_redo = {};
    shell.hit_scene_name = shell.hit_scene_pos = shell.hit_scene_vel =
        shell.hit_scene_sprite = shell.hit_scene_size =
            shell.hit_scene_color = {};
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
  y += bh + 14 * s;

  // Entity list (left) + scene preview (right).
  const UiRect list_rect{x, y, body.width * 0.34f, body.height * 0.5f};
  out.overlay.push_back(FilledRectangle{list_rect, {6, 16, 26, 255}});
  out.overlay.push_back(StrokedRectangle{list_rect, panel_edge});
  shell.scene_rows = list_rect;
  shell.entity_list.viewport_height = list_rect.height;
  shell.entity_list.row_height = 22 * s;
  shell.entity_list.row_count = shell.scene_doc.entities.size();
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
  shell.scene_preview = {px, y, pw, std::min(ph, body.height * 0.55f)};
  const auto &pv = shell.scene_preview;
  out.overlay.push_back(FilledRectangle{pv, {8, 16, 26, 255}});
  out.overlay.push_back(StrokedRectangle{pv, panel_edge});
  const float sx = pv.width / 1280.f, sy = pv.height / 720.f;
  for (std::size_t i = 0; i < shell.scene_doc.entities.size(); ++i) {
    const auto &e = shell.scene_doc.entities[i];
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
    if (sprite)
      out.overlay.push_back(Image{sprite, rect});
    else
      out.overlay.push_back(FilledRectangle{rect, {e.r, e.g, e.b, 200}});
    if (i == shell.selected_entity)
      out.overlay.push_back(StrokedRectangle{rect, accent});
  }
  out.overlay.push_back(Text{{pv.x + 6 * s, pv.y + pv.height - 16 * s},
                             "click selects - drag moves entities", muted,
                             static_cast<int>(11 * s), 0, pv});

  // Property fields for the selected entity.
  const auto *entity = selected_scene_entity(shell);
  float fy = pv.y + pv.height + 14 * s;
  const float fw = pv.width;
  auto field = [&](UiRect &hit, const char *label, const std::string &value,
                   bool editing, const char *hint) {
    out.overlay.push_back(Text{{px, fy}, label, muted, font});
    hit = {px + 90 * s, fy - 4 * s, fw - 90 * s, (font + 12) * s};
    field_box(out, hit, editing ? shell.scene_buffer : value, editing, hint,
              font, s);
    fy += hit.height + 8 * s;
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
  shell.asset_list.row_count = row_count;

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
    shell.projects_root = find_path("projects");
    std::filesystem::create_directories(shell.projects_root);
    refresh_projects(shell);
    scan_assets(shell, find_path("assets"));
    // Headless pipeline ops run without creating the window.
    if (argc > 1) {
      std::vector<std::string> args;
      for (int i = 1; i < argc; ++i)
#ifdef _WIN32
        args.push_back(std::filesystem::path(argv[i]).generic_string());
#else
        args.emplace_back(argv[i]);
#endif
      if (args.front() == "--create" || args.front() == "--cook" ||
          args.front() == "--build" || args.front() == "--package" ||
          args.front() == "--run" || args.front() == "--test")
        return run_headless(shell, args);
    }
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
            shell.editing_package || shell.editing_scene) {
          std::string &buffer =
              shell.editing_import      ? shell.import_buffer
              : shell.editing_package   ? shell.package_buffer
              : shell.editing_scene     ? shell.scene_buffer
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
            shell.editing_project_name = shell.editing_import =
                shell.editing_package = shell.editing_scene = false;
            window.set_text_input(false);
            if (was_import) import_asset(shell);
            else if (was_package) create_package(shell);
            else if (was_scene) commit_scene_field(shell);
            else create_project_from_field(shell);
            continue;
          }
        }
        switch (event.type) {
        case InputEventType::PointerMove:
          last_input = "pointer move";
          if (shell.scene_dragging &&
              shell.scene_preview.contains(event.position)) {
            if (auto *e = selected_scene_entity(shell); e != nullptr) {
              const auto &pv = shell.scene_preview;
              e->x = std::clamp(
                  (event.position.x - pv.x) / pv.width * 1280.f, 0.f,
                  1280.f - e->w);
              e->y = std::clamp(
                  (event.position.y - pv.y) / pv.height * 720.f, 0.f,
                  720.f - e->h);
              shell.scene_modified = true;
            }
          }
          break;
        case InputEventType::LeftPressed:
          last_input = "left press";
          if (shell.tool == Tool::Scene &&
              shell.scene_preview.contains(event.position)) {
            const auto &pv = shell.scene_preview;
            const float wx =
                (event.position.x - pv.x) / pv.width * 1280.f;
            const float wy =
                (event.position.y - pv.y) / pv.height * 720.f;
            for (std::size_t i = shell.scene_doc.entities.size(); i-- > 0;) {
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
              else shell.scene_buffer.clear();
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
          } else if (shell.editing_project_name || shell.editing_import ||
                     shell.editing_package || shell.editing_scene) {
            shell.editing_project_name = shell.editing_import =
                shell.editing_package = shell.editing_scene = false;
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
