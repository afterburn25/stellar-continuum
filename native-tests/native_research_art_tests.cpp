#include "native_research_art.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
int main(int argc, char **argv) try {
  require(argc == 2, "Pass the artwork root.");
  const std::filesystem::path root(argv[1]);
  stellar::native_research_ui::NativeResearchArt art(root);
  require(!art.image("unrevealed-technology", false), "Unknown technology acquired art.");
  require(!art.image("sc.station.human_station_docking.l1", true), "Planned content acquired research art.");
  require(art.cached_bytes() == 0, "Catalog eagerly decoded artwork.");
  std::ifstream input(root / "assets/visual/catalog/catalog.json");
  const auto catalog = nlohmann::json::parse(input);
  std::set<std::string> families;
  for (const auto &record : catalog.at("research")) {
    const auto id = record.at("id").get<std::string>();
    const auto thumb = art.image(id, false);
    require(thumb && thumb->width() == 128 && thumb->height() == 128, "Thumbnail missing or invalid.");
    const auto before = art.decode_count();
    require(thumb == art.image(id, false) && before == art.decode_count(), "Artwork redecoded every lookup.");
    if (families.insert(record.at("art").get<std::string>()).second) {
      const auto portrait = art.image(id, true);
      require(portrait && portrait->width() == 512, "Portrait missing or invalid.");
    }
  }
  require(art.cached_bytes() == families.size() * 128u * 128u * 4u + 12u * 512u * 512u * 4u,
          "Research portrait cache is not bounded to 12 entries.");
  std::cout << "Research artwork: " << catalog.at("research").size() << " bindings, "
            << families.size() << " illustrations, bounded cache, no planned unlocks.\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << "Research artwork validation: " << error.what() << '\n';
  return 1;
}
