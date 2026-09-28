#include <stellar/engine/memory_tracker.hpp>
#include <stellar/engine/profiler.hpp>
#include <stellar/engine/foundation.hpp>

#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace

int main() {
    using namespace stellar::engine;

    // Profiler: scoped spans, cross-thread capture, aggregates, JSON export.
    {
        auto& profiler = Profiler::instance();
        profiler.set_enabled(true);
        profiler.reset_aggregates();

        profiler.begin_frame();
        {
            auto scope = profiler.span("update", "sim");
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        {
            auto scope = profiler.span("render", "gpu");
        }
        profiler.add_counter("jobs", 3);
        profiler.set_gauge("vram_mb", 42.5);
        const auto frame = profiler.end_frame();
        check(frame.wall_nanoseconds > 0, "frame wall time recorded");
        bool saw_update = false, saw_render = false;
        for (const auto& span : frame.spans) {
            if (span.name == "update") saw_update = true;
            if (span.name == "render") saw_render = true;
        }
        check(saw_update && saw_render, "frame captured both scopes");
        check(frame.counters.count("jobs") && frame.counters.at("jobs") == 3,
              "frame counter captured");
        check(frame.gauges.count("vram_mb") && frame.gauges.at("vram_mb") == 42.5,
              "frame gauge captured");

        // Worker-thread spans are drained at the frame boundary.
        {
            JobSystem jobs{1};
            profiler.begin_frame();
            jobs.submit([&] {
                auto scope = profiler.span("worker-task", "jobs");
                std::this_thread::sleep_for(std::chrono::microseconds(20));
            }).get();
            const auto worker_frame = profiler.end_frame();
            bool found = false;
            for (const auto& span : worker_frame.spans)
                if (span.name == "worker-task") found = true;
            check(found, "worker-thread span drained into frame");
        }

        const auto aggregates = profiler.aggregates();
        bool update_found = false;
        for (const auto& a : aggregates)
            if (a.name == "update" && a.calls == 1) update_found = true;
        check(update_found, "aggregate records span calls");

        const auto json = profiler.export_json();
        check(json.find("\"update\"") != std::string::npos &&
                  json.find("\"frames\"") != std::string::npos,
              "JSON export contains spans and frames");
        const auto lines = profiler.overlay_lines();
        check(!lines.empty(), "overlay lines produced");

        // Disabled profiler skips recording.
        profiler.set_enabled(false);
        profiler.reset_aggregates();
        { auto scope = profiler.span("off", "x"); }
        check(profiler.aggregates().empty(), "disabled profiler records nothing");
        profiler.set_enabled(true);
    }

    // MemoryTracker: registration, alloc/free accounting, high-water, report.
    {
        auto& tracker = MemoryTracker::instance();
        const auto textures = tracker.register_subsystem("gpu.textures");
        const auto same = tracker.register_subsystem("gpu.textures");
        check(textures == same, "subsystem registration is idempotent");
        const auto meshes = tracker.register_subsystem("gpu.meshes");
        check(meshes != textures, "distinct names get distinct ids");

        tracker.note_alloc(textures, 4096);
        tracker.note_alloc(textures, 2048);
        tracker.note_free(textures, 1024);
        tracker.report(meshes, 8192, 16384);

        const auto snapshot = tracker.snapshot();
        const SubsystemMemoryStats* tex = nullptr;
        for (const auto& s : snapshot.subsystems)
            if (s.name == "gpu.textures") tex = &s;
        check(tex && tex->current_bytes == 5120, "alloc/free accounting");
        check(tex && tex->high_water_bytes == 6144, "high-water mark tracked");
        check(tex && tex->allocation_count == 2 && tex->deallocation_count == 1,
              "alloc/free counts");

        // TrackedAllocator funnels container memory through a subsystem.
        const auto vectors = tracker.register_subsystem("sim.vectors");
        {
            std::vector<int, TrackedAllocator<int>> tracked(
                (TrackedAllocator<int>{vectors}));
            tracked.assign(1000, 7);
        }
        const auto after = tracker.snapshot();
        const SubsystemMemoryStats* vec = nullptr;
        for (const auto& s : after.subsystems)
            if (s.name == "sim.vectors") vec = &s;
        check(vec && vec->current_bytes == 0, "freed tracked memory returns to zero");
        check(vec && vec->high_water_bytes >= 4000, "vector allocation reached tracker");

        const auto json = tracker.export_json();
        check(json.find("\"gpu.textures\"") != std::string::npos,
              "memory JSON export contains subsystems");
        check(!tracker.overlay_lines().empty(), "memory overlay lines produced");
    }

    if (failures != 0) {
        std::cerr << failures << " diagnostics checks failed\n";
        return 1;
    }
    std::cout << "Profiler and MemoryTracker tests passed\n";
    return 0;
}
