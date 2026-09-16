#include "native_surface_scene.hpp"
#include "native_surface_workspace.hpp"

#include <stellar/core/surface_economy.hpp>
#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/native_triangle_mesh.hpp>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

namespace {
using stellar::native_colony::NativeColonyView;
using stellar::native_colony::NativeSurfaceSite;
using stellar::native_map::InputEvent;
using stellar::native_map::InputEventType;
using stellar::native_map::Point;
using stellar::native_map::Window;
using stellar::native_colony_ui::NativeSurfaceWorkspace;
using stellar::native_colony_ui::SurfaceWorkspaceLayout;

struct Arguments { std::filesystem::path asset_root, output; int width{}, height{}; };

Arguments parse(int argc, char **argv) {
  if (argc != 9) throw std::invalid_argument("usage: --asset-root PACKAGE --output BMP --width N --height N");
  Arguments result;
  for (int i = 1; i < argc; i += 2) {
    const std::string key = argv[i], value = argv[i + 1];
    if (key == "--asset-root") result.asset_root = std::filesystem::absolute(value);
    else if (key == "--output") result.output = std::filesystem::absolute(value);
    else if (key == "--width") result.width = std::stoi(value);
    else if (key == "--height") result.height = std::stoi(value);
    else throw std::invalid_argument("unknown argument: " + key);
  }
  if (!std::filesystem::is_directory(result.asset_root) || result.output.empty() ||
      (result.width != 1280 && result.width != 1920) ||
      (result.height != 720 && result.height != 1080))
    throw std::invalid_argument("invalid visual fixture arguments");
  return result;
}

NativeColonyView fixture_view() {
  NativeColonyView view;
  view.colony_name = "Surface visual fixture (not a campaign)";
  view.body_display_name = "Surface artwork fixture";
  view.solid_surface = true;
  view.surface_hub_level = 3;
  const auto catalog = stellar::core::surface_building_catalog();
  if (catalog.size() != 14) throw std::runtime_error("surface catalog must contain 14 definitions");
  constexpr float radius = 110.f;
  for (std::size_t i = 0; i < catalog.size(); ++i) {
    const float angle = static_cast<float>(i) * 6.28318530718f / 14.f;
    const auto &definition = catalog[i];
    NativeSurfaceSite site;
    site.building_id = static_cast<int>(i);
    site.type_id = definition.id;
    site.name = definition.name;
    site.x = radius * std::cos(angle);
    site.z = radius * std::sin(angle);
    site.rotation_degrees = static_cast<float>(i * 15);
    site.complete = site.powered = site.staffed = site.enabled = true;
    site.condition = site.efficiency = site.industry_progress = site.progress_fraction = 1.;
    view.construction_sites.push_back(std::move(site));
  }
  return view;
}
}

int main(int argc, char **argv) {
  try {
    const auto args = parse(argc, argv);
    const auto font = args.asset_root / "assets/visual/fonts/Rajdhani-SemiBold.ttf";
    const auto ground = args.asset_root / "assets/visual/surface/temperate-ground-albedo-v1.png";
    if (!std::filesystem::is_regular_file(font))
      throw std::runtime_error("visual fixture missing font asset: " + font.string());
    if (!std::filesystem::is_regular_file(ground))
      throw std::runtime_error("visual fixture missing ground asset: " + ground.string());
    Window window("Stellar Surface Visual Fixture", args.width, args.height, false, font);
    const int width = window.drawable_width(), height = window.drawable_height();
    if (width <= 0 || height <= 0)
      throw std::runtime_error("visual fixture received invalid drawable dimensions");
    auto terrain = stellar::native_map::decode_rgba_image(ground);
    NativeSurfaceWorkspace workspace;
    workspace.set_terrain_image(std::move(terrain));
    workspace.open(fixture_view(), width, height);
    const auto layout = SurfaceWorkspaceLayout::for_viewport(width, height);
    const auto point = workspace.viewport().world_to_screen(110., 0., layout.terrain);
    (void)workspace.handle(InputEvent{InputEventType::LeftPressed, point}, width, height);
    (void)workspace.handle(InputEvent{InputEventType::LeftReleased, point}, width, height);
    if (workspace.selected_building_id() != std::optional<int>{0})
      throw std::runtime_error("visual fixture failed to select the first site");
    for (int frame = 0; frame < 3; ++frame) {
      const auto input = window.poll();
      if (input.quit_requested) throw std::runtime_error("visual fixture window quit during warm-up");
      stellar::native_map::DrawList draw;
      workspace.render(draw, width, height);
      for (const auto &command : draw.overlay)
        if (const auto *mesh = std::get_if<stellar::native_map::TriangleMesh>(&command))
          stellar::native_map::validate_triangle_mesh(*mesh);
      window.draw(draw, frame == 2 ? std::optional<std::filesystem::path>(args.output) : std::nullopt);
    }
    const auto diagnostics = workspace.scene_diagnostics();
    if (diagnostics.sites != 14 || diagnostics.meshes == 0 || diagnostics.triangles == 0 || diagnostics.road_segments == 0)
      throw std::runtime_error("visual fixture did not draw the complete surface scene");
    std::cout << "surface_visual fixture=presentation_only sites=" << diagnostics.sites
              << " meshes=" << diagnostics.meshes << " triangles=" << diagnostics.triangles
              << " road_segments=" << diagnostics.road_segments
              << " gpu_driver=" << window.gpu_driver() << "\n";
    return 0;
  } catch (const std::exception &error) {
    std::error_code cwd_error;
    const auto cwd = std::filesystem::current_path(cwd_error);
    std::cerr << "native surface visual fixture failed: " << error.what()
              << " cwd=" << (cwd_error ? "<unavailable>" : cwd.string())
              << " argc=" << argc << " args=";
    for (int i = 0; i < argc; ++i) std::cerr << (i == 0 ? "" : " ") << argv[i];
    std::cerr << "\n";
    return 1;
  }
}
