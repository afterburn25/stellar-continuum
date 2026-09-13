#include "galaxy_main.hpp"
#include "fresh_campaign_output.hpp"
#include "stellar/build_version.hpp"
#include <stellar/engine/runtime_paths.hpp>
#include <stellar/core/galaxy_catalog.hpp>
#include <stellar/core/planetary_catalog.hpp>
#include <stellar/core/species_environment.hpp>
#include <stellar/core/civilization_catalog.hpp>
#include <stellar/core/colony_economy.hpp>
#include <stellar/core/surface_economy.hpp>
#include <stellar/core/colony_biology.hpp>
#include <stellar/core/campaign_economy.hpp>
#include <stellar/core/construction_state.hpp>
#include <stellar/core/logistics.hpp>
#include <nlohmann/json.hpp>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
using namespace stellar::core;
using Json=nlohmann::json;
namespace {
std::int64_t signed_number(const std::string& text) {
    std::int64_t n{}; const auto parsed=std::from_chars(text.data(),text.data()+text.size(),n);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size()) throw std::invalid_argument("Expected a signed 64-bit numeric seed/count");
    return n;
}
Json system_json(const StellarSystem& s) {
    return {{"id",s.id},{"name",s.name},{"xLightYears",s.position.x},{"yLightYears",s.position.y},
        {"depthLightYears",s.position.depth_light_years},{"primaryClass",s.primary},{"secondaryClass",s.secondary},
        {"tertiaryClass",s.tertiary},{"catalogPresetId",s.catalog_preset_id},{"stellarCatalogId",s.stellar_catalog_id},
        {"archetype",s.archetype},{"hasHabitableWorld",s.has_habitable_world},{"hasAnomaly",s.has_anomaly},
        {"hasRareResource",s.has_rare_resource},{"hasPreWarpCivilization",s.has_pre_warp_civilization}};
}
Json body_json(const PlanetaryBody& b) {
    const auto& e=b.environment;
    return {{"id",b.id},{"systemId",b.system_id},{"parentBodyId",b.parent_body_id},{"orbitIndex",b.orbit_index},
        {"name",b.name},{"kind",b.kind},{"radiusEarth",b.radius_earth},{"massEarth",b.mass_earth},
        {"legacyColonizationCandidate",b.legacy_colonization_candidate},{"hasRareResource",b.has_rare_resource},
        {"hasAnomaly",b.has_anomaly},{"hasPreWarpCivilization",b.has_pre_warp_civilization},
        {"orbitalEccentricity",b.orbital_eccentricity},{"orbitalInclinationDegrees",b.orbital_inclination_degrees},
        {"environment",{{"gravityG",e.gravity_g},{"temperatureKelvin",e.temperature_kelvin},{"pressureKPa",e.pressure_kpa},
            {"atmosphere",e.atmosphere},{"availableSolvent",e.available_solvent},{"radiationHazard",e.radiation_hazard},
            {"isImmersedEnvironment",e.is_immersed_environment},{"hasSolidSurface",e.has_solid_surface}}}};
}
Json civilization_json(const Civilization& c) {
    const auto& t=c.traits; Json offices=Json::array();
    for(const auto& o:c.leadership) offices.push_back({{"office",o.office},{"characterId",o.character.id},
        {"displayName",o.character.display_name},{"voiceProfileId",o.character.voice_profile_id},{"portrait",o.character.portrait}});
    return {{"id",c.id},{"name",c.name},{"homeSystemId",c.home_system_id},{"speciesId",c.species_id},
        {"archetype",c.archetype},{"isPlayer",c.is_player},{"developmentStage",c.development_stage},
        {"isSeededAncient",c.is_seeded_ancient},{"expansionAllowed",c.expansion_allowed},
        {"neutralUnlessProvoked",c.neutral_unless_provoked},{"leadership",std::move(offices)},
        {"traits",{{"aggression",t.aggression},{"territoriality",t.territoriality},{"greed",t.greed},
            {"scientificCuriosity",t.scientific_curiosity},{"riskTolerance",t.risk_tolerance},
            {"survivalPriority",t.survival_priority},{"honorBound",t.honor_bound}}}};
}
Json colony_json(const Colony& c) {
    Json buildings=Json::array();
    for(const auto& b:c.surface_buildings) buildings.push_back({{"id",b.id},{"typeId",b.type_id},
        {"x",b.x},{"z",b.z},{"rotationDegrees",b.rotation_degrees},{"industryProgress",b.industry_progress},
        {"isComplete",b.is_complete},{"isEnabled",b.is_enabled},{"pendingUpgradeTypeId",b.pending_upgrade_type_id},
        {"upgradeDaysRemaining",b.upgrade_days_remaining},{"operatingPriority",b.operating_priority},
        {"condition",b.condition},{"storedPowerDays",b.stored_power_days}});
    return {{"id",c.id},{"civilizationId",c.civilization_id},{"systemId",c.system_id},{"planetaryBodyId",c.planetary_body_id},
        {"name",c.name},{"kind",c.kind},{"populationSpeciesId",c.population_species_id},{"populationMillions",c.population_millions},
        {"infrastructure",c.infrastructure},{"stability",c.stability},
        {"storedFoodPopulationDaysMillions",c.stored_food_population_days_millions},
        {"storedWaterPopulationDaysMillions",c.stored_water_population_days_millions},
        {"storedExtractedMaterials",c.stored_extracted_materials},{"remainingExtractableMaterials",c.remaining_extractable_materials},
        {"surfaceHubLevel",c.surface_hub_level},{"surfaceHubUpgradeDaysRemaining",c.surface_hub_upgrade_days_remaining},
        {"surfaceBuildings",std::move(buildings)}};
}
Json economy_json(const CivilizationEconomy& e) {
    return {{"civilizationId",e.civilization_id},{"credits",e.credits},{"industry",e.industry},{"science",e.science},
        {"lastCreditsPerSecond",e.last_credits_per_second},{"lastIndustryPerSecond",e.last_industry_per_second},
        {"lastSciencePerSecond",e.last_science_per_second},{"lastResearchSpendingPerDay",e.last_research_spending_per_day},
        {"lastResearchFundingFraction",e.last_research_funding_fraction},{"operatingArrears",e.operating_arrears},
        {"lastBaseOperationsFundingFraction",e.last_base_operations_funding_fraction},{"industryPriority",e.industry_priority}};
}
Json construction_state_json(const ConstructionState& state) {
    Json queued = Json::array();
    for (const auto& order : state.queued_projects)
        queued.push_back({{"projectId", order.project_id}, {"authorizationCredits", order.authorization_credits}});
    return {{"civilizationId", state.civilization_id}, {"completedProjectIds", state.completed_project_ids},
        {"activeProjectId", state.active_project_id}, {"activeProjectProgress", state.active_project_progress},
        {"activeProjectAuthorizationCredits", state.active_project_authorization_credits}, {"queuedProjects", std::move(queued)}};
}
Json surface_output_json(const SurfaceColonyOutput& output) {
    return {{"supply", output.supply}, {"demand", output.demand},
        {"sciencePerDay", output.science_per_day}, {"industryPerDay", output.industry_per_day},
        {"creditsPerDay", output.credits_per_day}, {"upkeepCreditsPerDay", output.upkeep_credits_per_day},
        {"poweredBuildingIds", output.powered_building_ids},
        {"habitatSupportReduction", output.habitat_support_reduction},
        {"foodCapacityMillions", output.food_capacity_millions}, {"waterCapacityMillions", output.water_capacity_millions},
        {"housingCapacityMillions", output.housing_capacity_millions},
        {"workforceAvailableMillions", output.workforce_available_millions},
        {"workforceDemandMillions", output.workforce_demand_millions}, {"staffedBuildingIds", output.staffed_building_ids},
        {"storedPowerDays", output.stored_power_days}, {"powerStorageCapacityDays", output.power_storage_capacity_days},
        {"storageChargePerDay", output.storage_charge_per_day}, {"storageDischargePerDay", output.storage_discharge_per_day},
        {"cargoTransferCapacityPerDay", output.cargo_transfer_capacity_per_day}};
}
Json sustenance_capacity_json(const ColonySustenanceCapacity& capacity) {
    return {{"naturalFoodCapacityMillions", capacity.natural_food_capacity_millions},
        {"naturalWaterCapacityMillions", capacity.natural_water_capacity_millions},
        {"naturalHousingCapacityMillions", capacity.natural_housing_capacity_millions},
        {"builtFoodCapacityMillions", capacity.built_food_capacity_millions},
        {"builtWaterCapacityMillions", capacity.built_water_capacity_millions},
        {"builtHousingCapacityMillions", capacity.built_housing_capacity_millions},
        {"foodCapacityMillions", capacity.food_capacity_millions}, {"waterCapacityMillions", capacity.water_capacity_millions},
        {"housingCapacityMillions", capacity.housing_capacity_millions},
        {"supportedPopulationMillions", capacity.supported_population_millions}, {"supportRatio", capacity.support_ratio},
        {"limitingSupply", capacity.limiting_supply}};
}
Json reserve_preview_json(const ColonySustenanceReserveSnapshot& reserves) {
    return {{"foodReserveDays", reserves.food_reserve_days}, {"waterReserveDays", reserves.water_reserve_days},
        {"effectiveSupportRatio", reserves.effective_support_ratio}, {"limitingSupply", reserves.limiting_supply}};
}
Json colony_support_json(const Colony& colony, std::span<const PlanetaryBody> bodies) {
    const auto surface = surface_colony_output(colony);
    const auto sustenance = colony_sustenance_capacity(bodies, colony, surface_sustenance_projection(surface));
    const auto habitat = colony_habitat_support(colony, bodies);
    const auto turnover = colony_population_turnover(colony, bodies);
    Json environment = nullptr;
    if (habitat.environment) { const auto& value=*habitat.environment; environment={{"planetaryBodyId",value.planetary_body_id},{"colonizationViability",value.colonization_viability},{"naturalHabitability",value.natural_habitability},{"unprotectedOperationalCapacity",value.unprotected_operational_capacity},{"limitingFactor",value.limiting_factor},{"requiredMitigationCategories",value.required_mitigation_categories},{"requiresGravityMitigation",value.requires_gravity_mitigation},{"requiresThermalControl",value.requires_thermal_control},{"requiresPressureControl",value.requires_pressure_control},{"requiresSealedHabitat",value.requires_sealed_habitat},{"requiresArtificialBiosphere",value.requires_artificial_biosphere},{"requiresRadiationShielding",value.requires_radiation_shielding},{"requiresAnyEnvironmentalMitigation",value.requires_any_environmental_mitigation},{"usesPrototypeHabitatSupportedFallback",value.uses_prototype_habitat_supported_fallback}}; }
    return {{"colonyId", colony.id}, {"surface", surface_output_json(surface)},
        {"sustenance", sustenance_capacity_json(sustenance)}, {"reserves", reserve_preview_json(preview_colony_reserves(colony, sustenance, 1.0))},
        {"habitat", {{"colonyId",habitat.colony_id},{"civilizationId",habitat.civilization_id},{"systemId",habitat.system_id},{"speciesId",habitat.species_id},{"populationMillions",habitat.population_millions},{"typicalDayMetabolicDemandMillions",habitat.typical_day_metabolic_demand_millions},{"adultBiomassMillionKg",habitat.adult_biomass_million_kg},{"environment",environment},{"usesExactOccupiedBody",habitat.uses_exact_occupied_body},{"requiresEnvironmentalSupport",habitat.requires_environmental_support},{"gravityMitigationPopulationMillions",habitat.gravity_mitigation_population_millions},{"thermalControlPopulationMillions",habitat.thermal_control_population_millions},{"pressureControlPopulationMillions",habitat.pressure_control_population_millions},{"sealedHabitatPopulationMillions",habitat.sealed_habitat_population_millions},{"artificialBiospherePopulationMillions",habitat.artificial_biosphere_population_millions},{"radiationShieldingPopulationMillions",habitat.radiation_shielding_population_millions}}},
        {"turnover", {{"colonyId",turnover.colony_id},{"speciesId",turnover.species_id},{"intrinsicGrowthPaceFactor",turnover.intrinsic_growth_pace_factor},{"usesExactOccupiedBody",turnover.uses_exact_occupied_body},{"planetaryBodyId",turnover.planetary_body_id},{"colonizationViability",turnover.colonization_viability},{"naturalHabitability",turnover.natural_habitability},{"naturalEnvironmentTurnoverFactor",turnover.natural_environment_turnover_factor},{"effectiveGrowthPaceFactor",turnover.effective_growth_pace_factor},{"limitingFactor",turnover.limiting_factor},{"requiresEnvironmentalSupport",turnover.requires_environmental_support},{"environmentalPressureApplied",turnover.environmental_pressure_applied}}}};
}
Json logistics_node_json(const LogisticsNode& value) { return {{"id",value.id},{"civilizationId",value.civilization_id},{"systemId",value.system_id},{"name",value.name},{"kind",value.kind}}; }
Json logistics_link_json(const LogisticsLink& value) { return {{"id",value.id},{"civilizationId",value.civilization_id},{"fromNodeId",value.from_node_id},{"toNodeId",value.to_node_id},{"capacityPerDay",value.capacity_per_day},{"transitDays",value.transit_days},{"bidirectional",value.bidirectional},{"enabled",value.enabled}}; }
Json quantity_json(const LogisticsNodeQuantity& value) { return {{"nodeId",value.node_id},{"perDay",value.per_day}}; }
Json offer_json(const LogisticsSupplyOffer& value) { return {{"nodeId",value.node_id},{"availablePerDay",value.available_per_day}}; }
Json demand_json(const LogisticsDemand& value) { return {{"nodeId",value.node_id},{"requiredPerDay",value.required_per_day},{"priority",value.priority}}; }
Json flow_json(const LogisticsFlowPlan& value) { Json allocations=Json::array(); for(const auto& item:value.allocations) allocations.push_back({{"sourceNodeId",item.source_node_id},{"destinationNodeId",item.destination_node_id},{"allocatedPerDay",item.allocated_per_day},{"transitDays",item.transit_days},{"routeLinkIds",item.route_link_ids}}); Json unmet=Json::array(); for(const auto& item:value.unmet_demand_per_day) unmet.push_back(quantity_json(item)); Json unused=Json::array(); for(const auto& item:value.unused_supply_per_day) unused.push_back(quantity_json(item)); return {{"allocations",allocations},{"unmetDemandPerDay",unmet},{"unusedSupplyPerDay",unused},{"totalAllocatedPerDay",value.total_allocated_per_day},{"totalUnmetDemandPerDay",value.total_unmet_demand_per_day}}; }
Json logistics_snapshot_json(const CivilizationLogisticsSnapshot& value) { Json colonies=Json::array(); for(const auto& item:value.colonies) colonies.push_back({{"colonyId",item.colony_id},{"systemId",item.system_id},{"supportDemandPerDay",item.support_demand_per_day},{"localSupportCapacityPerDay",item.local_support_capacity_per_day},{"importedSupportRequiredPerDay",item.imported_support_required_per_day},{"coverageRatio",item.coverage_ratio},{"condition",item.condition}}); return {{"civilizationId",value.civilization_id},{"totalSupportDemandPerDay",value.total_support_demand_per_day},{"totalLocalSupportCapacityPerDay",value.total_local_support_capacity_per_day},{"importRequirementPerDay",value.import_requirement_per_day},{"cargoHandlingCapacityPerDay",value.cargo_handling_capacity_per_day},{"effectiveCoverageRatio",value.effective_coverage_ratio},{"condition",value.condition},{"colonies",colonies},{"criticalColonyCount",value.critical_colony_count},{"strainedColonyCount",value.strained_colony_count}}; }
Json home_network_json(const HomeSystemLogisticsNetwork& value) { Json nodes=Json::array(),links=Json::array(),offers=Json::array(),demands=Json::array(); for(const auto& item:value.nodes) nodes.push_back(logistics_node_json(item)); for(const auto& item:value.links) links.push_back(logistics_link_json(item)); for(const auto& item:value.supply_offers) offers.push_back(offer_json(item)); for(const auto& item:value.demands) demands.push_back(demand_json(item)); return {{"civilizationId",value.civilization_id},{"homeSystemId",value.home_system_id},{"nodes",nodes},{"links",links},{"supplyOffers",offers},{"demands",demands},{"dailyFlow",flow_json(value.daily_flow)},{"totalDemandPerDay",value.total_demand_per_day},{"totalSupplyOfferedPerDay",value.total_supply_offered_per_day},{"totalAllocatedPerDay",value.total_allocated_per_day},{"totalUnmetDemandPerDay",value.total_unmet_demand_per_day}}; }
Json coverage_json(const CivilizationLogisticsCoverage& value) { Json external=Json::array(); for(const auto& item:value.external_systems) external.push_back({{"civilizationId",item.civilization_id},{"systemId",item.system_id},{"colonyCount",item.colony_count},{"supportDemandPerDay",item.support_demand_per_day},{"localSupportCapacityPerDay",item.local_support_capacity_per_day},{"importRequirementPerDay",item.import_requirement_per_day},{"localSurplusPerDay",item.local_surplus_per_day},{"condition",item.condition},{"hasRepresentedInterstellarFreightCorridor",item.has_represented_interstellar_freight_corridor}}); return {{"civilizationId",value.civilization_id},{"homeSystem",home_network_json(value.home_system)},{"externalSystems",external},{"ownedSystemCount",value.owned_system_count},{"externalSystemCount",value.external_system_count},{"externalImportRequirementPerDay",value.external_import_requirement_per_day},{"externalLocalSurplusPerDay",value.external_local_surplus_per_day},{"unrepresentedInterstellarSupportPerDay",value.unrepresented_interstellar_support_per_day},{"hasUnrepresentedInterstellarSupportGap",value.has_unrepresented_interstellar_support_gap}}; }
}
int run_galaxy_catalog(int argc,char** argv) {
    std::int64_t seed=8374837,count=500,repeats=1;
    std::int64_t pre_warp_count=6,ancient_count=1;
    bool plan_homes=false,found_civilizations=false,founding_options=false,constrained_fallback=false,seed_settlements=false;
    bool seed_campaign=false, preview_mode_requested=false;
    std::string player_species="terran_baseline";
    auto asset_root=stellar::engine::executable_directory(); std::filesystem::path output;
    for(int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        if(arg=="--generate-galaxy" || arg=="--headless") continue;
        if(arg=="--plan-homes") { plan_homes=true; preview_mode_requested=true; continue; }
        if(arg=="--found-civilizations") { found_civilizations=true; preview_mode_requested=true; continue; }
        if(arg=="--seed-colonies") { seed_settlements=true; found_civilizations=true; preview_mode_requested=true; continue; }
        if(arg=="--seed-campaign") { seed_campaign=true; found_civilizations=true; seed_settlements=true; continue; }
        if(i+1==argc) throw std::invalid_argument("Missing value for "+arg);
        const std::string value=argv[++i];
        if(arg=="--seed") seed=signed_number(value);
        else if(arg=="--systems") count=signed_number(value);
        else if(arg=="--repeat") repeats=signed_number(value);
        else if(arg=="--asset-root") asset_root=value;
        else if(arg=="--catalog-output") output=value;
        else if(arg=="--civilizations") { pre_warp_count=signed_number(value); founding_options=true; }
        else if(arg=="--ancients") { ancient_count=signed_number(value); founding_options=true; }
        else if(arg=="--player-species") { player_species=value; founding_options=true; }
        else throw std::invalid_argument("Unknown galaxy argument: "+arg);
    }
    if((count!=250 && count!=500 && count!=1000 && count!=2500) || repeats<1 || repeats>100)
        throw std::invalid_argument("Galaxy size must be 250, 500, 1000 or 2500; repeat must be 1..100");
    if(pre_warp_count<1||pre_warp_count>13||ancient_count<0||ancient_count>3)
        throw std::invalid_argument("Ordinary civilizations must be 1..13 and ancient civilizations 0..3");
    if(seed_campaign && preview_mode_requested)
        throw std::invalid_argument("Choose --seed-campaign or a catalog preview mode, not both");
    if(founding_options&&!found_civilizations) throw std::invalid_argument("Civilization options require --found-civilizations");
    if(plan_homes&&found_civilizations) throw std::invalid_argument("Choose --plan-homes or --found-civilizations; they are different migration stages");
    if(found_civilizations) (void)species_environment_profile(player_species);
    const auto input=std::filesystem::absolute(asset_root/"Data/astronomy/hyg-nearby-500-v1.json");
    const auto catalog=load_nearby_catalog(input);
    const auto start=std::chrono::steady_clock::now();
    FreshCampaignState campaign;
    auto& systems=campaign.systems;
    auto& bodies=campaign.bodies;
    std::vector<SpeciesHomeworldAssignment> homes;
    auto& civilizations=campaign.civilizations;
    auto& colonies=campaign.colonies;
    auto& economies=campaign.economies;
    auto& construction=campaign.construction;
    std::vector<EconomyConstructionState> economic_construction;
    for(std::int64_t i=0;i<repeats;++i) {
        if(seed_campaign) {
            campaign=seed_fresh_campaign(seed,catalog,static_cast<int>(count),
                static_cast<int>(pre_warp_count),static_cast<int>(ancient_count),player_species);
            constrained_fallback=campaign.used_constrained_home_fallback;
            homes.clear();
            for(const auto& c:civilizations) homes.push_back(resolve_species_homeworld(c.id,c.species_id,c.home_system_id,bodies));
            continue;
        }
        systems=generate_stellar_catalog(seed,static_cast<int>(count),catalog);
        if(found_civilizations) {
            auto founded=create_founding_catalog(seed,systems,static_cast<int>(pre_warp_count),static_cast<int>(ancient_count),player_species);
            systems=std::move(founded.systems); bodies=std::move(founded.bodies); civilizations=std::move(founded.civilizations);
            constrained_fallback=founded.used_constrained_home_fallback;
            homes.clear();
            for(const auto& c:civilizations) homes.push_back(resolve_species_homeworld(c.id,c.species_id,c.home_system_id,bodies));
            if(seed_settlements) { colonies=seed_colonies(civilizations,bodies); economies=seed_economies(civilizations); }
        } else bodies=generate_planetary_catalog(seed,systems);
        if(plan_homes) {
            std::vector<std::string> species;
            // The existing default full-galaxy profile seeds six ordinary factions and one ancient.
            for(int civilization=0;civilization<7;++civilization) species.push_back(assign_species(seed,civilization,true));
            homes=plan_species_homeworlds(systems,bodies,species);
        }
    }
    const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    if(seed_settlements) {
        if(!seed_campaign) construction=seed_construction(civilizations);
        economic_construction=economic_construction_projection(construction);
    }
    Json records=Json::array(); for(const auto& system:systems) records.push_back(system_json(system));
    Json planets=Json::array(),sol=Json::array();
    for(const auto& body:bodies) {
        planets.push_back(body_json(body));
        if(body.system_id==0) sol.push_back(body_json(body));
    }
    const auto core=seed_campaign ? campaign.core.value() : full_galaxy_core(static_cast<int>(count));
    Json home_records=Json::array();
    for(const auto& home:homes) home_records.push_back({{"civilizationId",home.civilization_id},{"speciesId",home.species_id},
        {"systemId",home.system_id},{"planetaryBodyId",home.planetary_body_id},{"naturalHabitability",home.natural_habitability},
        {"suitability",home.suitability}});
    Json civilization_records=Json::array(); for(const auto& c:civilizations) civilization_records.push_back(civilization_json(c));
    Json colony_records=Json::array(); for(const auto& c:colonies) colony_records.push_back(colony_json(c));
    Json economy_records=Json::array(); for(const auto& e:economies) economy_records.push_back(economy_json(e));
    Json construction_records=Json::array();
    if(seed_settlements) for(const auto& state:construction) construction_records.push_back(construction_state_json(state));
    Json colony_support=Json::array();
    for(const auto& colony:colonies) colony_support.push_back(colony_support_json(colony, bodies));
    Json logistics_preview=Json::array();
    if(seed_settlements) {
        const EconomyWorldView world{civilizations,bodies,economic_construction,{}};
        for(const auto& civilization:civilizations) {
            const auto coverage=civilization_logistics_coverage(world,colonies,economies,civilization.id);
            const auto logistics=economy_logistics(world,colonies,economies,civilization.id);
            logistics_preview.push_back({
            {"civilizationId",civilization.id},
            {"economyLogistics",logistics_snapshot_json(logistics)},
            {"coverage",coverage_json(coverage)},
            {"homeNetwork",home_network_json(coverage.home_system)}
            });
        }
    }
    Json snapshot={{"format",seed_settlements?"stellar-colony-catalog-v1":found_civilizations?"stellar-founding-catalog-v1":"stellar-physical-catalog-v1"},
        {"phase",seed_settlements?"colonies-before-fleets":found_civilizations?"founding-before-colonies":"physical-before-civilizations"},
        {"seed",seed},{"count",count},{"generatorVersion","full-galaxy-compact-v1"},
        {"settingsProfile","existing-full-galaxy-defaults"},
        {"homeworldPlanning",found_civilizations?"with-nearby-expansion":plan_homes?"normal-before-nearby-expansion":"not-requested"},
        {"usedConstrainedHomeFallback",constrained_fallback},{"civilizations",std::move(civilization_records)},
        {"colonies",std::move(colony_records)},{"economies",std::move(economy_records)},
        {"colonySupport",std::move(colony_support)},
        {"logisticsPreview",seed_settlements},{"logistics",std::move(logistics_preview)},
        {"homeworldPreview",std::move(home_records)},
        {"radiusLightYears",full_galaxy_radius(static_cast<int>(count))},
        {"core",{{"x",core.position.x},{"y",core.position.y},{"exclusionRadius",core.exclusion_radius}}},
        {"systems",std::move(records)},{"planetaryBodies",std::move(planets)},{"solBodies",std::move(sol)}};
    if(seed_settlements) snapshot["constructionStates"]=std::move(construction_records);
    if(seed_campaign) append_fresh_campaign_state(snapshot,campaign);
    if(!output.empty()) {
        auto pending=output; pending+=".pending";
        if(std::filesystem::exists(output) || std::filesystem::exists(pending)) throw std::runtime_error("Refusing to overwrite catalog output: "+output.string());
        { std::ofstream stream(pending); if(!stream) throw std::runtime_error("Cannot create catalog output: "+pending.string());
          stream<<snapshot.dump()<<'\n'; stream.flush(); if(!stream) throw std::runtime_error("Catalog output write failed: "+pending.string()); }
        std::filesystem::rename(pending,output);
    }
    Json report={{"mode",seed_campaign?"fresh-campaign":seed_settlements?"colony-catalog":found_civilizations?"founding-catalog":"physical-stellar-catalog"},{"engineVersion",STELLAR_ENGINE_VERSION},
        {"gameplayParity",false},{"sourceCommit",STELLAR_SOURCE_COMMIT},{"seed",seed},{"systems",count},
        {"measuredSystems",96},{"solBodies",snapshot.at("solBodies").size()},{"planetaryBodies",bodies.size()},{"repeats",repeats},
        {"normalHomeworldPlanning",plan_homes},{"plannedHomeworlds",homes.size()},
        {"foundingCivilizations",civilizations.size()},{"usedConstrainedHomeFallback",constrained_fallback},
        {"seededColonies",colonies.size()},{"seededEconomies",economies.size()},
        {"surfaceSupportPreview",seed_settlements},{"colonyBiologyPreview",seed_settlements},
        {"logisticsPreview",seed_settlements},
        {"elapsedMs",elapsed},{"meanGenerationMs",elapsed/static_cast<double>(repeats)},
        {"assetPath",input.string()},{"catalogOutput",output.string()}};
    if(seed_campaign) {
        report["freshCampaignInitialized"]=true;
        report["playerSaveCompatible"]=false;
        report["playerCivilizationId"]=campaign.player_civilization_id;
        report["seededFleets"]=campaign.fleets.size();
        report["seededTechnologies"]=campaign.technologies.size();
        report["seededShipyards"]=campaign.shipyards.size();
        report["seededKnowledgeObservers"]=snapshot["knowledge"].size();
    }
    std::cout<<report.dump()<<'\n';
    return 0;
}
