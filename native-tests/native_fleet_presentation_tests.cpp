#include "native_fleet_presentation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace stellar::native_fleet;
using namespace stellar::native_fleet_ui;

namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

[[nodiscard]] NativeOwnFleet fleet(int id, double x, double y) {
  NativeOwnFleet result;
  result.id = id;
  result.position = {static_cast<float>(x), static_cast<float>(y)};
  return result;
}

[[nodiscard]] bool same_offsets(std::span<const FleetMarkerOffset> left,
                                std::span<const FleetMarkerOffset> right) {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index)
    if (left[index].fleet_id != right[index].fleet_id ||
        left[index].pixels.x != right[index].pixels.x ||
        left[index].pixels.y != right[index].pixels.y)
      return false;
  return true;
}
} // namespace

int main() try {
  const std::array systems{
      ObservedSystemName{1, "Sol", false},
      ObservedSystemName{2, "Solaris", true},
      ObservedSystemName{3, "Secret Junction", false},
      ObservedSystemName{4, "Barnard's Star", true},
      ObservedSystemName{5, "éèæ", false}};
  const auto accepted = observer_safe_fleet_message(
      "Route accepted for Sol through Solaris to Barnard's Star.", systems);
  require(accepted ==
              "Route accepted for Unknown system through Solaris to "
              "Barnard's Star.",
          "Unknown target redaction damaged a longer known shared prefix.");
  const auto denied = observer_safe_fleet_message(
      "Fuel is insufficient at Secret Junction; Secret Junction is beyond "
      "range, while Solaris remains reachable.",
      systems);
  require(denied ==
              "Fuel is insufficient at Unknown system; Unknown system is "
              "beyond range, while Solaris remains reachable.",
          "Unknown intermediate redaction missed repetition or a known name.");
  NativeFleetRoutePreview canonical_preview;
  canonical_preview.message = "Travel to Sol via Secret Junction.";
  auto ui_preview = canonical_preview;
  ui_preview.message = observer_safe_fleet_message(ui_preview.message, systems);
  require(canonical_preview.message == "Travel to Sol via Secret Junction." &&
              ui_preview.message ==
                  "Travel to Unknown system via Unknown system.",
          "Sanitizing the UI preview altered the raw canonical route command.");
  const auto boundaries = observer_safe_fleet_message(
      "Solar probes are not Sol, and éèæ is hidden.", systems);
  require(boundaries ==
              "Solar probes are not Unknown system, and Unknown system is "
              "hidden.",
          "System-name redaction ignored token boundaries or UTF-8 names.");

  std::vector<NativeOwnFleet> first{
      fleet(12, 4, 7), fleet(10, 4, 7), fleet(14, 9, 3), fleet(11, 4, 7)};
  auto second = first;
  std::ranges::reverse(second);
  const auto offsets = deterministic_fleet_marker_offsets(first);
  const auto reversed = deterministic_fleet_marker_offsets(second);
  require(same_offsets(offsets, reversed) && offsets.size() == 4 &&
              std::ranges::is_sorted(offsets, {}, &FleetMarkerOffset::fleet_id),
          "Fleet marker offsets changed with input ordering.");
  require(offsets[0].fleet_id == 10 && offsets[0].pixels.x == 0.f &&
              offsets[0].pixels.y == 0.f &&
              std::hypot(offsets[1].pixels.x, offsets[1].pixels.y) > 0.f &&
              std::hypot(offsets[2].pixels.x, offsets[2].pixels.y) > 0.f &&
              offsets[3].fleet_id == 14 && offsets[3].pixels.x == 0.f &&
              offsets[3].pixels.y == 0.f,
          "Co-located fleets did not receive stable distinct offsets.");

  std::cout << "Native fleet observer-safe messages and stable grouped marker "
               "offsets passed\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
