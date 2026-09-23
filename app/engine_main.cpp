// Standalone Stellar Engine shell.
//
// This executable deliberately links only the engine libraries
// (stellar_engine + stellar_native_platform). It owns no game module: it
// opens an engine window through the engine platform layer, drives the job
// system, profiler, localization table and memory tracker, and renders a
// live engine status dashboard. It is the runnable proof that the engine is
// reusable without the Stellar Continuum game code.

#include "stellar/build_version.hpp"

#include <stellar/engine/foundation.hpp>
#include <stellar/engine/localization.hpp>
#include <stellar/engine/memory_tracker.hpp>
#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/profiler.hpp>
#include <stellar/engine/runtime_diagnostics.hpp>
#include <stellar/engine/runtime_paths.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
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

std::filesystem::path find_asset(const char *relative) {
  // The shell searches beside the executable first (packaged layout), then
  // walks upward so a build-tree run reaches the shared assets directory.
  auto base = engine::executable_directory();
  for (int depth = 0; depth < 4; ++depth) {
    const auto candidate = base / relative;
    if (std::filesystem::is_regular_file(candidate)) return candidate;
    if (!base.has_parent_path() || base == base.parent_path()) break;
    base = base.parent_path();
  }
  return relative;
}

std::filesystem::path find_locale() {
  auto base = engine::executable_directory();
  for (int depth = 0; depth < 4; ++depth) {
    for (const char *relative : {"Data/locale/en.json", "data/locale/en.json"}) {
      const auto candidate = base / relative;
      if (std::filesystem::is_regular_file(candidate)) return candidate;
    }
    if (!base.has_parent_path() || base == base.parent_path()) break;
    base = base.parent_path();
  }
  return {};
}

void line(DrawList &out, float x, float &y, std::string label, std::string value,
          int font = 14) {
  out.text.push_back(Text{{x, y}, std::move(label), muted, font});
  out.text.push_back(Text{{x + 210.f, y}, std::move(value), ink, font});
  y += font + 8.f;
}

void heading(DrawList &out, float x, float &y, const std::string &title) {
  out.text.push_back(Text{{x, y}, title, accent, 15, 0, std::nullopt,
                          TextAlign::Left, FontFace::Heading});
  y += 26.f;
}

std::string ms(double value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.2f ms", value);
  return buffer;
}

std::string number(std::uint64_t value) { return std::to_string(value); }

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
#else
int main(int argc, char **argv) {
#endif
  (void)argc;
  (void)argv;
  engine::RuntimeDiagnostics diagnostics("engine-shell", STELLAR_ENGINE_VERSION);
  try {
    engine::RuntimeDiagnostics::context("stellar engine standalone shell");
    engine::JobSystem jobs;
    auto &profiler = engine::Profiler::instance();
    profiler.set_enabled(true);

    engine::LocalizationTable locale;
    std::size_t locale_keys{};
    std::string locale_status = "no catalog found";
    if (const auto path = find_locale(); !path.empty()) {
      std::string error;
      if (locale.load_file(path.string(), &error)) {
        locale_keys = locale.size();
        locale_status = path.filename().string() + " (" +
                        std::to_string(locale_keys) + " keys)";
      } else {
        locale_status = "load failed: " + error;
      }
    }

    Window window("Stellar Engine", 1280, 800, false,
                  find_asset("assets/visual/fonts/Rajdhani-SemiBold.ttf"));
    window.set_auto_frame_cap();

    // The provided application icon doubles as the in-window engine emblem.
    std::shared_ptr<const RgbaImage> emblem;
    try {
      emblem = decode_rgba_image(
          find_asset("assets/visual/branding/stellar-continuum-icon-v1.png"),
          256);
    } catch (const std::exception &) {
    }

    std::atomic<int> demo_jobs_done{};
    auto next_job = std::chrono::steady_clock::now();
    auto last_frame = std::chrono::steady_clock::now();
    double fps{};
    std::string last_input = "none";

    for (;;) {
      const auto frame_begin = std::chrono::steady_clock::now();
      const auto snapshot = window.poll();
      if (snapshot.quit_requested) break;
      for (const auto &event : snapshot.events) {
        if (event.type == InputEventType::EscapePressed) return 0;
        switch (event.type) {
        case InputEventType::PointerMove: last_input = "pointer move"; break;
        case InputEventType::LeftPressed: last_input = "left press"; break;
        case InputEventType::LeftReleased: last_input = "left release"; break;
        case InputEventType::RightPressed: last_input = "right press"; break;
        case InputEventType::Wheel: last_input = "wheel"; break;
        case InputEventType::KeyPressed:
        case InputEventType::KeyReleased: last_input = "key"; break;
        default: break;
        }
      }
      if (!snapshot.renderable()) continue;

      // Exercise the job system: a tagged trivial job every ~250 ms.
      if (frame_begin >= next_job) {
        next_job = frame_begin + std::chrono::milliseconds(250);
        (void)jobs.submit("engine-shell.demo", engine::JobPriority::Normal, {},
                          [&demo_jobs_done] { demo_jobs_done.fetch_add(1); });
      }

      profiler.begin_frame();
      const auto frame_scope = profiler.span("engine-shell.frame", "frame");
      const auto build_scope = profiler.span("engine-shell.build", "frame");

      const float w = static_cast<float>(snapshot.drawable_width);
      const float h = static_cast<float>(snapshot.drawable_height);
      const float s = std::clamp(h / 800.f, 0.8f, 2.0f);

      DrawList draw;
      draw.overlay.push_back(FilledRectangle{{0, 0, w, h}, {4, 10, 16, 255}});

      // Subtle grid so the window reads as a live rendered surface.
      for (float gx = 0; gx < w; gx += 64.f)
        draw.overlay.push_back(Line{{gx, 0}, {gx, h}, {14, 26, 36, 255}});
      for (float gy = 0; gy < h; gy += 64.f)
        draw.overlay.push_back(Line{{0, gy}, {w, gy}, {14, 26, 36, 255}});

      const UiRect panel{28 * s, 24 * s, w - 56 * s, h - 48 * s};
      draw.overlay.push_back(FilledRectangle{panel, panel_fill});
      draw.overlay.push_back(StrokedRectangle{panel, panel_edge});

      float x = panel.x + 26 * s;
      float y = panel.y + 22 * s;
      if (emblem) {
        const float emblem_size = 72 * s;
        draw.overlay.push_back(
            Image{emblem, {panel.x + panel.width - 26 * s - emblem_size,
                           panel.y + 20 * s, emblem_size, emblem_size}});
      }
      draw.text.push_back(
          Text{{x, y}, "STELLAR ENGINE", ink, static_cast<int>(30 * s), 0,
               std::nullopt, TextAlign::Left, FontFace::Heading});
      y += 44 * s;
      draw.text.push_back(
          Text{{x, y}, "Standalone engine shell - no game module linked",
               muted, static_cast<int>(14 * s)});
      y += 34 * s;

      const int font = static_cast<int>(14 * s);
      heading(draw, x, y, "PLATFORM");
      line(draw, x, y, "engine", STELLAR_ENGINE_VERSION, font);
      line(draw, x, y, "source", STELLAR_SOURCE_COMMIT, font);
      line(draw, x, y, "gpu", window.graphics_adapter(), font);
      line(draw, x, y, "presentation", window.presentation_mode(), font);
      line(draw, x, y,
           "display", std::to_string(snapshot.drawable_width) + " x " +
                          std::to_string(snapshot.drawable_height),
           font);
      y += 10 * s;

      heading(draw, x, y, "RUNTIME");
      line(draw, x, y, "fps", std::to_string(static_cast<int>(fps)), font);
      const auto stats = jobs.stats();
      line(draw, x, y, "job workers", number(stats.workers), font);
      line(draw, x, y, "jobs completed",
           number(stats.completed) + " / " + number(stats.submitted) +
               " submitted",
           font);
      line(draw, x, y, "jobs failed/cancelled",
           number(stats.failed) + " / " + number(stats.cancelled), font);
      line(draw, x, y, "demo jobs run", number(demo_jobs_done.load()), font);
      line(draw, x, y, "text cache",
           number(window.text_cache_entries()) + " entries / " +
               number(window.text_cache_bytes() / 1024) + " KiB",
           font);
      line(draw, x, y, "image cache",
           number(window.image_cache_entries()) + " entries / " +
               number(window.image_cache_resident_bytes() / 1024 / 1024) +
               " MiB",
           font);
      y += 10 * s;

      heading(draw, x, y, "SERVICES");
      line(draw, x, y, "localization", locale_status, font);
      line(draw, x, y, "sample key",
           locale.contains("GENERAL_TITLE")
               ? std::string(locale.translate("GENERAL_TITLE"))
               : "(GENERAL_TITLE absent)",
           font);
      line(draw, x, y, "pointer",
           std::to_string(static_cast<int>(snapshot.pointer.x)) + ", " +
               std::to_string(static_cast<int>(snapshot.pointer.y)),
           font);
      line(draw, x, y, "last input", last_input, font);
      y += 10 * s;

      draw.text.push_back(
          Text{{x, panel.y + panel.height - 30 * s},
               "ESC to quit - F12 screenshots to the Pictures folder", muted,
               static_cast<int>(12 * s)});

      // Live profiler feed in the right-hand pane.
      const float rx = panel.x + panel.width * 0.55f;
      float ry = panel.y + 60 * s;
      heading(draw, rx, ry, "PROFILER");
      const auto aggregates = profiler.aggregates();
      const auto count = std::min<std::size_t>(aggregates.size(), 14);
      for (std::size_t i = 0; i < count; ++i) {
        const auto &a = aggregates[i];
        line(draw, rx, ry,
             a.category.empty() ? a.name : a.category + "/" + a.name,
             number(a.calls) + " calls, " +
                 ms(static_cast<double>(a.total_nanoseconds) / 1e6) + " total",
             font);
      }

      // Frame-time bar graph across the bottom of the pane.
      const auto frames = profiler.recent_frames();
      const float bar_w = 6 * s, base_y = panel.y + panel.height - 60 * s;
      float bx = rx;
      for (const auto &frame : frames) {
        const auto scaled = static_cast<float>(frame.wall_nanoseconds) / 1e6f;
        const float bar_h = std::clamp(scaled * 4.f, 2.f, 60 * s);
        draw.overlay.push_back(FilledRectangle{
            {bx, base_y - bar_h, bar_w - 1.f, bar_h}, bar_fill});
        bx += bar_w;
        if (bx > panel.x + panel.width - 40 * s) break;
      }
      draw.text.push_back(Text{{rx, base_y + 8 * s}, "frame wall ms", muted,
                               static_cast<int>(11 * s)});

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
