#include <stellar/engine/profiler.hpp>

#include <algorithm>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace stellar::engine;

namespace {
int failures = 0;
void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}
} // namespace

int main() {
  auto &profiler = Profiler::instance();
  profiler.set_enabled(true);
  profiler.reset_aggregates();

  // Record two frames with distinct spans.
  for (int i = 0; i < 2; ++i) {
    profiler.begin_frame();
    {
      auto scope = profiler.span("physics", "sim");
      (void)scope;
    }
    {
      auto scope = profiler.span("render", "gpu");
      (void)scope;
    }
    profiler.add_counter("draws", 3);
    const auto frame = profiler.end_frame();
    check(frame.index == static_cast<std::uint64_t>(i), "frame index");
    check(frame.spans.size() == 2, "spans recorded");
    check(frame.counters.at("draws") == 3, "counter recorded");
  }
  const auto aggregates = profiler.aggregates();
  check(aggregates.size() == 2, "aggregates grouped by name+category");

  // export -> parse round-trip.
  const auto exported = profiler.export_json();
  const auto capture = ProfileCapture::parse(exported);
  check(capture.has_value(), "export parses");
  check(capture->frames.size() == 2 && capture->frames[0].spans.size() == 2,
        "frames survive");
  check(capture->frames[0].counters.at("draws") == 3,
        "counters survive");
  check(capture->aggregates.size() == 2, "aggregates survive");

  // to_json re-serializes and parses identically.
  const auto reparsed = ProfileCapture::parse(capture->to_json());
  check(reparsed.has_value() && reparsed->frames.size() == 2 &&
            reparsed->aggregates.size() == 2,
        "to_json round-trips");

  // Malformed input rejects.
  check(!ProfileCapture::parse("not json"), "garbage rejected");
  check(!ProfileCapture::parse("{}"), "schema-less doc rejected");
  check(!ProfileCapture::parse("{\"frames\":[],\"aggregates\":{}}"),
        "non-array aggregates rejected");

  // compare_captures: B regresses "physics", improves "render", adds
  // "network" — rows sort by absolute delta.
  ProfileCapture a, b;
  a.aggregates = {{"physics", "sim", 10, 1000, 200},
                  {"render", "gpu", 10, 2000, 300}};
  b.aggregates = {{"physics", "sim", 10, 1600, 200},
                  {"render", "gpu", 10, 1000, 300},
                  {"network", "sim", 5, 250, 100}};
  const auto rows = compare_captures(a, b);
  check(rows.size() == 3, "union of keys");
  check(rows[0].name == "render" && rows[0].mean_ns_a == 200.0 &&
            rows[0].mean_ns_b == 100.0,
        "largest delta first");
  check(rows[1].name == "physics" && rows[1].mean_ns_b == 160.0,
        "regression ranked by magnitude");
  check(rows[2].name == "network" && rows[2].calls_a == 0 &&
            rows[2].mean_ns_a == 0.0,
        "b-only key reports zero on a");

  // Multi-threaded recording: spans and call aggregates accumulate in
  // per-thread buffers and merge at thread exit/frame boundaries — no
  // global lock on the recording path.
  profiler.reset_aggregates();
  profiler.begin_frame();
  {
    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t)
      workers.emplace_back([&profiler] {
        for (int i = 0; i < 50; ++i) {
          auto scope = profiler.span("worker", "job");
          (void)scope;
        }
      });
    for (auto &worker : workers) worker.join();
  }
  const auto worker_frame = profiler.end_frame();
  check(worker_frame.spans.size() == 200, "threaded spans drained");
  const auto merged = profiler.aggregates();
  const auto worker_aggregate =
      std::find_if(merged.begin(), merged.end(),
                   [](const ProfileAggregate &a) { return a.name == "worker"; });
  check(worker_aggregate != merged.end() && worker_aggregate->calls == 200,
        "threaded aggregates merged");

  if (failures == 0)
    std::cout << "Profiler tests passed\n";
  return failures == 0 ? 0 : 1;
}
