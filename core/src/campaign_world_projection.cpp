#include <stellar/core/campaign_world_projection.hpp>

#include <cstring>
#include <optional>
#include <stdexcept>

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

} // namespace

void register_campaign_world_components(engine::World &world) {
  register_tag<CampaignSystemTag>(world, "campaign.system");
  register_tag<CampaignBodyTag>(world, "campaign.body");
  register_tag<CampaignCivilizationTag>(world, "campaign.civilization");
  register_tag<CampaignColonyTag>(world, "campaign.colony");
  register_tag<CampaignFleetTag>(world, "campaign.fleet");
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
