#include <stellar/core/legacy_technology.hpp>

#include <algorithm>
#include <array>
#include <stdexcept>

namespace stellar::core {
namespace {

const std::array<TechnologyDefinition, 6> definitions{{
    {"orbital_industry",
     "Orbital Industry",
     "Large-scale orbital construction, automated fabrication, and sustained "
     "off-world infrastructure.",
     900.0,
     {},
     {"orbital_launch_complex"},
     TechnologyCategory::Industry},
    {"fusion_propulsion",
     "Fusion Propulsion",
     "High-efficiency fusion drives capable of sustained deep-space "
     "operations.",
     1300.0,
     {},
     {},
     TechnologyCategory::Propulsion},
    {"deep_space_sensors",
     "Deep-Space Sensor Networks",
     "Long-baseline arrays and autonomous observatories for detecting distant "
     "objects and field effects.",
     1100.0,
     {},
     {},
     TechnologyCategory::Sensors},
    {"exotic_field_theory",
     "Exotic Field Theory",
     "Experimental physics describing controllable spacetime and subspace "
     "field interactions.",
     2200.0,
     {"fusion_propulsion", "deep_space_sensors"},
     {},
     TechnologyCategory::Physics},
    {"warp_field_control",
     "Warp Field Control",
     "Stable laboratory-scale distortion fields and the control systems "
     "required to shape them.",
     3200.0,
     {"orbital_industry", "exotic_field_theory"},
     {},
     TechnologyCategory::Ftl},
    {"prototype_warp_drive",
     "Prototype Warp Drive",
     "A vessel-scale drive capable of sustained faster-than-light travel "
     "between star systems.",
     4800.0,
     {"warp_field_control"},
     {"warp_test_facility"},
     TechnologyCategory::Ftl},
}};

bool contains(std::span<const std::string> values, std::string_view value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

} // namespace

bool OrderedTechnologyIdSet::insert(std::string id) {
  if (contains(id))
    return false;
  values_.push_back(std::move(id));
  return true;
}

bool OrderedTechnologyIdSet::contains(std::string_view id) const {
  return std::find(values_.begin(), values_.end(), id) != values_.end();
}

std::span<const std::string> OrderedTechnologyIdSet::values() const noexcept {
  return values_;
}

std::span<const TechnologyDefinition> legacy_technology_catalog() {
  return definitions;
}

const TechnologyDefinition &get_legacy_technology(std::string_view id) {
  const auto found = std::find_if(
      definitions.begin(), definitions.end(),
      [id](const auto &definition) { return definition.id == id; });
  if (found == definitions.end())
    throw std::runtime_error("Sequence contains no matching element");
  return *found;
}

std::vector<TechnologyDefinition>
available_legacy_technologies(const TechnologyState &state,
                              const ConstructionState &construction) {
  std::vector<TechnologyDefinition> result;
  for (const auto &definition : definitions) {
    if (state.completed_technology_ids.contains(definition.id))
      continue;
    if (!std::all_of(definition.prerequisites.begin(),
                     definition.prerequisites.end(), [&](const auto &id) {
                       return state.completed_technology_ids.contains(id);
                     }))
      continue;
    if (!std::all_of(definition.required_projects.begin(),
                     definition.required_projects.end(), [&](const auto &id) {
                       return contains(construction.completed_project_ids, id);
                     }))
      continue;
    result.push_back(definition);
  }
  return result;
}

std::vector<TechnologyState>
seed_legacy_technologies(std::span<const Civilization> civilizations) {
  std::vector<TechnologyState> result;
  result.reserve(civilizations.size());
  for (const auto &civilization : civilizations) {
    TechnologyState state;
    state.civilization_id = civilization.id;
    if (civilization.development_stage ==
        CivilizationDevelopmentStage::AncientSpacefaring)
      for (const auto &definition : definitions)
        state.completed_technology_ids.insert(definition.id);
    result.push_back(std::move(state));
  }
  return result;
}

} // namespace stellar::core
