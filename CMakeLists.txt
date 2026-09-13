cmake_minimum_required(VERSION 3.28)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/export/runtime-config.json" STELLAR_VERSION_CONFIG)
string(JSON STELLAR_ENGINE_VERSION GET "${STELLAR_VERSION_CONFIG}" engineVersion)
string(JSON STELLAR_GAME_VERSION GET "${STELLAR_VERSION_CONFIG}" gameVersion)
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" STELLAR_GAME_NUMERIC_VERSION "${STELLAR_GAME_VERSION}")
string(REPLACE "." "," STELLAR_GAME_RESOURCE_VERSION "${STELLAR_GAME_NUMERIC_VERSION}")
project(StellarEngine VERSION ${STELLAR_ENGINE_VERSION} LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/W4 /permissive- /EHsc /fp:precise /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -ffp-contract=off)
endif()
find_package(Threads REQUIRED)
include(CTest)
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE STELLAR_SOURCE_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT STELLAR_SOURCE_COMMIT)
  set(STELLAR_SOURCE_COMMIT "unknown")
endif()
configure_file(app/build_version.hpp.in generated/stellar/build_version.hpp @ONLY)
add_library(stellar_engine STATIC engine/src/foundation.cpp engine/src/runtime_paths.cpp)
target_include_directories(stellar_engine PUBLIC engine/include)
target_link_libraries(stellar_engine PUBLIC Threads::Threads)
add_library(stellar_json INTERFACE)
target_include_directories(stellar_json SYSTEM INTERFACE third_party)
file(SHA256 "${CMAKE_SOURCE_DIR}/third_party/nlohmann/json.hpp" STELLAR_JSON_SHA256)
if(NOT STELLAR_JSON_SHA256 STREQUAL "aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63")
  message(FATAL_ERROR "Vendored nlohmann JSON differs from the reviewed 3.12.0 header; review and pin dependency changes explicitly")
endif()
add_library(stellar_core STATIC core/src/interstellar_distance.cpp core/src/legacy_random.cpp
  core/src/galaxy_catalog.cpp core/src/stellar_population.cpp core/src/stellar_traits.cpp core/src/planetary_catalog.cpp
  core/src/planetary_generation.cpp core/src/planetary_diversity.cpp
  core/src/species_environment.cpp core/src/species_homeworlds.cpp core/src/nearby_habitable.cpp
  core/src/civilization_catalog.cpp core/src/founding_catalog.cpp core/src/colony_economy.cpp core/src/colony_biology.cpp
  core/src/surface_economy.cpp core/src/colony_operations.cpp core/src/campaign_economy.cpp
  core/src/industry_allocation.cpp core/src/strategic_intent.cpp core/src/strategic_planning.cpp core/src/strategic_input_support.cpp core/src/strategic_input_builder.cpp core/src/strategic_runtime.cpp core/src/logistics_routing.cpp core/src/logistics_views.cpp
  core/src/sovereign_currency.cpp core/src/construction_state.cpp core/src/surface_construction.cpp
  core/src/construction_projects.cpp core/src/ship_designs.cpp
  core/src/fleet_state.cpp core/src/combat_state.cpp core/src/shipyard_state.cpp
  core/src/fleet_seeding.cpp core/src/shipbuilding.cpp core/src/fleet_transit.cpp
  core/src/lane_network.cpp core/src/fleet_reach.cpp core/src/knowledge.cpp core/src/legacy_technology.cpp core/src/fresh_campaign.cpp core/src/legacy_research.cpp core/src/freight.cpp core/src/settlement_knowledge.cpp core/src/settlement_planning.cpp
  core/src/civilian_recovery.cpp core/src/colonization_runtime.cpp core/src/survey_operations.cpp core/src/exploration_planning.cpp core/src/exploration_advance.cpp
  core/src/combat_simulation.cpp core/src/combat_command_runtime.cpp
  core/src/combat_readiness_detail.cpp core/src/own_combat_fleet_status.cpp
  core/src/campaign_coordinator.cpp core/src/coordinator_commands.cpp
  core/src/adaptive_research_catalog.cpp core/src/adaptive_research_applicability_catalog.cpp
  core/src/adaptive_research_facilities.cpp core/src/adaptive_research_progress_policy.cpp
  core/src/adaptive_research_state.cpp core/src/adaptive_research_eligibility.cpp
  core/src/adaptive_research_snapshot.cpp core/src/adaptive_research_view.cpp
  core/src/adaptive_research_runtime.cpp core/src/adaptive_research_expertise.cpp
  core/src/adaptive_research_expertise_state.cpp
  core/src/adaptive_research_readiness.cpp core/src/adaptive_research_expertise_service.cpp
  core/src/adaptive_research_starting_profiles.cpp
  core/src/adaptive_research_authority.cpp core/src/adaptive_research_expertise_snapshot.cpp
  core/src/adaptive_research_pressure.cpp core/src/adaptive_research_agenda.cpp)
target_include_directories(stellar_core PUBLIC core/include)
target_link_libraries(stellar_core PUBLIC stellar_engine PRIVATE stellar_json)
add_executable(stellar-continuum app/headless_main.cpp app/galaxy_main.cpp app/fresh_campaign_output.cpp)
target_include_directories(stellar-continuum PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(stellar-continuum PRIVATE stellar_core stellar_json)
add_custom_target(stellar_runtime_data ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:stellar-continuum>/Data/astronomy"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json" "$<TARGET_FILE_DIR:stellar-continuum>/Data/astronomy/hyg-nearby-500-v1.json"
  COMMENT "Synchronizing runtime stellar catalog" VERBATIM)
add_dependencies(stellar-continuum stellar_runtime_data)
if(WIN32)
  enable_language(RC)
  configure_file(app/windows_version.rc.in generated/windows_version.rc @ONLY)
  target_sources(stellar-continuum PRIVATE "${CMAKE_BINARY_DIR}/generated/windows_version.rc")
endif()
install(TARGETS stellar-continuum RUNTIME DESTINATION .)
install(FILES export/runtime-config.json DESTINATION Configuration)
install(FILES data/astronomy/hyg-nearby-500-v1.json data/astronomy/README.md DESTINATION Data/astronomy)
install(FILES third_party/nlohmann/LICENSE.MIT DESTINATION Licenses RENAME nlohmann-JSON-MIT.txt)
install(FILES third_party/dotnet/LICENSE.TXT DESTINATION Licenses RENAME dotnet-MIT.txt)
if(BUILD_TESTING)
  add_executable(stellar_own_combat_fleet_status_tests native-tests/own_combat_fleet_status_tests.cpp)
  target_link_libraries(stellar_own_combat_fleet_status_tests PRIVATE stellar_core stellar_json)
  add_test(NAME own_combat_fleet_status_parity COMMAND stellar_own_combat_fleet_status_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/own-combat-fleet-status.json")
  set_tests_properties(own_combat_fleet_status_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_combat_command_runtime_tests native-tests/combat_command_runtime_tests.cpp)
  target_link_libraries(stellar_combat_command_runtime_tests PRIVATE stellar_core stellar_json)
  add_test(NAME combat_command_runtime_parity COMMAND stellar_combat_command_runtime_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/combat-command-runtime.json")
  set_tests_properties(combat_command_runtime_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_colonization_runtime_tests native-tests/colonization_runtime_tests.cpp)
  target_link_libraries(stellar_colonization_runtime_tests PRIVATE stellar_core stellar_json)
  add_test(NAME colonization_runtime_parity COMMAND stellar_colonization_runtime_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/colonization-runtime.json")
  set_tests_properties(colonization_runtime_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_combat_simulation_tests native-tests/combat_simulation_tests.cpp)
  target_link_libraries(stellar_combat_simulation_tests PRIVATE stellar_core stellar_json)
  add_test(NAME combat_simulation_parity COMMAND stellar_combat_simulation_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/combat-simulation.json")
  set_tests_properties(combat_simulation_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_civilian_recovery_tests native-tests/civilian_recovery_tests.cpp)
  target_link_libraries(stellar_civilian_recovery_tests PRIVATE stellar_core stellar_json)
  add_test(NAME civilian_recovery_parity COMMAND stellar_civilian_recovery_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/civilian-recovery.json")
  add_executable(stellar_survey_operations_tests native-tests/survey_operations_tests.cpp)
  target_link_libraries(stellar_survey_operations_tests PRIVATE stellar_core stellar_json)
  add_test(NAME survey_operations_parity COMMAND stellar_survey_operations_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/survey-operations.json")
  add_executable(stellar_exploration_planning_tests native-tests/exploration_planning_tests.cpp)
  target_link_libraries(stellar_exploration_planning_tests PRIVATE stellar_core stellar_json)
  add_test(NAME exploration_planning_parity COMMAND stellar_exploration_planning_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/exploration-planning.json")
  set_tests_properties(civilian_recovery_parity survey_operations_parity exploration_planning_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_exploration_advance_tests native-tests/exploration_advance_tests.cpp)
  target_link_libraries(stellar_exploration_advance_tests PRIVATE stellar_core stellar_json)
  add_test(NAME exploration_advance_parity COMMAND stellar_exploration_advance_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/exploration-advance.json")
  set_tests_properties(exploration_advance_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_shipyard_state_tests native-tests/shipyard_state_tests.cpp)
  target_link_libraries(stellar_shipyard_state_tests PRIVATE stellar_core stellar_json)
  add_test(NAME shipyard_state_parity COMMAND stellar_shipyard_state_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/shipyard-state.json")
  set_tests_properties(shipyard_state_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_fleet_seeding_tests native-tests/fleet_seeding_tests.cpp)
  target_link_libraries(stellar_fleet_seeding_tests PRIVATE stellar_core stellar_json)
  add_test(NAME fleet_seeding_parity COMMAND stellar_fleet_seeding_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/fleet-seeding.json")
  set_tests_properties(fleet_seeding_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_shipbuilding_tests native-tests/shipbuilding_tests.cpp)
  target_link_libraries(stellar_shipbuilding_tests PRIVATE stellar_core stellar_json)
  add_test(NAME shipbuilding_parity COMMAND stellar_shipbuilding_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/shipbuilding.json")
  set_tests_properties(shipbuilding_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_fleet_transit_tests native-tests/fleet_transit_tests.cpp)
  target_link_libraries(stellar_fleet_transit_tests PRIVATE stellar_core stellar_json)
  add_test(NAME fleet_transit_parity COMMAND stellar_fleet_transit_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/fleet-transit.json")
  set_tests_properties(fleet_transit_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_lane_network_tests native-tests/lane_network_tests.cpp)
  target_link_libraries(stellar_lane_network_tests PRIVATE stellar_core stellar_json)
  add_test(NAME lane_network_parity COMMAND stellar_lane_network_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/lane-network.json")
  set_tests_properties(lane_network_parity PROPERTIES TIMEOUT 120)
  add_executable(stellar_fleet_reach_tests native-tests/fleet_reach_tests.cpp)
  target_link_libraries(stellar_fleet_reach_tests PRIVATE stellar_core stellar_json)
  add_test(NAME fleet_reach_parity COMMAND stellar_fleet_reach_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/operational-reach.json")
  set_tests_properties(fleet_reach_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_knowledge_tests native-tests/knowledge_tests.cpp)
  target_link_libraries(stellar_knowledge_tests PRIVATE stellar_core stellar_json)
  add_test(NAME knowledge_parity COMMAND stellar_knowledge_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/knowledge.json")
  set_tests_properties(knowledge_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_legacy_technology_tests native-tests/legacy_technology_tests.cpp)
  target_link_libraries(stellar_legacy_technology_tests PRIVATE stellar_core stellar_json)
  add_test(NAME legacy_technology_parity COMMAND stellar_legacy_technology_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/legacy-technology.json")
  set_tests_properties(legacy_technology_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_fresh_campaign_tests native-tests/fresh_campaign_tests.cpp)
  target_link_libraries(stellar_fresh_campaign_tests PRIVATE stellar_core stellar_json)
  add_test(NAME fresh_campaign_parity COMMAND stellar_fresh_campaign_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/fresh-campaign.json" "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json")
  set_tests_properties(fresh_campaign_parity PROPERTIES TIMEOUT 120)
  add_executable(stellar_freight_tests native-tests/freight_tests.cpp)
  target_link_libraries(stellar_freight_tests PRIVATE stellar_core stellar_json)
  add_test(NAME freight_parity COMMAND stellar_freight_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/freight.json")
  set_tests_properties(freight_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_settlement_knowledge_tests native-tests/settlement_knowledge_tests.cpp)
  target_link_libraries(stellar_settlement_knowledge_tests PRIVATE stellar_core stellar_json)
  add_test(NAME settlement_knowledge_parity COMMAND stellar_settlement_knowledge_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/settlement-knowledge.json")
  set_tests_properties(settlement_knowledge_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_settlement_planning_tests native-tests/settlement_planning_tests.cpp)
  target_link_libraries(stellar_settlement_planning_tests PRIVATE stellar_core stellar_json)
  add_test(NAME settlement_planning_parity COMMAND stellar_settlement_planning_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/settlement-planning.json")
  set_tests_properties(settlement_planning_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_legacy_research_tests native-tests/legacy_research_tests.cpp)
  target_link_libraries(stellar_legacy_research_tests PRIVATE stellar_core stellar_json)
  add_test(NAME legacy_research_parity COMMAND stellar_legacy_research_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/legacy-research.json")
  set_tests_properties(legacy_research_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_fleet_state_tests native-tests/fleet_state_tests.cpp)
  target_link_libraries(stellar_fleet_state_tests PRIVATE stellar_core stellar_json)
  add_test(NAME fleet_state_parity COMMAND stellar_fleet_state_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/fleet-state.json")
  set_tests_properties(fleet_state_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_ship_designs_tests native-tests/ship_designs_tests.cpp)
  target_link_libraries(stellar_ship_designs_tests PRIVATE stellar_core stellar_json)
  add_test(NAME ship_designs_parity COMMAND stellar_ship_designs_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/ship-designs.json")
  set_tests_properties(ship_designs_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_construction_projects_tests native-tests/construction_projects_tests.cpp)
  target_link_libraries(stellar_construction_projects_tests PRIVATE stellar_core stellar_json)
  add_test(NAME construction_projects_parity COMMAND stellar_construction_projects_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/construction-projects.json")
  set_tests_properties(construction_projects_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_surface_construction_tests native-tests/surface_construction_tests.cpp)
  target_link_libraries(stellar_surface_construction_tests PRIVATE stellar_core stellar_json)
  add_test(NAME surface_construction_parity COMMAND stellar_surface_construction_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/surface-construction.json")
  set_tests_properties(surface_construction_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_construction_currency_tests native-tests/construction_currency_tests.cpp)
  target_link_libraries(stellar_construction_currency_tests PRIVATE stellar_core stellar_json)
  add_test(NAME construction_currency_parity COMMAND stellar_construction_currency_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/construction-currency.json")
  set_tests_properties(construction_currency_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_foundation_tests native-tests/foundation_tests.cpp)
  target_link_libraries(stellar_foundation_tests PRIVATE stellar_engine)
  add_test(NAME foundation COMMAND stellar_foundation_tests)
  add_executable(stellar_distance_tests native-tests/distance_tests.cpp)
  target_link_libraries(stellar_distance_tests PRIVATE stellar_core)
  add_test(NAME legacy_distance_parity COMMAND stellar_distance_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/interstellar-distance.csv")
  add_test(NAME headless_smoke COMMAND stellar-continuum --headless --systems 500 --ticks 20 --workers 2)
  add_test(NAME campaign_simulation_smoke COMMAND stellar-continuum --headless
    --simulate-campaign --systems 250 --ticks 2 --step-days 0.25)
  add_test(NAME headless_rejects_bad_arguments COMMAND stellar-continuum --headless --systems -1)
  add_executable(stellar_sol_catalog_tests native-tests/sol_catalog_tests.cpp)
  target_link_libraries(stellar_sol_catalog_tests PRIVATE stellar_core)
  add_test(NAME sol_catalog COMMAND stellar_sol_catalog_tests)
  add_executable(stellar_galaxy_catalog_tests native-tests/galaxy_catalog_tests.cpp)
  target_link_libraries(stellar_galaxy_catalog_tests PRIVATE stellar_core stellar_json)
  add_test(NAME galaxy_catalog_parity COMMAND stellar_galaxy_catalog_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/galaxy-catalog.json"
    "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json")
  set_tests_properties(galaxy_catalog_parity sol_catalog PROPERTIES TIMEOUT 60)
  add_executable(stellar_species_tests native-tests/species_tests.cpp)
  target_link_libraries(stellar_species_tests PRIVATE stellar_core stellar_json)
  add_test(NAME species_parity COMMAND stellar_species_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/species-environment.json")
  set_tests_properties(species_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_civilization_tests native-tests/civilization_tests.cpp)
  target_link_libraries(stellar_civilization_tests PRIVATE stellar_core stellar_json)
  add_test(NAME civilization_parity COMMAND stellar_civilization_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/civilization-catalog.json")
  set_tests_properties(civilization_parity PROPERTIES TIMEOUT 120)
  add_executable(stellar_colony_economy_tests native-tests/colony_economy_tests.cpp)
  target_link_libraries(stellar_colony_economy_tests PRIVATE stellar_core stellar_json)
  add_test(NAME colony_economy_parity COMMAND stellar_colony_economy_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/colony-economy.json")
  set_tests_properties(colony_economy_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_surface_economy_tests native-tests/surface_economy_tests.cpp)
  target_link_libraries(stellar_surface_economy_tests PRIVATE stellar_core stellar_json)
  add_test(NAME surface_economy_parity COMMAND stellar_surface_economy_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/surface-economy.json")
  set_tests_properties(surface_economy_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_colony_biology_tests native-tests/colony_biology_tests.cpp)
  target_link_libraries(stellar_colony_biology_tests PRIVATE stellar_core stellar_json)
  add_test(NAME colony_biology_parity COMMAND stellar_colony_biology_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/colony-biology.json")
  set_tests_properties(colony_biology_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_colony_operations_tests native-tests/colony_operations_tests.cpp)
  target_link_libraries(stellar_colony_operations_tests PRIVATE stellar_core stellar_json)
  add_test(NAME colony_operations_parity COMMAND stellar_colony_operations_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/colony-operations.json")
  add_executable(stellar_campaign_economy_tests native-tests/campaign_economy_tests.cpp)
  target_link_libraries(stellar_campaign_economy_tests PRIVATE stellar_core stellar_json)
  add_test(NAME campaign_economy_parity COMMAND stellar_campaign_economy_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/funded-economy.json")
  set_tests_properties(colony_operations_parity campaign_economy_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_industry_allocation_tests native-tests/industry_allocation_tests.cpp)
  target_link_libraries(stellar_industry_allocation_tests PRIVATE stellar_core stellar_json)
  add_test(NAME industry_allocation_parity COMMAND stellar_industry_allocation_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/industry-allocation.json")
  add_executable(stellar_strategic_intent_tests native-tests/strategic_intent_tests.cpp)
  target_link_libraries(stellar_strategic_intent_tests PRIVATE stellar_core)
  add_test(NAME strategic_intent_parity COMMAND stellar_strategic_intent_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/strategic-intent.tsv")
  add_executable(stellar_strategic_input_support_tests native-tests/strategic_input_support_tests.cpp)
  target_link_libraries(stellar_strategic_input_support_tests PRIVATE stellar_core stellar_json)
  add_test(NAME strategic_input_support_parity COMMAND stellar_strategic_input_support_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/strategic-input-support.json")
  add_executable(stellar_strategic_input_builder_tests native-tests/strategic_input_builder_tests.cpp)
  target_link_libraries(stellar_strategic_input_builder_tests PRIVATE stellar_core stellar_json)
  add_test(NAME strategic_input_builder_parity COMMAND stellar_strategic_input_builder_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/strategic-input-builder.json")
  add_executable(stellar_strategic_runtime_tests native-tests/strategic_runtime_tests.cpp)
  target_link_libraries(stellar_strategic_runtime_tests PRIVATE stellar_core stellar_json)
  add_test(NAME strategic_runtime_parity COMMAND stellar_strategic_runtime_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/strategic-runtime.json")
  add_executable(stellar_campaign_coordinator_tests native-tests/campaign_coordinator_tests.cpp)
  target_link_libraries(stellar_campaign_coordinator_tests PRIVATE stellar_core stellar_json)
  add_test(NAME campaign_coordinator_parity COMMAND stellar_campaign_coordinator_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/campaign-step.json")
  add_executable(stellar_campaign_command_tests native-tests/campaign_command_tests.cpp)
  target_link_libraries(stellar_campaign_command_tests PRIVATE stellar_core stellar_json)
  add_test(NAME campaign_command_parity COMMAND stellar_campaign_command_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/campaign-command.json")
  set_tests_properties(campaign_command_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_adaptive_research_catalog_tests native-tests/adaptive_research_catalog_tests.cpp)
  target_link_libraries(stellar_adaptive_research_catalog_tests PRIVATE stellar_core stellar_json)
  add_test(NAME adaptive_research_catalog_parity COMMAND stellar_adaptive_research_catalog_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-catalog.json"
    "${CMAKE_SOURCE_DIR}/data/research/v1")
  set_tests_properties(adaptive_research_catalog_parity PROPERTIES TIMEOUT 120)
  if(WIN32)
    add_executable(stellar_adaptive_research_applicability_tests native-tests/adaptive_research_applicability_tests.cpp)
    target_link_libraries(stellar_adaptive_research_applicability_tests PRIVATE stellar_core stellar_json)
    target_link_libraries(stellar_adaptive_research_applicability_tests PRIVATE bcrypt)
    add_test(NAME adaptive_research_applicability_parity COMMAND stellar_adaptive_research_applicability_tests
      "${CMAKE_SOURCE_DIR}/data/research/v1"
      "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-applicability.json")
    set_tests_properties(adaptive_research_applicability_parity PROPERTIES TIMEOUT 120)
  endif()
  add_executable(stellar_adaptive_research_policy_tests native-tests/adaptive_research_policy_tests.cpp)
  target_link_libraries(stellar_adaptive_research_policy_tests PRIVATE stellar_core stellar_json)
  add_test(NAME adaptive_research_policy_parity COMMAND stellar_adaptive_research_policy_tests
    "${CMAKE_SOURCE_DIR}/data/research/v1"
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-policy.json")
  set_tests_properties(adaptive_research_policy_parity PROPERTIES TIMEOUT 120)
  add_executable(stellar_adaptive_research_state_tests native-tests/adaptive_research_state_tests.cpp)
  target_link_libraries(stellar_adaptive_research_state_tests PRIVATE stellar_core stellar_json)
  add_test(NAME adaptive_research_state_parity COMMAND stellar_adaptive_research_state_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-state.json")
  set_tests_properties(adaptive_research_state_parity PROPERTIES TIMEOUT 60)
  add_executable(stellar_adaptive_research_snapshot_tests native-tests/adaptive_research_snapshot_tests.cpp)
  target_link_libraries(stellar_adaptive_research_snapshot_tests PRIVATE stellar_core stellar_json)
  add_test(NAME adaptive_research_snapshot_parity COMMAND stellar_adaptive_research_snapshot_tests
    "${CMAKE_SOURCE_DIR}/data/research/v1"
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-snapshot.json")
  set_tests_properties(adaptive_research_snapshot_parity PROPERTIES TIMEOUT 120)
  add_executable(stellar_adaptive_research_eligibility_tests native-tests/adaptive_research_eligibility_tests.cpp)
  target_link_libraries(stellar_adaptive_research_eligibility_tests PRIVATE stellar_core stellar_json)
  add_test(NAME adaptive_research_eligibility_parity COMMAND stellar_adaptive_research_eligibility_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-eligibility.json"
    "${CMAKE_SOURCE_DIR}/data/research/v1")
  set_tests_properties(adaptive_research_eligibility_parity PROPERTIES TIMEOUT 120)
  add_executable(stellar_adaptive_research_view_tests native-tests/adaptive_research_view_tests.cpp)
  target_link_libraries(stellar_adaptive_research_view_tests PRIVATE stellar_core stellar_json)
  add_test(NAME adaptive_research_view_parity COMMAND stellar_adaptive_research_view_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-view.json"
    "${CMAKE_SOURCE_DIR}/data/research/v1")
  set_tests_properties(adaptive_research_view_parity PROPERTIES TIMEOUT 120)
  if(WIN32)
    add_executable(stellar_adaptive_research_runtime_tests native-tests/adaptive_research_runtime_tests.cpp)
    target_link_libraries(stellar_adaptive_research_runtime_tests PRIVATE stellar_core stellar_json bcrypt)
    add_test(NAME adaptive_research_runtime_parity COMMAND stellar_adaptive_research_runtime_tests
      "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-runtime.json"
      "${CMAKE_SOURCE_DIR}/data/research/v1")
    set_tests_properties(adaptive_research_runtime_parity PROPERTIES TIMEOUT 180)
  endif()
  add_executable(stellar_adaptive_research_expertise_tests native-tests/adaptive_research_expertise_tests.cpp)
  target_link_libraries(stellar_adaptive_research_expertise_tests PRIVATE stellar_core stellar_json)
  add_test(NAME adaptive_research_expertise_parity COMMAND stellar_adaptive_research_expertise_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-expertise.json"
    "${CMAKE_SOURCE_DIR}/data/research/v1")
  set_tests_properties(adaptive_research_expertise_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_adaptive_research_readiness_tests native-tests/adaptive_research_readiness_tests.cpp)
  target_link_libraries(stellar_adaptive_research_readiness_tests PRIVATE stellar_core stellar_json)
  add_test(NAME adaptive_research_readiness_parity COMMAND stellar_adaptive_research_readiness_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-readiness.json"
    "${CMAKE_SOURCE_DIR}/data/research/v1")
  set_tests_properties(adaptive_research_readiness_parity PROPERTIES TIMEOUT 180)
  if(WIN32)
    add_executable(stellar_research_start_tests native-tests/adaptive_research_starting_profiles_tests.cpp)
    target_link_libraries(stellar_research_start_tests PRIVATE stellar_core stellar_json bcrypt)
    add_test(NAME adaptive_research_starting_profiles_parity COMMAND stellar_research_start_tests
      "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-starting-profiles.json"
      "${CMAKE_SOURCE_DIR}/data/research/v1")
    set_tests_properties(adaptive_research_starting_profiles_parity PROPERTIES TIMEOUT 180)
  endif()
  add_executable(stellar_research_authority_tests native-tests/adaptive_research_authority_tests.cpp)
  target_link_libraries(stellar_research_authority_tests PRIVATE stellar_core stellar_json)
  add_test(NAME adaptive_research_authority_parity COMMAND stellar_research_authority_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-authority.json"
    "${CMAKE_SOURCE_DIR}/data/research/v1")
  set_tests_properties(adaptive_research_authority_parity PROPERTIES TIMEOUT 180)
  if(WIN32)
    add_executable(stellar_research_v2_tests native-tests/adaptive_research_expertise_snapshot_tests.cpp)
    target_link_libraries(stellar_research_v2_tests PRIVATE stellar_core stellar_json bcrypt)
    add_test(NAME adaptive_research_expertise_snapshot_parity COMMAND stellar_research_v2_tests
      "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-expertise-snapshot.json"
      "${CMAKE_SOURCE_DIR}/data/research/v1")
    set_tests_properties(adaptive_research_expertise_snapshot_parity PROPERTIES TIMEOUT 180)
  endif()
  add_executable(stellar_research_pressure_tests native-tests/adaptive_research_pressure_tests.cpp)
  target_link_libraries(stellar_research_pressure_tests PRIVATE stellar_core stellar_json)
  add_test(NAME adaptive_research_pressure_parity COMMAND stellar_research_pressure_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-pressure.json"
    "${CMAKE_SOURCE_DIR}/data/research/v1")
  set_tests_properties(adaptive_research_pressure_parity PROPERTIES TIMEOUT 180)
  if(WIN32)
    add_executable(stellar_research_agenda_tests native-tests/adaptive_research_agenda_tests.cpp)
    target_link_libraries(stellar_research_agenda_tests PRIVATE stellar_core stellar_json bcrypt)
    add_test(NAME adaptive_research_agenda_parity COMMAND stellar_research_agenda_tests
      "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-agenda.json"
      "${CMAKE_SOURCE_DIR}/data/research/v1")
    set_tests_properties(adaptive_research_agenda_parity PROPERTIES TIMEOUT 180)
  endif()
  add_executable(stellar_strategic_planning_tests native-tests/strategic_planning_tests.cpp)
  target_link_libraries(stellar_strategic_planning_tests PRIVATE stellar_core stellar_json)
  add_test(NAME strategic_planning_parity COMMAND stellar_strategic_planning_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/strategic-planning.json")
  add_executable(stellar_logistics_routing_tests native-tests/logistics_routing_tests.cpp)
  target_link_libraries(stellar_logistics_routing_tests PRIVATE stellar_core stellar_json)
  add_test(NAME logistics_routing_parity COMMAND stellar_logistics_routing_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/logistics-routing.json")
  add_executable(stellar_logistics_views_tests native-tests/logistics_views_tests.cpp)
  target_link_libraries(stellar_logistics_views_tests PRIVATE stellar_core stellar_json)
  add_test(NAME logistics_views_parity COMMAND stellar_logistics_views_tests "${CMAKE_SOURCE_DIR}/native-tests/fixtures/logistics-views.json")
  set_tests_properties(industry_allocation_parity strategic_intent_parity strategic_planning_parity strategic_input_support_parity strategic_input_builder_parity strategic_runtime_parity campaign_coordinator_parity logistics_routing_parity logistics_views_parity PROPERTIES TIMEOUT 60)
  set_tests_properties(headless_rejects_bad_arguments PROPERTIES WILL_FAIL TRUE)
  set_tests_properties(foundation legacy_distance_parity headless_smoke campaign_simulation_smoke headless_rejects_bad_arguments PROPERTIES TIMEOUT 30)
endif()
