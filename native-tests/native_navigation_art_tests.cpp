#include "native_navigation_art.hpp"
#include <array>
#include <filesystem>
#include <iostream>
#include <ranges>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
}

int main(int argc, char **argv) try {
  require(argc == 2, "Pass the repository asset root.");
  stellar::native_navigation::NativeNavigationArt art{std::filesystem::path(argv[1])};
  using stellar::native_map::UiAction;
  const std::array actions{UiAction::Map, UiAction::Home, UiAction::Inspect,
      UiAction::ZoomIn, UiAction::ZoomOut, UiAction::Economy, UiAction::Research,
      UiAction::Shipyard, UiAction::Construction, UiAction::Explore,
      UiAction::Colonies, UiAction::Supply, UiAction::Diplomacy, UiAction::Menu};
  std::size_t resident_bytes{};
  for (auto action : actions) {
    const auto image = art.image(action);
    require(image && image->width() == 256 && image->height() == 256 &&
                image->byte_size() == 256u * 256u * 4u,
            "Navigation icon dimensions or storage are wrong.");
    require(art.image(action) == image, "Navigation image was recreated on lookup.");
    bool has_transparent{}, has_opaque{};
    std::size_t visible_pixels{};
    for (std::size_t i = 3; i < image->pixels().size(); i += 4) {
      const auto alpha = image->pixels()[i];
      has_transparent = has_transparent || alpha == 0;
      has_opaque = has_opaque || alpha == 255;
      visible_pixels += alpha != 0;
    }
    require(has_transparent && has_opaque && visible_pixels > 1000,
            "Navigation symbol lost transparent padding or visible artwork.");
    for (auto other : actions)
      if (other != action)
        require(art.image(other) != image &&
                    !std::ranges::equal(art.image(other)->pixels(),image->pixels()),
                "Different navigation actions share the same photo thumbnail.");
    resident_bytes += image->byte_size();
  }
  require(resident_bytes == 14u * 256u * 256u * 4u, "Navigation symbol cache budget changed.");
  require(!art.image(UiAction::Pause) && !art.image(UiAction::None),
          "Unsupported navigation action returned artwork.");
  std::cout << "Native navigation symbols preserve transparency, immutable cache mappings and 3.5 MiB budget.\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "Native navigation validation failed: " << error.what() << '\n';
  return 1;
}
