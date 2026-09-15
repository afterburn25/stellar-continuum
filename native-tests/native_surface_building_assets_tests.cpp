#include "native_surface_building_assets.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace s = stellar::native_surface_building;
using stellar::native_map::ImagePreparationQueue;
using stellar::native_map::RgbaImage;
using namespace std::chrono_literals;
namespace {
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
const s::SurfaceBuildingAssetScope scope{1, 2, 3, 4};
s::SurfaceBuildingAssetRequest request(float angle = 0, int size = 64) {
  s::SurfaceBuildingAssetRequest result;
  result.state.type_id = "fabricator"; result.state.rotation_degrees = angle;
  result.state.complete = true; result.state.progress_fraction = 1;
  result.state.powered = result.state.enabled = result.state.staffed = true;
  result.spec.width = result.spec.height = size;
  return result;
}
s::SurfaceBuildingRasterResult fake(const s::SurfaceBuildingStateKey &key,
                                   const s::SurfaceBuildingRasterSpec &spec) {
  auto result = std::make_shared<s::PreparedSurfaceBuildingRaster>();
  result->state = key;
  result->image = RgbaImage::create(spec.width, spec.height,
      std::vector<std::uint8_t>(static_cast<std::size_t>(spec.width) * spec.height * 4, 127));
  result->projection = {{spec.width * .5f, spec.height * .75f},
                       {0, 0, static_cast<float>(spec.width), static_cast<float>(spec.height)}, 14};
  result->input_triangles = result->rasterized_triangles = 1; result->pixel_tests = 4;
  return {result, s::SurfaceBuildingRasterError::None, {}};
}
std::vector<s::SurfaceBuildingAssetView> settled(s::NativeSurfaceBuildingAssets &assets,
    const std::vector<s::SurfaceBuildingAssetRequest> &requests,
    s::SurfaceBuildingAssetScope current = scope) {
  const auto deadline = std::chrono::steady_clock::now() + 10s;
  for (;;) {
    auto result = assets.update(current, requests);
    bool pending{};
    for (const auto &view : result) pending |= view.status == s::SurfaceBuildingAssetStatus::Pending;
    if (!pending) return result;
    check(std::chrono::steady_clock::now() < deadline, "Surface cache did not settle.");
    std::this_thread::sleep_for(1ms);
  }
}
struct Gate {
  struct State { std::mutex mutex; std::condition_variable cv; bool started{}, released{}; };
  std::shared_ptr<State> state{std::make_shared<State>()};
  ~Gate() { release(); }
  static void block(const std::shared_ptr<State> &state) {
    std::unique_lock lock(state->mutex); state->started = true; state->cv.notify_all();
    state->cv.wait(lock, [&] { return state->released; });
  }
  void wait() { std::unique_lock lock(state->mutex);
    check(state->cv.wait_for(lock, 10s, [&] { return state->started; }), "Worker never reached gate."); }
  void release() { { std::lock_guard lock(state->mutex); state->released = true; } state->cv.notify_all(); }
};
void coalescing_normalized_keys_and_owner() {
  auto queue = std::make_shared<ImagePreparationQueue>();
  const auto owner = std::this_thread::get_id(); std::atomic<int> calls{};
  s::NativeSurfaceBuildingAssets assets(queue, [&](const auto &key, const auto &spec) {
    check(std::this_thread::get_id() != owner, "Geometry factory ran on owner thread.");
    ++calls; return fake(key, spec);
  });
  auto first = request(); first.state.complete = false; first.state.progress_fraction = .72;
  auto second = first; second.state.progress_fraction = .99;
  const auto initial = assets.update(scope, std::vector{first, second});
  check(initial[0].status == s::SurfaceBuildingAssetStatus::Pending && assets.stats().admitted == 1,
        "Duplicate normalized visual states did not coalesce.");
  const auto ready = settled(assets, {first, second});
  check(ready[0].prepared && ready[0].prepared == ready[1].prepared && calls == 1,
        "Duplicates did not share a single prepared resource.");
  second.state.complete = true;
  const auto complete = settled(assets, {second});
  check(complete[0].prepared && complete[0].prepared != ready[0].prepared && calls == 2,
        "Authoritative completion did not replace unfinished imagery.");
  bool rejected{};
  std::thread other([&] { try { (void)assets.stats(); } catch (const std::logic_error &) { rejected = true; } });
  other.join(); check(rejected, "Nonowner cache access was accepted.");
}
void stale_work_and_shared_capacity() {
  auto queue = std::make_shared<ImagePreparationQueue>(1, 1u << 20);
  Gate gate; std::atomic<int> calls{};
  s::NativeSurfaceBuildingAssets assets(queue, [state = gate.state, &calls](const auto &key, const auto &spec) {
    if (++calls == 1) Gate::block(state); return fake(key, spec);
  });
  (void)assets.update(scope, std::vector{request()}); gate.wait();
  auto next = scope; ++next.campaign_generation;
  const auto waiting = assets.update(next, std::vector{request(90)});
  check(waiting[0].status == s::SurfaceBuildingAssetStatus::Deferred &&
        assets.stats().canceled == 1 && assets.stats().pending == 0,
        "Old scope work was not canceled or shared queue capacity was bypassed.");
  gate.release();
  const auto deadline = std::chrono::steady_clock::now() + 10s;
  while (queue->outstanding_jobs()) {
    check(std::chrono::steady_clock::now() < deadline, "Canceled worker did not drain.");
    std::this_thread::sleep_for(1ms);
  }
  const auto ready = settled(assets, {request(90)}, next);
  check(ready[0].prepared && ready[0].prepared->state.rotation_degrees == 90 &&
        assets.stats().completed == 1, "Stale result was published into a new scope.");
  (void)assets.update(next, std::vector{request(180)});
  (void)assets.update(next, {});
  check(assets.stats().pending == 0 && assets.stats().canceled == 2,
        "A removed visible request retained pending work.");
}
void failures_latch_and_invalid_requests_do_not_start() {
  auto queue = std::make_shared<ImagePreparationQueue>(); std::atomic<int> calls{};
  s::NativeSurfaceBuildingAssets assets(queue, [&](const auto &, const auto &) -> s::SurfaceBuildingRasterResult {
    ++calls; throw std::runtime_error("deterministic raster failure");
  });
  auto result = settled(assets, {request()});
  check(result[0].status == s::SurfaceBuildingAssetStatus::Failed &&
        result[0].message == "deterministic raster failure", "Worker failure was hidden.");
  for (int i = 0; i < 20; ++i) {
    (void)assets.update(scope, {}); result = assets.update(scope, std::vector{request()});
  }
  check(calls == 1 && assets.stats().pending == 0 && result[0].status == s::SurfaceBuildingAssetStatus::Failed,
        "Terminal failure automatically retried after view changes.");
  assets.retry_failed(); (void)settled(assets, {request()}); check(calls == 2, "Explicit retry was ignored.");
  auto invalid = request(); invalid.state.rotation_degrees = std::numeric_limits<float>::quiet_NaN();
  auto oversized = request(); oversized.spec.width = 513;
  result = assets.update(scope, std::vector{invalid, oversized});
  check(result[0].status == s::SurfaceBuildingAssetStatus::Failed &&
        result[1].status == s::SurfaceBuildingAssetStatus::Failed && calls == 2,
        "Invalid input reached worker preparation.");
  bool limited{};
  try { (void)assets.update(scope, std::vector(131, request())); } catch (const std::length_error &) { limited = true; }
  check(limited, "Unbounded request list was accepted.");
  auto tiny = std::make_shared<ImagePreparationQueue>(1, 4);
  s::NativeSurfaceBuildingAssets small(tiny, fake);
  const auto rejected = small.update(scope, std::vector{request()});
  check(rejected[0].status == s::SurfaceBuildingAssetStatus::Failed && small.stats().failed == 1,
        "An impossible queue reservation was not a clean terminal failure.");
}
void output_metadata_is_checked() {
  auto queue = std::make_shared<ImagePreparationQueue>();
  s::NativeSurfaceBuildingAssets assets(queue, [](const auto &key, const auto &spec) {
    auto result = fake(key, spec);
    auto corrupt = std::make_shared<s::PreparedSurfaceBuildingRaster>(*result.prepared);
    corrupt->state.rotation_degrees = 222; result.prepared = corrupt; return result;
  });
  const auto result = settled(assets, {request()});
  check(result[0].status == s::SurfaceBuildingAssetStatus::Failed && !result[0].prepared &&
        assets.stats().cached_bytes == 0 && assets.stats().reserved_bytes == 0,
        "Mismatched state metadata was published or retained its reservation.");
  for (int mode = 0; mode < 4; ++mode) {
    s::NativeSurfaceBuildingAssets invalid(queue, [mode](const auto &key, const auto &spec) {
      auto result = fake(key, spec);
      auto corrupt = std::make_shared<s::PreparedSurfaceBuildingRaster>(*result.prepared);
      if (mode == 0) corrupt->projection.projected_bounds.width = 0;
      if (mode == 1) corrupt->projection.projected_bounds.x = -1;
      if (mode == 2) corrupt->projection.projected_bounds.width = spec.width + 1.f;
      if (mode == 3) corrupt->projection.ground_anchor_pixels.y = spec.height + 1.f;
      result.prepared = corrupt; return result;
    });
    check(settled(invalid, {request()})[0].status == s::SurfaceBuildingAssetStatus::Failed,
          "Unusable projection metadata was accepted.");
  }
}
void admission_memory_and_lru_are_bounded() {
  auto queue = std::make_shared<ImagePreparationQueue>();
  Gate gate;
  s::NativeSurfaceBuildingAssets blocked(queue, [state = gate.state](const auto &key, const auto &spec) {
    Gate::block(state); return fake(key, spec);
  });
  std::vector<s::SurfaceBuildingAssetRequest> many;
  for (int i = 0; i < 8; ++i) many.push_back(request(static_cast<float>(i)));
  (void)blocked.update(scope, many); gate.wait();
  check(blocked.stats().admitted == 2, "Per-frame admission cap failed.");
  (void)blocked.update(scope, many); (void)blocked.update(scope, many);
  check(blocked.stats().pending == 4 && blocked.stats().admitted == 4, "Pending work cap failed.");
  blocked.clear(); gate.release();

  s::NativeSurfaceBuildingAssets assets(queue, fake);
  many.clear();
  for (int i = 0; i < 27; ++i) many.push_back(request(static_cast<float>(i), 512));
  const auto constrained = settled(assets, many);
  std::size_t ready{}, deferred{};
  for (const auto &view : constrained) {
    ready += view.status == s::SurfaceBuildingAssetStatus::Ready;
    deferred += view.status == s::SurfaceBuildingAssetStatus::Deferred;
  }
  check(ready == 24 && deferred == 3 && assets.stats().cached_bytes == s::NativeSurfaceBuildingAssets::maximum_cached_bytes,
        "Pinned images exceeded memory budget or thrashed instead of deferring.");
  check(settled(assets, {request(100, 512)})[0].prepared && assets.stats().evicted > 0,
        "Unused images were not evicted for new visible work.");

  assets.clear(); many.clear();
  for (int i = 0; i < 48; ++i) many.push_back(request(static_cast<float>(i)));
  (void)settled(assets, many); check(assets.stats().entries == 48, "Cache entry fixture did not fill.");
  (void)settled(assets, {request(0)}); // Most recently used; angle 1 is now oldest.
  (void)settled(assets, {request(48)});
  auto admissions = assets.stats().admitted;
  (void)settled(assets, {request(0)}); check(assets.stats().admitted == admissions, "LRU evicted a recently used image.");
  (void)settled(assets, {request(1)}); check(assets.stats().admitted == admissions + 1 && assets.stats().entries == 48,
        "LRU did not evict the oldest unused image or exceeded entry count.");
}
void real_worker_prepares_geometry_and_image() {
  auto queue = std::make_shared<ImagePreparationQueue>(); s::NativeSurfaceBuildingAssets assets(queue);
  auto rotated = request(90, 256);
  const auto result = settled(assets, {rotated});
  check(result[0].prepared && result[0].prepared->image->width() == 256 &&
        result[0].prepared->state.rotation_degrees == 90 && result[0].prepared->input_triangles > 10,
        "Real geometry/raster path failed through the Engine worker.");
}
} // namespace
int main() {
  try {
    coalescing_normalized_keys_and_owner(); stale_work_and_shared_capacity();
    failures_latch_and_invalid_requests_do_not_start(); output_metadata_is_checked();
    admission_memory_and_lru_are_bounded(); real_worker_prepares_geometry_and_image();
    std::cout << "Native surface building preparation/cache checks passed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Native surface building preparation/cache failure: " << error.what() << '\n'; return 1;
  }
}
