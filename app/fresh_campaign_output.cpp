#include "fresh_campaign_output.hpp"

using namespace stellar::core;
using Json = nlohmann::json;
namespace {
Json vector_json(Vec2 value) { return {{"x", value.x}, {"y", value.y}}; }

Json combat_json(const std::optional<FleetCombatState>& state) {
    if (!state) return nullptr;
    const auto& v = *state;
    return {{"profileId",v.profile_id},{"shields",v.shields},{"armor",v.armor},{"hull",v.hull},
        {"weaponCooldownRemainingDays",v.weapon_cooldown_remaining_days},{"order",v.order},
        {"targetFleetId",v.target_fleet_id},{"defendSystemId",v.defend_system_id},
        {"retreatProgressDays",v.retreat_progress_days},{"retreatStarted",v.retreat_started},
        {"isDisengaged",v.is_disengaged},{"disengagedSystemId",v.disengaged_system_id}};
}

Json loadout_json(const std::optional<MassiveCombatLoadout>& state) {
    if (!state) return nullptr;
    const auto& v = *state;
    Json weapons=Json::array(), modules=Json::array();
    for (const auto& w:v.weapons) weapons.push_back({{"id",w.id},{"kind",w.kind},
        {"mountsPerShip",w.mounts_per_ship},{"damagePerShot",w.damage_per_shot},
        {"shotsPerSecond",w.shots_per_second},{"range",w.range},{"accuracy",w.accuracy},
        {"powerPerSecond",w.power_per_second},{"heatPerSecond",w.heat_per_second}});
    for (const auto& m:v.modules) modules.push_back({{"id",m.id},{"kind",m.kind},
        {"installedCount",m.installed_count},{"massEach",m.mass_each},
        {"powerPerSecondEach",m.power_per_second_each},{"heatPerSecondEach",m.heat_per_second_each},
        {"condition",m.condition},{"enabled",m.enabled},{"effectiveRange",m.effective_range},
        {"fieldStrength",m.field_strength},{"detectionSignature",m.detection_signature},{"slots",m.slots}});
    return {{"massPerShip",v.mass_per_ship},{"acceleration",v.acceleration},{"maximumSpeed",v.maximum_speed},
        {"shieldPerShip",v.shield_per_ship},{"armorPerShip",v.armor_per_ship},{"hullPerShip",v.hull_per_ship},
        {"reactorOutputPerShip",v.reactor_output_per_ship},{"coolingPerShip",v.cooling_per_ship},
        {"warpStabilization",v.warp_stabilization},{"warpSpoolSeconds",v.warp_spool_seconds},
        {"moduleSlotCapacity",v.module_slot_capacity},{"maximumModuleMass",v.maximum_module_mass},
        {"weapons",std::move(weapons)},{"modules",std::move(modules)}};
}

Json vessel_json(const std::optional<MassiveVesselState>& state) {
    if (!state) return nullptr;
    const auto& v=*state;
    return {{"id",v.id},{"name",v.name},{"designId",v.design_id},{"isFlagship",v.is_flagship},
        {"isCarrier",v.is_carrier},{"isInterdictor",v.is_interdictor},{"isStoryShip",v.is_story_ship},
        {"hullFraction",v.hull_fraction},{"engineFraction",v.engine_fraction},{"sensorFraction",v.sensor_fraction},
        {"warpDriveFraction",v.warp_drive_fraction},{"reactorFraction",v.reactor_fraction},
        {"interdictorFraction",v.interdictor_fraction},{"battlesFought",v.battles_fought},
        {"confirmedKills",v.confirmed_kills},{"destroyed",v.destroyed},{"escaped",v.escaped}};
}

Json fleet_json(const FleetState& v) {
    return {{"id",v.id},{"civilizationId",v.civilization_id},{"name",v.name},{"role",v.role},
        {"designId",v.design_id},{"position",vector_json(v.position)},
        {"currentSystemId",v.current_system_id},{"destinationSystemId",v.destination_system_id},
        {"transitPhase",v.transit_phase},{"transitOriginSystemId",v.transit_origin_system_id},
        {"transitTargetSystemId",v.transit_target_system_id},{"transitProgress",v.transit_progress},
        {"localTransitStart",vector_json(v.local_transit_start)},
        {"localTransitPosition",vector_json(v.local_transit_position)},
        {"localTransitTarget",vector_json(v.local_transit_target)},
        {"plannedRouteSystemIds",v.planned_route_system_ids},{"holdRequested",v.hold_requested},
        {"returnToBaseRequested",v.return_to_base_requested},{"returnToBaseFailureReason",v.return_to_base_failure_reason},
        {"missionOrderRevision",v.mission_order_revision},{"destinationPlanetaryBodyId",v.destination_planetary_body_id},
        {"preventAutomaticSettlement",v.prevent_automatic_settlement},{"settlementBodyId",v.settlement_body_id},
        {"settlementDaysCompleted",v.settlement_days_completed},{"reconnaissanceSystemId",v.reconnaissance_system_id},
        {"reconnaissanceDaysCompleted",v.reconnaissance_days_completed},{"freightTargetOutpostId",v.freight_target_outpost_id},
        {"freightHomeColonyId",v.freight_home_colony_id},{"cargoMaterialCapacity",v.cargo_material_capacity},
        {"cargoMaterials",v.cargo_materials},{"strategicSpeed",v.strategic_speed},
        {"maximumLegRangeLightYears",v.maximum_leg_range_light_years},{"fuelCapacityLightYears",v.fuel_capacity_light_years},
        {"fuelRemainingLightYears",v.fuel_remaining_light_years},{"sensorRange",v.sensor_range},{"isActive",v.is_active},
        {"embarkedPopulationMillions",v.embarked_population_millions},{"embarkedPopulationSpeciesId",v.embarked_population_species_id},
        {"combat",combat_json(v.combat)},{"tacticalLoadout",loadout_json(v.tactical_loadout)},
        {"tacticalVessel",vessel_json(v.tactical_vessel)}};
}

Json shipyard_json(const ShipyardState& v) {
    Json queue=Json::array();
    for (const auto& q:v.queued_builds) queue.push_back({{"orderId",q.order_id},{"designId",q.design_id},
        {"authorizationCredits",q.authorization_credits},{"reservedPopulationMillions",q.reserved_population_millions},
        {"reservedPopulationSpeciesId",q.reserved_population_species_id},{"reservedPopulationSourceColonyId",q.reserved_population_source_colony_id}});
    return {{"civilizationId",v.civilization_id},{"nextOrderSequence",v.next_order_sequence},
        {"activeDesignId",v.active_design_id},{"activeOrderId",v.active_order_id},{"activeBuildProgress",v.active_build_progress},
        {"activeAuthorizationCredits",v.active_authorization_credits},{"reservedPopulationMillions",v.reserved_population_millions},
        {"reservedPopulationSpeciesId",v.reserved_population_species_id},{"reservedPopulationSourceColonyId",v.reserved_population_source_colony_id},
        {"queuedBuilds",std::move(queue)}};
}
}

void append_fresh_campaign_state(Json& output, const FreshCampaignState& campaign) {
    output["format"]="stellar-fresh-campaign-v1";
    output["phase"]="fresh-campaign-before-first-tick";
    output["gameplayParity"]=false;
    output["playerSaveCompatible"]=false;
    output["playerCivilizationId"]=campaign.player_civilization_id;
    output["fleets"]=Json::array();
    for (const auto& v:campaign.fleets) output["fleets"].push_back(fleet_json(v));
    output["technologies"]=Json::array();
    for (const auto& v:campaign.technologies) {
        Json completed=Json::array();
        for (const auto& id:v.completed_technology_ids.values()) completed.push_back(id);
        output["technologies"].push_back({{"civilizationId",v.civilization_id},
            {"completedTechnologyIds",std::move(completed)},{"activeResearchId",v.active_research_id},
            {"activeResearchProgress",v.active_research_progress}});
    }
    output["shipyards"]=Json::array();
    for (const auto& v:campaign.shipyards) output["shipyards"].push_back(shipyard_json(v));
    output["knowledge"]=Json::array();
    for (const auto& c:campaign.civilizations) {
        Json surveys=Json::array();
        for (const auto& s:campaign.knowledge.system_survey_knowledge(c.id))
            surveys.push_back({{"systemId",s.system_id},{"level",s.level},{"progress",s.progress}});
        output["knowledge"].push_back({{"civilizationId",c.id},
            {"knownSystems",campaign.knowledge.known_systems(c.id)},
            {"knownCivilizations",campaign.knowledge.known_civilizations(c.id)},
            {"surveys",std::move(surveys)},{"coreAccess",campaign.knowledge.has_galactic_core_access(c.id)},
            {"coreDiscovered",campaign.knowledge.is_galactic_core_discovered(c.id)}});
    }
    output["coreObservers"]=campaign.knowledge.galactic_core_observers();
}
