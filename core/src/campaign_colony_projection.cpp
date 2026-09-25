#include <stellar/core/campaign_colony_projection.hpp>

#include <stellar/core/surface_economy.hpp>

#include <algorithm>
#include <span>
#include <unordered_set>

namespace stellar::core {
namespace {

engine::StructureSpec spec_for(const SurfaceBuildingDefinition &def) {
  engine::StructureSpec spec;
  spec.id = "surface." + def.id;
  spec.category = std::string(surface_functional_family(def.id));
  if (def.industry_cost > 0.0)
    spec.build_cost.push_back({"industry", def.industry_cost});
  if (def.power_demand > 0.0)
    spec.utility_demand_per_day.push_back({"power", def.power_demand});
  if (def.power_supply > 0.0)
    spec.utility_supply_per_day.push_back({"power", def.power_supply});
  if (def.upkeep_credits_per_day > 0.0)
    spec.upkeep_per_day.push_back({"credits", def.upkeep_credits_per_day});
  if (def.science_per_day > 0.0)
    spec.outputs_per_day.push_back({"science", def.science_per_day});
  if (def.industry_per_day > 0.0)
    spec.outputs_per_day.push_back({"industry", def.industry_per_day});
  if (def.credits_per_day > 0.0)
    spec.outputs_per_day.push_back({"credits", def.credits_per_day});
  spec.jobs = def.workforce_required_millions;
  spec.housing = def.housing_capacity_millions;
  return spec;
}

engine::StructureSpec unknown_spec() {
  engine::StructureSpec spec;
  spec.id = "surface.unknown";
  spec.category = "unknown";
  return spec;
}

bool listed(std::span<const int> ids, int id) {
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

} // namespace

engine::Colony project_colony_settlement(const Colony &colony) {
  engine::Colony settlement;

  // Define specs for every observed type id first — restore_state
  // rejects instances whose spec id is unknown.
  std::unordered_set<std::string> defined;
  bool all_known = true;
  for (const auto &building : colony.surface_buildings) {
    const auto *def = find_surface_building(building.type_id);
    all_known = all_known && def != nullptr;
    const std::string spec_id =
        def ? "surface." + def->id : std::string("surface.unknown");
    if (!defined.insert(spec_id).second) continue;
    settlement.define_structure(def ? spec_for(*def) : unknown_spec());
  }

  // The authoritative power/staffing allocator throws on unknown type
  // ids; when it cannot run, no structure claims an operating result.
  std::vector<int> powered;
  if (all_known)
    powered = surface_colony_output(colony).powered_building_ids;

  const auto capacity = surface_building_capacity(colony);
  engine::Colony::State state;
  state.standalone_slots = static_cast<std::uint32_t>(std::max<int>(
      capacity, static_cast<int>(colony.surface_buildings.size())));
  for (const auto &building : colony.surface_buildings) {
    const auto *def = find_surface_building(building.type_id);
    engine::Colony::StructureState s;
    s.id = static_cast<std::uint64_t>(building.id);
    s.spec_id = def ? "surface." + def->id : std::string("surface.unknown");
    s.district_id = 0;
    s.complete = building.is_complete;
    s.construction_remaining =
        building.is_complete
            ? 0.0
            : std::max(0.0, (def ? def->industry_cost : 0.0) -
                                building.industry_progress);
    s.enabled = building.is_enabled;
    s.condition = building.condition;
    s.operating = listed(powered, building.id) ? 1.0 : 0.0;
    state.structures.push_back(std::move(s));
  }
  std::sort(state.structures.begin(), state.structures.end(),
            [](const auto &a, const auto &b) { return a.id < b.id; });
  settlement.restore_state(state);
  return settlement;
}

} // namespace stellar::core
