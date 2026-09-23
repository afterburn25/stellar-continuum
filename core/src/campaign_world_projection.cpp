#include <stellar/core/campaign_world_projection.hpp>

#include <cstring>
#include <optional>
#include <stdexcept>
#include <unordered_set>

namespace stellar::core {
namespace {

// POD tag serialization for snapshot codecs: same-build snapshots
// only — campaign authority persists through the Player17 path.
template <class T>
std::vector<std::uint8_t> encode_tag(const T &component) {
  std::vector<std::uint8_t> bytes(sizeof(T));
  std::memcpy(bytes.data(), &component, sizeof(T));
  return bytes;
}

template <class T>
T decode_tag(const std::vector<std::uint8_t> &bytes) {
  if (bytes.size() != sizeof(T))
    throw std::invalid_argument("campaign world tag size mismatch");
  T component;
  std::memcpy(&component, bytes.data(), sizeof(T));
  return component;
}

template <class T>
void register_tag(engine::World &world, const char *name) {
  world.register_component<T>(name, &encode_tag<T>, &decode_tag<T>);
}

// Parent links are best-effort over authoritative refs: absent or
// cyclic targets leave the entity unparented rather than failing the
// projection — the ref check is the invariant pass's job.
void try_parent(engine::World &world, engine::EntityId child,
                std::optional<engine::EntityId> parent) {
  if (!parent || *parent == child) return;
  try {
    world.set_parent(child, *parent);
  } catch (const std::invalid_argument &) {
  }
}

// Find-or-create by namespaced legacy id. Existing entities refresh
// their tag in place (sparse-set add overwrites); absent rows create
// and bind a fresh entity.
template <class Tag>
engine::EntityId upsert(engine::World &world, std::int64_t legacy_id,
                        const Tag &tag, bool &created) {
  if (const auto existing = world.entity_for_legacy(legacy_id)) {
    world.add(*existing, tag);
    created = false;
    return *existing;
  }
  const auto entity = world.create();
  world.bind_legacy(entity, legacy_id);
  world.add(entity, tag);
  created = true;
  return entity;
}

// Enforce the projection's parent contract on a surviving entity:
// unchanged parents are left alone, a nullopt desired parent leaves
// the entity unparented, and unresolvable/cyclic targets degrade to
// unparented via try_parent.
bool reparent(engine::World &world, engine::EntityId child,
              std::optional<engine::EntityId> desired) {
  if (world.parent(child) == desired) return false;
  world.clear_parent(child);
  try_parent(world, child, desired);
  return true;
}

} // namespace

void register_campaign_world_components(engine::World &world) {
  register_tag<CampaignSystemTag>(world, "campaign.system");
  register_tag<CampaignBodyTag>(world, "campaign.body");
  register_tag<CampaignCivilizationTag>(world, "campaign.civilization");
  register_tag<CampaignColonyTag>(world, "campaign.colony");
  register_tag<CampaignFleetTag>(world, "campaign.fleet");
  register_tag<CampaignEconomyTag>(world, "campaign.economy");
  register_tag<CampaignTechnologyTag>(world, "campaign.technology");
  register_tag<CampaignConstructionTag>(world, "campaign.construction");
  register_tag<CampaignShipyardTag>(world, "campaign.shipyard");
}

engine::World project_campaign_world(const FreshCampaignState &state) {
  engine::World world;
  const auto legacy = [](CampaignDomain domain, int id) {
    return campaign_legacy_id(domain, id);
  };
  const auto find = [&world](CampaignDomain domain, int id) {
    return world.entity_for_legacy(campaign_legacy_id(domain, id));
  };

  for (const auto &system : state.systems) {
    const auto entity = world.create();
    world.bind_legacy(entity, legacy(CampaignDomain::System, system.id));
    world.add(entity, CampaignSystemTag{system.id, system.position.x,
                                        system.position.y});
  }
  for (const auto &body : state.bodies) {
    const auto entity = world.create();
    world.bind_legacy(entity, legacy(CampaignDomain::Body, body.id));
    world.add(entity, CampaignBodyTag{body.id, body.system_id});
    // Moons parent to their host body when it resolves; planets and
    // unresolved refs parent to the system entity.
    if (body.parent_body_id)
      try_parent(world, entity,
                 find(CampaignDomain::Body, *body.parent_body_id));
    if (!world.parent(entity))
      try_parent(world, entity,
                 find(CampaignDomain::System, body.system_id));
  }
  for (const auto &civilization : state.civilizations) {
    const auto entity = world.create();
    world.bind_legacy(entity,
                      legacy(CampaignDomain::Civilization,
                             civilization.id));
    world.add(entity, CampaignCivilizationTag{
                          civilization.id, civilization.home_system_id});
  }
  // Civilization-owned state rows are 1:1 with their owner: the
  // legacy key reuses the civ id inside each domain's namespace and
  // the entity parents to its civilization when it resolves —
  // orphaned rows (absent civ) stay unparented for the invariant
  // pass to name.
  for (const auto &economy : state.economies) {
    const auto entity = world.create();
    world.bind_legacy(
        entity, legacy(CampaignDomain::Economy, economy.civilization_id));
    world.add(entity, CampaignEconomyTag{economy.civilization_id});
    try_parent(world, entity, find(CampaignDomain::Civilization,
                                   economy.civilization_id));
  }
  for (const auto &technology : state.technologies) {
    const auto entity = world.create();
    world.bind_legacy(entity, legacy(CampaignDomain::Technology,
                                     technology.civilization_id));
    world.add(entity, CampaignTechnologyTag{technology.civilization_id});
    try_parent(world, entity, find(CampaignDomain::Civilization,
                                   technology.civilization_id));
  }
  for (const auto &construction : state.construction) {
    const auto entity = world.create();
    world.bind_legacy(entity, legacy(CampaignDomain::Construction,
                                     construction.civilization_id));
    world.add(entity,
              CampaignConstructionTag{construction.civilization_id});
    try_parent(world, entity, find(CampaignDomain::Civilization,
                                   construction.civilization_id));
  }
  for (const auto &shipyard : state.shipyards) {
    const auto entity = world.create();
    world.bind_legacy(entity, legacy(CampaignDomain::Shipyard,
                                     shipyard.civilization_id));
    world.add(entity, CampaignShipyardTag{shipyard.civilization_id});
    try_parent(world, entity, find(CampaignDomain::Civilization,
                                   shipyard.civilization_id));
  }
  for (const auto &colony : state.colonies) {
    const auto entity = world.create();
    world.bind_legacy(entity, legacy(CampaignDomain::Colony, colony.id));
    world.add(entity, CampaignColonyTag{colony.id, colony.civilization_id,
                                        colony.system_id});
    // The occupied body parents the colony only when it resolves in
    // the colony's own system — the authoritative exact_body rule.
    if (colony.planetary_body_id)
      if (const auto body =
              find(CampaignDomain::Body, *colony.planetary_body_id)) {
        const auto *tag = world.get<CampaignBodyTag>(*body);
        if (tag && tag->system_id == colony.system_id)
          try_parent(world, entity, body);
      }
    if (!world.parent(entity))
      try_parent(world, entity,
                 find(CampaignDomain::System, colony.system_id));
  }
  for (const auto &fleet : state.fleets) {
    const auto entity = world.create();
    world.bind_legacy(entity, legacy(CampaignDomain::Fleet, fleet.id));
    world.add(entity,
              CampaignFleetTag{fleet.id, fleet.civilization_id});
    if (fleet.current_system_id)
      try_parent(world, entity,
                 find(CampaignDomain::System, *fleet.current_system_id));
  }
  return world;
}

CampaignWorldProjectionSync
sync_campaign_world(engine::World &world, const FreshCampaignState &state) {
  CampaignWorldProjectionSync result;
  std::unordered_set<std::int64_t> seen;
  const auto legacy = [](CampaignDomain domain, int id) {
    return campaign_legacy_id(domain, id);
  };
  const auto find = [&world](CampaignDomain domain, int id) {
    return world.entity_for_legacy(campaign_legacy_id(domain, id));
  };
  bool created = false;

  for (const auto &system : state.systems) {
    const auto key = legacy(CampaignDomain::System, system.id);
    seen.insert(key);
    const auto entity = upsert(
        world, key,
        CampaignSystemTag{system.id, system.position.x, system.position.y},
        created);
    created ? ++result.created : ++result.updated;
    reparent(world, entity, std::nullopt);
  }
  for (const auto &body : state.bodies) {
    const auto key = legacy(CampaignDomain::Body, body.id);
    seen.insert(key);
    const auto entity =
        upsert(world, key, CampaignBodyTag{body.id, body.system_id}, created);
    created ? ++result.created : ++result.updated;
    std::optional<engine::EntityId> desired;
    if (body.parent_body_id)
      desired = find(CampaignDomain::Body, *body.parent_body_id);
    if (!desired)
      desired = find(CampaignDomain::System, body.system_id);
    if (reparent(world, entity, desired)) ++result.reparented;
  }
  for (const auto &civilization : state.civilizations) {
    const auto key =
        legacy(CampaignDomain::Civilization, civilization.id);
    seen.insert(key);
    const auto entity =
        upsert(world, key,
               CampaignCivilizationTag{civilization.id,
                                       civilization.home_system_id},
               created);
    created ? ++result.created : ++result.updated;
    reparent(world, entity, std::nullopt);
  }
  for (const auto &economy : state.economies) {
    const auto key = legacy(CampaignDomain::Economy, economy.civilization_id);
    seen.insert(key);
    const auto entity = upsert(world, key,
                               CampaignEconomyTag{economy.civilization_id},
                               created);
    created ? ++result.created : ++result.updated;
    if (reparent(world, entity,
                 find(CampaignDomain::Civilization, economy.civilization_id)))
      ++result.reparented;
  }
  for (const auto &technology : state.technologies) {
    const auto key =
        legacy(CampaignDomain::Technology, technology.civilization_id);
    seen.insert(key);
    const auto entity =
        upsert(world, key, CampaignTechnologyTag{technology.civilization_id},
               created);
    created ? ++result.created : ++result.updated;
    if (reparent(world, entity, find(CampaignDomain::Civilization,
                                     technology.civilization_id)))
      ++result.reparented;
  }
  for (const auto &construction : state.construction) {
    const auto key =
        legacy(CampaignDomain::Construction, construction.civilization_id);
    seen.insert(key);
    const auto entity =
        upsert(world, key,
               CampaignConstructionTag{construction.civilization_id},
               created);
    created ? ++result.created : ++result.updated;
    if (reparent(world, entity, find(CampaignDomain::Civilization,
                                     construction.civilization_id)))
      ++result.reparented;
  }
  for (const auto &shipyard : state.shipyards) {
    const auto key = legacy(CampaignDomain::Shipyard, shipyard.civilization_id);
    seen.insert(key);
    const auto entity = upsert(world, key,
                               CampaignShipyardTag{shipyard.civilization_id},
                               created);
    created ? ++result.created : ++result.updated;
    if (reparent(world, entity, find(CampaignDomain::Civilization,
                                     shipyard.civilization_id)))
      ++result.reparented;
  }
  for (const auto &colony : state.colonies) {
    const auto key = legacy(CampaignDomain::Colony, colony.id);
    seen.insert(key);
    const auto entity =
        upsert(world, key,
               CampaignColonyTag{colony.id, colony.civilization_id,
                                 colony.system_id},
               created);
    created ? ++result.created : ++result.updated;
    std::optional<engine::EntityId> desired;
    if (colony.planetary_body_id)
      if (const auto body =
              find(CampaignDomain::Body, *colony.planetary_body_id)) {
        const auto *tag = world.get<CampaignBodyTag>(*body);
        if (tag && tag->system_id == colony.system_id) desired = body;
      }
    if (!desired)
      desired = find(CampaignDomain::System, colony.system_id);
    if (reparent(world, entity, desired)) ++result.reparented;
  }
  for (const auto &fleet : state.fleets) {
    const auto key = legacy(CampaignDomain::Fleet, fleet.id);
    seen.insert(key);
    const auto entity =
        upsert(world, key, CampaignFleetTag{fleet.id, fleet.civilization_id},
               created);
    created ? ++result.created : ++result.updated;
    std::optional<engine::EntityId> desired;
    if (fleet.current_system_id)
      desired = find(CampaignDomain::System, *fleet.current_system_id);
    if (reparent(world, entity, desired)) ++result.reparented;
  }

  // Rows that disappeared from the state retire their entity — only
  // campaign-namespaced bindings are swept; consumer-owned entities
  // with other (or no) legacy ids are left alone.
  for (const auto entity : world.entities()) {
    const auto bound = world.legacy_for(entity);
    if (!bound || seen.contains(*bound)) continue;
    const auto domain = static_cast<CampaignDomain>(*bound >> 32);
    if (domain < CampaignDomain::System || domain > CampaignDomain::Shipyard)
      continue;
    if (world.destroy(entity)) ++result.destroyed;
  }
  return result;
}

CampaignWorldProjectionCensus
campaign_world_projection_census(const FreshCampaignState &state) {
  auto world = project_campaign_world(state);
  CampaignWorldProjectionCensus census;
  census.entities = static_cast<int>(world.size());
  census.systems = static_cast<int>(world.view<CampaignSystemTag>().size());
  census.bodies = static_cast<int>(world.view<CampaignBodyTag>().size());
  census.civilizations =
      static_cast<int>(world.view<CampaignCivilizationTag>().size());
  census.colonies = static_cast<int>(world.view<CampaignColonyTag>().size());
  census.fleets = static_cast<int>(world.view<CampaignFleetTag>().size());
  census.economies =
      static_cast<int>(world.view<CampaignEconomyTag>().size());
  census.technologies =
      static_cast<int>(world.view<CampaignTechnologyTag>().size());
  census.construction =
      static_cast<int>(world.view<CampaignConstructionTag>().size());
  census.shipyards =
      static_cast<int>(world.view<CampaignShipyardTag>().size());
  for (const auto entity : world.entities()) {
    if (world.legacy_for(entity)) ++census.legacy_bound;
    if (world.parent(entity))
      ++census.parented;
    else
      ++census.unparented;
  }
  return census;
}

} // namespace stellar::core
