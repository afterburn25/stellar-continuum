#include "native_surface_art_assets.hpp"
#include "native_surface_workspace.hpp"

#include <stellar/engine/native_image_preparation.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;
using namespace stellar::native_colony;
using namespace stellar::native_colony_ui;
using namespace stellar::native_map;
using namespace stellar::native_surface_ui;

namespace {
void require(
    const bool value, std::string message,
    const std::source_location where = std::source_location::current()) {
  if (!value)
    throw std::runtime_error(std::move(message) + " at " + where.file_name() +
                             ":" + std::to_string(where.line()));
}

[[nodiscard]] bool near(const float left, const float right,
                        const float tolerance = .02f) noexcept {
  return std::abs(left - right) <= tolerance;
}

[[nodiscard]] bool same_rect(const UiRect left, const UiRect right) noexcept {
  return near(left.x, right.x) && near(left.y, right.y) &&
         near(left.width, right.width) && near(left.height, right.height);
}

[[nodiscard]] Point center(const UiRect value) noexcept {
  return {value.x + value.width * .5f, value.y + value.height * .5f};
}

template <class Request>
[[nodiscard]] std::shared_ptr<const RgbaImage> await_image(
    Request &&request, std::string timeout_message) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(15);
  for (;;) {
    if (auto image = request()) return image;
    if (std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error(std::move(timeout_message));
    std::this_thread::yield();
  }
}

[[nodiscard]] NativeColonyView surface_view() {
  NativeColonyView view;
  view.campaign_generation = 41;
  view.revision = 7;
  view.player_civilization_id = 2;
  view.system_id = 3;
  view.body_id = 5;
  view.colony_id = 11;
  view.colony_name = "Test Surface";
  view.body_display_name = "Test World";
  view.solid_surface = true;
  return view;
}

[[nodiscard]] std::vector<const Image *> terrain_images(
    const DrawList &draw, const std::shared_ptr<const RgbaImage> &expected) {
  std::vector<const Image *> result;
  for (const auto &command : draw.overlay)
    if (const auto *image = std::get_if<Image>(&command);
        image && image->resource == expected)
      result.push_back(image);
  return result;
}

[[nodiscard]] double require_terrain_cover(
    const NativeSurfaceWorkspace &workspace,
    const std::shared_ptr<const RgbaImage> &image, const int width,
    const int height) {
  DrawList draw;
  workspace.render(draw, width, height);
  const auto layout = SurfaceWorkspaceLayout::for_viewport(width, height);
  const auto tiles = terrain_images(draw, image);
  require(!tiles.empty(), "prepared terrain image was not rendered");
  require(tiles.size() <= 64, "terrain render exceeded its 64-image bound");

  auto left = tiles.front()->destination.x;
  auto top = tiles.front()->destination.y;
  auto right = left + tiles.front()->destination.width;
  auto bottom = top + tiles.front()->destination.height;
  const auto tile_world_units = static_cast<double>(
      tiles.front()->destination.width / workspace.viewport().pixels_per_unit);
  const auto lod_multiple = tile_world_units / 512.0;
  const auto lod_power = std::round(std::log2(lod_multiple));
  require(tile_world_units >= 512.0 - .01 &&
              std::abs(lod_multiple - std::pow(2.0, lod_power)) < .001,
          "terrain tile did not use a 512-unit power-of-two LOD");
  const auto expected_tile_pixels = static_cast<float>(
      workspace.viewport().pixels_per_unit * tile_world_units);
  for (const auto *tile : tiles) {
    require(tile->resource == image,
            "terrain tile did not retain the injected immutable image");
    require(tile->clip && same_rect(*tile->clip, layout.terrain),
            "terrain image was not clipped to the visible terrain field");
    require(near(tile->destination.width, expected_tile_pixels) &&
                near(tile->destination.height, expected_tile_pixels),
            "terrain tile pixel scale did not match the surface viewport");
    const auto world_corner = workspace.viewport().screen_to_world(
        {tile->destination.x, tile->destination.y}, layout.terrain);
    require(std::abs(world_corner.first / tile_world_units -
                     std::round(world_corner.first / tile_world_units)) <
                .001 &&
                std::abs(world_corner.second / tile_world_units -
                         std::round(world_corner.second / tile_world_units)) <
                    .001,
            "terrain tile corner was not aligned to its world-space LOD grid");
    require(tile->destination.x < layout.terrain.x + layout.terrain.width &&
                tile->destination.x + tile->destination.width >
                    layout.terrain.x &&
                tile->destination.y < layout.terrain.y + layout.terrain.height &&
                tile->destination.y + tile->destination.height >
                    layout.terrain.y,
            "terrain draw list retained an image outside its visible field");
    left = std::min(left, tile->destination.x);
    top = std::min(top, tile->destination.y);
    right = std::max(right,
                     tile->destination.x + tile->destination.width);
    bottom = std::max(bottom,
                      tile->destination.y + tile->destination.height);
  }
  require(left <= layout.terrain.x && top <= layout.terrain.y &&
              right >= layout.terrain.x + layout.terrain.width &&
              bottom >= layout.terrain.y + layout.terrain.height,
          "terrain image tiles did not cover the visible terrain field");
  require(draw.world.empty(),
          "surface terrain preparation unexpectedly emitted GPU/world work");
  return tile_world_units;
}

void actual_asset_is_prepared_once(const fs::path &asset_root) {
  auto queue = std::make_shared<ImagePreparationQueue>(1,
      NativeSurfaceArtAssets::maximum_cached_bytes);
  NativeSurfaceArtAssets assets(asset_root);
  assets.use_background_preparation(queue);

  require(!assets.request_image(),
          "first surface image request decoded synchronously");
  require(queue->outstanding_jobs() == 1 &&
              queue->reserved_bytes() ==
                  NativeSurfaceArtAssets::maximum_cached_bytes,
          "surface image request did not use one bounded background job");
  const auto image = await_image(
      [&] { return assets.request_image(); },
      "approved surface image did not finish background preparation");
  require(image->byte_size() > 0 &&
              image->byte_size() <=
                  NativeSurfaceArtAssets::maximum_cached_bytes,
          "approved surface image exceeded its decoded cache budget");
  require(assets.image() == image && assets.cache_bytes() == image->byte_size() &&
              assets.decode_count() == 1,
          "prepared surface image was not retained as one shared cache entry");
  require(queue->outstanding_jobs() == 0 && queue->reserved_bytes() == 0,
          "collected surface image retained preparation capacity");
  for (int frame = 0; frame < 32; ++frame)
    require(assets.request_image() == image,
            "repeated surface request did not reuse the prepared image");
  require(assets.decode_count() == 1 && queue->outstanding_jobs() == 0,
          "repeated surface requests decoded or queued the image again");

  std::atomic<bool> request_rejected{}, image_rejected{}, queue_rejected{};
  std::jthread wrong_thread([&] {
    try {
      (void)assets.request_image();
    } catch (const std::logic_error &) {
      request_rejected = true;
    }
    try {
      (void)assets.image();
    } catch (const std::logic_error &) {
      image_rejected = true;
    }
    try {
      assets.use_background_preparation(queue);
    } catch (const std::logic_error &) {
      queue_rejected = true;
    }
  });
  wrong_thread.join();
  require(request_rejected && image_rejected && queue_rejected,
          "non-owner thread accessed surface art state");
  require(assets.image() == image && assets.decode_count() == 1,
          "rejected non-owner access disturbed the prepared image");
}

void missing_asset_reports_one_async_failure(const fs::path &asset_root) {
  auto queue = std::make_shared<ImagePreparationQueue>(1,
      NativeSurfaceArtAssets::maximum_cached_bytes);
  NativeSurfaceArtAssets missing(
      asset_root / "__missing_native_surface_art_test_root__");
  missing.use_background_preparation(queue);
  require(!missing.request_image(),
          "missing surface image failed synchronously instead of through its future");

  bool failed{};
  std::string message;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while (!failed && std::chrono::steady_clock::now() < deadline) {
    try {
      (void)missing.request_image();
    } catch (const std::exception &error) {
      failed = true;
      message = error.what();
    }
    if (!failed) std::this_thread::yield();
  }
  require(failed, "missing surface image future did not finish with an error");
  require(message.find("image") != std::string::npos ||
              message.find("Image") != std::string::npos,
          "missing surface image future did not report a useful image error");
  require(queue->outstanding_jobs() == 0 && queue->reserved_bytes() == 0,
          "failed surface image future retained capacity");
  require(!missing.image() && missing.decode_count() == 0 &&
              missing.cache_bytes() == 0,
          "failed surface image future produced or counted a cache entry");

  for (int repeated_request = 0; repeated_request < 4; ++repeated_request) {
    bool repeated_failure{};
    std::string repeated_message;
    try {
      const auto unexpected = missing.request_image();
      require(!unexpected,
              "terminal surface image failure unexpectedly returned an image");
    } catch (const std::exception &error) {
      repeated_failure = true;
      repeated_message = error.what();
    }
    require(repeated_failure && repeated_message == message,
            "repeated missing surface request did not rethrow the cached cause");
    require(queue->outstanding_jobs() == 0 && queue->reserved_bytes() == 0,
            "repeated missing surface request scheduled another preparation job");
    require(missing.decode_count() == 0 && missing.cache_bytes() == 0,
            "repeated missing surface request changed cache diagnostics");
  }
}

void shared_image_tracks_camera_and_resize() {
  const auto image = RgbaImage::create(
      2, 2, {12, 34, 56, 255, 78, 90, 12, 255,
             34, 56, 78, 255, 90, 12, 34, 255});
  NativeSurfaceWorkspace workspace;
  const auto original = surface_view();
  workspace.set_terrain_image(image);
  workspace.open(original, 1280, 720);
  (void)require_terrain_cover(workspace, image, 1280, 720);

  const auto layout = SurfaceWorkspaceLayout::for_viewport(1280, 720);
  const Point zoom_anchor{layout.terrain.x + layout.terrain.width * .63f,
                          layout.terrain.y + layout.terrain.height * .37f};
  const auto anchored_world =
      workspace.viewport().screen_to_world(zoom_anchor, layout.terrain);
  const auto scale_before = workspace.viewport().pixels_per_unit;
  (void)workspace.handle(
      {InputEventType::Wheel, zoom_anchor, {}, 2.f}, 1280, 720);
  const auto anchored_after = workspace.viewport().world_to_screen(
      anchored_world.first, anchored_world.second, layout.terrain);
  require(near(anchored_after.x, zoom_anchor.x) &&
              near(anchored_after.y, zoom_anchor.y),
          "surface art zoom moved its world anchor");
  require(workspace.viewport().pixels_per_unit > scale_before,
          "surface art zoom did not increase tile projection scale");
  (void)require_terrain_cover(workspace, image, 1280, 720);

  const auto tracked_world = std::pair{73.0, -49.0};
  const auto tracked_before = workspace.viewport().world_to_screen(
      tracked_world.first, tracked_world.second, layout.terrain);
  const auto drag_start = center(layout.terrain);
  constexpr Point drag_delta{31.f, 19.f};
  (void)workspace.handle({InputEventType::LeftPressed, drag_start}, 1280, 720);
  (void)workspace.handle(
      {InputEventType::PointerMove,
       {drag_start.x + drag_delta.x, drag_start.y + drag_delta.y}, drag_delta},
      1280, 720);
  (void)workspace.handle(
      {InputEventType::LeftReleased,
       {drag_start.x + drag_delta.x, drag_start.y + drag_delta.y}},
      1280, 720);
  const auto tracked_after = workspace.viewport().world_to_screen(
      tracked_world.first, tracked_world.second, layout.terrain);
  require(near(tracked_after.x - tracked_before.x, drag_delta.x) &&
              near(tracked_after.y - tracked_before.y, drag_delta.y),
          "surface art pan changed world projection beyond its translation");

  (void)require_terrain_cover(workspace, image, 1280, 720);
  (void)require_terrain_cover(workspace, image, 1920, 1080);
  (void)require_terrain_cover(workspace, image, 3840, 2160);

  const auto zoom_out_anchor = center(layout.terrain);
  (void)workspace.handle(
      {InputEventType::Wheel, zoom_out_anchor, {}, -100.f}, 1280, 720);
  require(near(static_cast<float>(workspace.viewport().pixels_per_unit), .08f),
          "extreme surface zoom-out did not reach the bounded camera scale");
  const auto coarse_tile_world_units =
      require_terrain_cover(workspace, image, 1280, 720);
  require(coarse_tile_world_units > 512.0,
          "extreme zoom-out did not exercise adaptive terrain LOD under the 64-image cap");
  require(workspace.view() && workspace.view()->campaign_generation ==
                                  original.campaign_generation &&
              workspace.view()->revision == original.revision &&
              workspace.view()->colony_id == original.colony_id &&
              workspace.view()->construction_sites.empty() &&
              workspace.view()->available_buildings.empty(),
          "surface art camera or render path mutated colony facts");
}
} // namespace

int main(const int argc, char **argv) try {
  require(argc == 2, "Usage: native_surface_art_tests <asset root>");
  const auto asset_root = fs::absolute(argv[1]);
  actual_asset_is_prepared_once(asset_root);
  missing_asset_reports_one_async_failure(asset_root);
  shared_image_tracks_camera_and_resize();
  std::cout << "native surface art tests passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
