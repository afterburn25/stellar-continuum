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
  const std::array actions{UiAction::Research, UiAction::Shipyard,
                           UiAction::Construction, UiAction::Diplomacy};
  std::size_t resident_bytes{};
  for (auto action : actions) {
    const auto image = art.image(action);
    require(image && image->width() == 256 && image->height() == 256 &&
                image->byte_size() == 256u * 256u * 4u,
            "Navigation icon dimensions or storage are wrong.");
    require(art.image(action) == image, "Navigation image was recreated on lookup.");
    bool transparent{}, visible{}, antialiased{};
    for (std::size_t i = 3; i < image->pixels().size(); i += 4) {
      const auto alpha = image->pixels()[i];
      transparent |= alpha == 0;
      visible |= alpha == 255;
      antialiased |= alpha > 0 && alpha < 255;
    }
    require(transparent && visible && antialiased,
            "Navigation icon lost its transparent, antialiased silhouette.");
    for (auto other : actions)
      if (other != action)
        require(art.image(other) != image &&
                    !std::ranges::equal(art.image(other)->pixels(),image->pixels()),
                "Different navigation actions share the same artwork.");
    resident_bytes += image->byte_size();
  }
  require(resident_bytes == 1024u * 1024u, "Navigation resident image budget changed.");
  require(!art.image(UiAction::Pause) && !art.image(UiAction::None),
          "Unsupported navigation action returned artwork.");
  std::cout << "Native navigation images preserve identity, alpha, mappings and 1 MiB budget.\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "Native navigation validation failed: " << error.what() << '\n';
  return 1;
}
