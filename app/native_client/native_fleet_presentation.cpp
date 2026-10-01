#include "native_fleet_presentation.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <ranges>
#include <cctype>

namespace stellar::native_fleet_ui {

std::string observer_safe_fleet_message(
    std::string_view message, std::span<const ObservedSystemName> systems,
    const stellar::engine::LocalizationTable *locale) {
  std::map<std::string, bool> unique_names;
  for (const auto &system : systems)
    if (!system.name.empty()) unique_names[system.name] |= system.known;
  struct NameVisibility {
    std::string_view name;
    bool known{};
  };
  std::vector<NameVisibility> names;
  names.reserve(unique_names.size());
  for (const auto &[name, known] : unique_names)
    names.push_back({name, known});
  std::ranges::sort(names, [](const auto &left, const auto &right) {
    if (left.name.size() != right.name.size())
      return left.name.size() > right.name.size();
    return left.name < right.name;
  });

  const auto token_byte = [](unsigned char value) {
    return std::isalnum(value) != 0 || value == '_' || value == '\'' ||
           value >= 0x80u;
  };

  std::string result;
  result.reserve(message.size());
  for (std::size_t index = 0; index < message.size();) {
    const auto matched = std::ranges::find_if(
        names, [&](const NameVisibility &candidate) {
          const auto name = candidate.name;
          if (index + name.size() > message.size() ||
              message.substr(index, name.size()) != name)
            return false;
          const auto left_boundary =
              index == 0 || !token_byte(static_cast<unsigned char>(message[index - 1]));
          const auto right_index = index + name.size();
          const auto right_boundary =
              right_index == message.size() ||
              !token_byte(static_cast<unsigned char>(message[right_index]));
          return left_boundary && right_boundary;
        });
    if (matched != names.end()) {
      result += matched->known
                    ? matched->name
                    : (locale && locale->contains("SYSTEM_NAME_UNKNOWN")
                           ? locale->translate("SYSTEM_NAME_UNKNOWN")
                           : "Unknown system");
      index += matched->name.size();
    } else {
      result.push_back(message[index]);
      ++index;
    }
  }
  return result;
}

std::vector<FleetMarkerOffset> deterministic_fleet_marker_offsets(
    std::span<const stellar::native_fleet::NativeOwnFleet> fleets) {
  std::vector<const stellar::native_fleet::NativeOwnFleet *> ordered;
  ordered.reserve(fleets.size());
  for (const auto &fleet : fleets) ordered.push_back(&fleet);
  std::ranges::sort(ordered, [](const auto *left, const auto *right) {
    return left->id < right->id;
  });

  std::map<std::pair<double, double>, std::size_t> position_counts;
  std::vector<FleetMarkerOffset> result;
  result.reserve(ordered.size());
  constexpr double golden_angle = 2.399963229728653;
  for (const auto *fleet : ordered) {
    const auto ordinal = position_counts[{fleet->position.x,
                                          fleet->position.y}]++;
    stellar::native_map::Point offset;
    if (ordinal > 0) {
      const auto radius = 10. * std::sqrt(static_cast<double>(ordinal));
      const auto angle = golden_angle * static_cast<double>(ordinal);
      offset = {static_cast<float>(std::cos(angle) * radius),
                static_cast<float>(std::sin(angle) * radius)};
    }
    result.push_back({fleet->id, offset});
  }
  return result;
}

} // namespace stellar::native_fleet_ui
