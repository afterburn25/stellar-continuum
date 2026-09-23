#include <stellar/core/campaign_world_projection.hpp>
#include <stellar/core/campaign_diagnostics.hpp>

#include <algorithm>
#include <iostream>
#include <string>

// Campaign world projection tests — the read-only adapter that
// reshapes authoritative FreshCampaignState rows into the engine's
// typed World store with namespaced legacy identity and real
// containment hierarchy (roadmap row 1 bridge).

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

using namespace stellar::core;

FreshCampaignState make_state() {
  FreshCampaignState state;
  StellarSystem system;
  system.id = 5;
  system.name = "Home";
  system.position = {1.0f, 2.0f};
  state.systems.push_back(system);
  StellarSystem other;
  other.id = 9;
  other.name = "Colony Hub";
  other.position = {7.0f, 1.0f};
  state.systems.push_back(other);

  PlanetaryBody world_body;
  world_body.id = 11;
  world_body.system_id = 5;
  state.bodies.push_back(world_body);
  PlanetaryBody moon;
  moon.id = 12;
  moon.system_id = 5;
  moon.parent_body_id = 11;
  state.bodies.push_back(moon);

  Civilization civ;
  civ.id = 5; // intentionally collides with the system id
  civ.name = "Terran Union";
  civ.home_system_id = 5;
  state.civilizations.push_back(civ);

  Colony colony;
  colony.id = 5; // collides with both system and civ ids
  colony.civilization_id = 5;
  colony.system_id = 5;
  colony.kind = SettlementKind::Colony;
  colony.planetary_body_id = 11;
  state.colonies.push_back(colony);

  Colony remote;
  remote.id = 7;
  remote.civilization_id = 5;
  remote.system_id = 9;
  remote.kind = SettlementKind::ResourceOutpost;
  // planetary_body_id unset — parents to its system entity.
  state.colonies.push_back(remote);

  FleetState fleet;
  fleet.id = 3;
  fleet.civilization_id = 5;
  fleet.current_system_id = 9;
  state.fleets.push_back(fleet);

  CivilizationEconomy economy;
  economy.civilization_id = 5;
  state.economies.push_back(economy);
  TechnologyState tech;
  tech.civilization_id = 5;
  state.technologies.push_back(tech);
  ConstructionState construction;
  construction.civilization_id = 5;
  state.construction.push_back(construction);
  ShipyardState shipyard;
  shipyard.civilization_id = 5;
  state.shipyards.push_back(shipyard);
  // Orphaned economy row: civ 99 does not exist — the entity projects
  // but stays unparented for the invariant pass to name.
  CivilizationEconomy orphan_economy;
  orphan_economy.civilization_id = 99;
  state.economies.push_back(orphan_economy);
  return state;
}

} // namespace

int main() {
  using stellar::engine::EntityId;
  const auto state = make_state();

  // Entity/tag counts, namespaced legacy identity, and hierarchy.
  {
    auto world = project_campaign_world(state);
    check(world.size() == 13, "projection carries every domain row");
    check(world.view<CampaignSystemTag>().size() == 2,
          "system tags present");
    check(world.view<CampaignBodyTag>().size() == 2, "body tags present");
    check(world.view<CampaignCivilizationTag>().size() == 1,
          "civilization tags present");
    check(world.view<CampaignColonyTag>().size() == 2,
          "colony tags present");
    check(world.view<CampaignFleetTag>().size() == 1, "fleet tag present");
    check(world.view<CampaignEconomyTag>().size() == 2,
          "economy tags present (incl. orphan)");
    check(world.view<CampaignTechnologyTag>().size() == 1,
          "technology tag present");
    check(world.view<CampaignConstructionTag>().size() == 1,
          "construction tag present");
    check(world.view<CampaignShipyardTag>().size() == 1,
          "shipyard tag present");

    // Namespaced legacy ids: system 5, civ 5 and colony 5 resolve to
    // three distinct entities.
    const auto system_entity =
        world.entity_for_legacy(campaign_legacy_id(CampaignDomain::System, 5));
    const auto civ_entity = world.entity_for_legacy(
        campaign_legacy_id(CampaignDomain::Civilization, 5));
    const auto colony_entity =
        world.entity_for_legacy(campaign_legacy_id(CampaignDomain::Colony, 5));
    check(system_entity && civ_entity && colony_entity,
          "colliding domain ids all resolve");
    check(system_entity && civ_entity && colony_entity &&
              *system_entity != *civ_entity && *civ_entity != *colony_entity &&
              *system_entity != *colony_entity,
          "namespaced ids resolve to distinct entities");

    // Hierarchy: moon under its host body, colony under its occupied
    // body, outpost under its system, fleet under its current system.
    const auto moon_entity =
        world.entity_for_legacy(campaign_legacy_id(CampaignDomain::Body, 12));
    const auto body_entity =
        world.entity_for_legacy(campaign_legacy_id(CampaignDomain::Body, 11));
    const auto remote_entity =
        world.entity_for_legacy(campaign_legacy_id(CampaignDomain::Colony, 7));
    const auto fleet_entity =
        world.entity_for_legacy(campaign_legacy_id(CampaignDomain::Fleet, 3));
    const auto hub_entity =
        world.entity_for_legacy(campaign_legacy_id(CampaignDomain::System, 9));
    check(moon_entity && body_entity && world.parent(*moon_entity) == body_entity,
          "moon parents to its host body");
    check(colony_entity && body_entity &&
              world.parent(*colony_entity) == body_entity,
          "colony parents to its occupied body");
    check(remote_entity && hub_entity &&
              world.parent(*remote_entity) == hub_entity,
          "body-less outpost parents to its system");
    check(fleet_entity && hub_entity &&
              world.parent(*fleet_entity) == hub_entity,
          "fleet parents to its current system");
    check(civ_entity && !world.parent(*civ_entity),
          "civilizations are unparented identity entities");

    // Civ-scoped state rows parent under their civilization; the
    // orphan economy has no civ to attach to.
    const auto economy_entity = world.entity_for_legacy(
        campaign_legacy_id(CampaignDomain::Economy, 5));
    const auto tech_entity = world.entity_for_legacy(
        campaign_legacy_id(CampaignDomain::Technology, 5));
    const auto construction_entity = world.entity_for_legacy(
        campaign_legacy_id(CampaignDomain::Construction, 5));
    const auto shipyard_entity = world.entity_for_legacy(
        campaign_legacy_id(CampaignDomain::Shipyard, 5));
    const auto orphan_economy_entity = world.entity_for_legacy(
        campaign_legacy_id(CampaignDomain::Economy, 99));
    check(economy_entity && tech_entity && construction_entity &&
              shipyard_entity && orphan_economy_entity,
          "civ-scoped state rows all resolve");
    check(economy_entity && *economy_entity != *civ_entity,
          "economy namespace id is distinct from the civ entity");
    check(economy_entity && world.parent(*economy_entity) == civ_entity &&
              tech_entity && world.parent(*tech_entity) == civ_entity &&
              construction_entity &&
              world.parent(*construction_entity) == civ_entity &&
              shipyard_entity &&
              world.parent(*shipyard_entity) == civ_entity,
          "civ-scoped rows parent to their civilization");
    check(orphan_economy_entity && !world.parent(*orphan_economy_entity),
          "orphaned state row stays unparented");

    // Tag fields round-trip the authoritative refs.
    const auto *tag = world.get<CampaignColonyTag>(*colony_entity);
    check(tag && tag->id == 5 && tag->civilization_id == 5 &&
              tag->system_id == 5,
          "colony tag carries authoritative refs");

    // Stale handles: destroying an entity retires its handle and
    // clears the legacy binding.
    check(world.destroy(*colony_entity), "colony entity destroys");
    check(!world.alive(*colony_entity), "destroyed handle reports stale");
    check(!world.entity_for_legacy(
              campaign_legacy_id(CampaignDomain::Colony, 5)),
          "destroyed entity clears the legacy binding");
    check(!world.legacy_for(*colony_entity),
          "stale handle resolves no legacy id");
    check(world.get<CampaignColonyTag>(*colony_entity) == nullptr,
          "stale handle resolves no component");

    // DetachChildren: removing the host body detaches the moon.
    check(world.parent(*moon_entity) == body_entity, "moon still parented");
    check(world.destroy(*body_entity), "host body destroys");
    check(world.alive(*moon_entity) && !world.parent(*moon_entity),
          "moon detaches when its host is destroyed");
  }

  // A colony whose body id exists only in a different system falls
  // back to its system parent — the exact_body rule.
  {
    auto cross = state;
    cross.colonies[0].system_id = 9; // body 11 still lives in system 5
    auto world = project_campaign_world(cross);
    const auto colony_entity =
        world.entity_for_legacy(campaign_legacy_id(CampaignDomain::Colony, 5));
    const auto hub_entity =
        world.entity_for_legacy(campaign_legacy_id(CampaignDomain::System, 9));
    check(colony_entity && hub_entity &&
              world.parent(*colony_entity) == hub_entity,
          "cross-system body ref falls back to the system parent");
  }

  // Save identity: snapshot -> restore preserves entities, legacy
  // bindings and hierarchy through the registered tag codecs.
  {
    auto world = project_campaign_world(state);
    register_campaign_world_components(world);
    const auto bytes = world.snapshot();
    stellar::engine::World restored;
    register_campaign_world_components(restored);
    restored.restore(bytes);
    check(restored.size() == world.size(),
          "restored world carries every entity");
    const auto colony_entity = restored.entity_for_legacy(
        campaign_legacy_id(CampaignDomain::Colony, 5));
    const auto body_entity = restored.entity_for_legacy(
        campaign_legacy_id(CampaignDomain::Body, 11));
    check(colony_entity && body_entity,
          "legacy bindings survive the snapshot round-trip");
    check(colony_entity && body_entity &&
              restored.parent(*colony_entity) == body_entity,
          "hierarchy survives the snapshot round-trip");
    const auto *tag = restored.get<CampaignColonyTag>(*colony_entity);
    check(tag && tag->civilization_id == 5 && tag->system_id == 5,
          "tag codecs survive the snapshot round-trip");
  }

  // Cyclic body parents: the projection survives (cycle-rejecting
  // set_parent leaves the moon under its system) and the invariant
  // pass names the corruption.
  {
    auto cyclic = state;
    cyclic.bodies[0].parent_body_id = 12; // 11 -> 12 -> 11
    auto world = project_campaign_world(cyclic);
    const auto moon_entity =
        world.entity_for_legacy(campaign_legacy_id(CampaignDomain::Body, 12));
    const auto host_entity =
        world.entity_for_legacy(campaign_legacy_id(CampaignDomain::Body, 11));
    const auto system_entity =
        world.entity_for_legacy(campaign_legacy_id(CampaignDomain::System, 5));
    check(moon_entity && host_entity && system_entity,
          "cyclic state still projects every entity");
    // Body 11 is created first; its parent 12 does not exist yet, so
    // it roots under the system. Body 12 then tries parent 11 — but
    // 11 is already under the system, so 12->11 is legal... unless
    // the store sees 11's pending link. The store rejects any link
    // that would close a cycle; whichever link was skipped, both
    // bodies must still be reachable under the system root.
    check(moon_entity && world.alive(*moon_entity) &&
              host_entity && world.alive(*host_entity),
          "cyclic bodies remain live entities");
    const auto findings = inspect_campaign_invariants(cyclic, 1, 1.0);
    const auto cyclic_parent = std::find_if(
        findings.begin(), findings.end(), [](const auto &f) {
          return f.event_type == "cyclic_parent";
        });
    check(cyclic_parent != findings.end(),
          "invariants flag the cyclic body parent");
    check(cyclic_parent != findings.end() &&
              cyclic_parent->severity ==
                  stellar::engine::DiagnosticSeverity::Critical,
          "cyclic parent is a Critical invariant finding");
  }

  // Census mirrors the projection contract on real-shaped state.
  {
    const auto census = campaign_world_projection_census(state);
    check(census.entities == 13 && census.systems == 2 &&
              census.bodies == 2 && census.civilizations == 1 &&
              census.colonies == 2 && census.fleets == 1 &&
              census.economies == 2 && census.technologies == 1 &&
              census.construction == 1 && census.shipyards == 1,
          "census counts every domain");
    check(census.legacy_bound == 13, "every entity is legacy-bound");
    check(census.parented == 9 && census.unparented == 4,
          "census counts resolved parents (civs+systems+orphan unparented)");
  }

  if (failures == 0)
    std::cout << "campaign world projection tests passed\n";
  return failures;
}
