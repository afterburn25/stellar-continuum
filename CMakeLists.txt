cmake_minimum_required(VERSION 3.28)
# Keep each object's debug data self-contained: parallel native builds must not
# depend on a shared compiler PDB surviving reconfiguration. Linker PDBs remain.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
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
  core/src/adaptive_research_pressure.cpp core/src/adaptive_research_agenda.cpp
  core/src/adaptive_research_foreign_technology.cpp
  core/src/adaptive_research_outcomes.cpp core/src/adaptive_research_sha256.cpp
  core/src/adaptive_research_foreign_discovery.cpp
  core/src/adaptive_research_strategic_runtime.cpp
  core/src/adaptive_research_strategic_snapshot.cpp
  core/src/adaptive_research_foreign_technology_snapshot.cpp
  core/src/adaptive_research_outcome_snapshot.cpp
  core/src/adaptive_research_campaign.cpp
  core/src/adaptive_research_funding.cpp
  core/src/adaptive_research_capability_adapters.cpp
  core/src/adaptive_research_campaign_simulation.cpp
  core/src/diplomacy_state.cpp
  core/src/diplomacy_simulation.cpp
  core/src/diplomacy_snapshot_invariants.cpp
  core/src/diplomacy_lifecycle.cpp
  core/src/diplomacy_observer_commands.cpp
  core/src/diplomacy_runtime.cpp
  core/src/fleet_combat_intelligence.cpp
  core/src/integrated_adaptive_campaign.cpp
  core/src/galaxy_generation_metadata.cpp
  core/src/massive_combat_persistence.cpp
  core/src/fleet_persistence.cpp
  core/src/shipyard_persistence.cpp
  core/src/galaxy_economy_persistence.cpp
  core/src/campaign_foundation_persistence.cpp
  core/src/civilization_founding_roster.cpp
  core/src/planetary_body_persistence.cpp
  core/src/galaxy_reference_validation.cpp
  core/src/knowledge_persistence.cpp
  core/src/legacy_campaign_recovery.cpp)
target_include_directories(stellar_core PUBLIC core/include)
target_link_libraries(stellar_core PUBLIC stellar_engine PRIVATE stellar_json)
add_executable(stellar-continuum app/headless_main.cpp app/galaxy_main.cpp
  app/fresh_campaign_output.cpp app/adaptive_campaign_host.cpp)
target_include_directories(stellar-continuum PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(stellar-continuum PRIVATE stellar_core stellar_json)
set(STELLAR_RESEARCH_RUNTIME_DECLARATION
  "${CMAKE_SOURCE_DIR}/export/research-runtime-files.json")
file(READ "${STELLAR_RESEARCH_RUNTIME_DECLARATION}"
  STELLAR_RESEARCH_RUNTIME_DOCUMENT)
string(JSON STELLAR_RESEARCH_RUNTIME_SCHEMA_TYPE ERROR_VARIABLE STELLAR_RESEARCH_JSON_ERROR
  TYPE "${STELLAR_RESEARCH_RUNTIME_DOCUMENT}" schemaVersion)
string(JSON STELLAR_RESEARCH_RUNTIME_SCHEMA ERROR_VARIABLE STELLAR_RESEARCH_JSON_ERROR
  GET "${STELLAR_RESEARCH_RUNTIME_DOCUMENT}" schemaVersion)
if(STELLAR_RESEARCH_JSON_ERROR OR NOT STELLAR_RESEARCH_RUNTIME_SCHEMA_TYPE STREQUAL "NUMBER" OR
   NOT STELLAR_RESEARCH_RUNTIME_SCHEMA MATCHES "^[0-9]+$" OR
   NOT STELLAR_RESEARCH_RUNTIME_SCHEMA EQUAL 1)
  message(FATAL_ERROR "Invalid Adaptive Research runtime-data declaration schema")
endif()
string(JSON STELLAR_RESEARCH_SOURCE_ROOT_TYPE TYPE
  "${STELLAR_RESEARCH_RUNTIME_DOCUMENT}" root)
string(JSON STELLAR_RESEARCH_DESTINATION_ROOT_TYPE TYPE
  "${STELLAR_RESEARCH_RUNTIME_DOCUMENT}" destination)
if(NOT STELLAR_RESEARCH_SOURCE_ROOT_TYPE STREQUAL "STRING" OR
   NOT STELLAR_RESEARCH_DESTINATION_ROOT_TYPE STREQUAL "STRING")
  message(FATAL_ERROR "Adaptive Research runtime-data roots must be strings")
endif()
string(JSON STELLAR_RESEARCH_SOURCE_ROOT GET
  "${STELLAR_RESEARCH_RUNTIME_DOCUMENT}" root)
string(JSON STELLAR_RESEARCH_DESTINATION_ROOT GET
  "${STELLAR_RESEARCH_RUNTIME_DOCUMENT}" destination)
if(NOT STELLAR_RESEARCH_SOURCE_ROOT STREQUAL "data/research/v1" OR
   NOT STELLAR_RESEARCH_DESTINATION_ROOT STREQUAL "Data/research/v1")
  message(FATAL_ERROR "Adaptive Research runtime-data roots do not match the reviewed layout")
endif()
string(JSON STELLAR_RESEARCH_RUNTIME_FILES_TYPE ERROR_VARIABLE STELLAR_RESEARCH_JSON_ERROR
  TYPE "${STELLAR_RESEARCH_RUNTIME_DOCUMENT}" files)
if(STELLAR_RESEARCH_JSON_ERROR OR NOT STELLAR_RESEARCH_RUNTIME_FILES_TYPE STREQUAL "ARRAY")
  message(FATAL_ERROR "Adaptive Research runtime-data files must be an array")
endif()
string(JSON STELLAR_RESEARCH_RUNTIME_COUNT LENGTH
  "${STELLAR_RESEARCH_RUNTIME_DOCUMENT}" files)
if(STELLAR_RESEARCH_RUNTIME_COUNT EQUAL 0)
  message(FATAL_ERROR "Adaptive Research runtime-data files must be nonempty")
endif()
set(STELLAR_RESEARCH_RUNTIME_FILES)
set(STELLAR_RESEARCH_RUNTIME_FILES_LOWER)
set(STELLAR_RESEARCH_RUNTIME_COPY_COMMANDS)
set(STELLAR_RESEARCH_PREVIOUS_FILE "")
math(EXPR STELLAR_RESEARCH_RUNTIME_LAST "${STELLAR_RESEARCH_RUNTIME_COUNT} - 1")
foreach(STELLAR_RESEARCH_INDEX RANGE 0 ${STELLAR_RESEARCH_RUNTIME_LAST})
  string(JSON STELLAR_RESEARCH_FILE TYPE
    "${STELLAR_RESEARCH_RUNTIME_DOCUMENT}" files ${STELLAR_RESEARCH_INDEX})
  if(NOT STELLAR_RESEARCH_FILE STREQUAL "STRING")
    message(FATAL_ERROR "Adaptive Research runtime-data files must be strings")
  endif()
  string(JSON STELLAR_RESEARCH_FILE GET
    "${STELLAR_RESEARCH_RUNTIME_DOCUMENT}" files ${STELLAR_RESEARCH_INDEX})
  cmake_path(IS_ABSOLUTE STELLAR_RESEARCH_FILE STELLAR_RESEARCH_FILE_ABSOLUTE)
  if(STELLAR_RESEARCH_FILE_ABSOLUTE OR STELLAR_RESEARCH_FILE MATCHES "^/|/$|//" OR
     STELLAR_RESEARCH_FILE MATCHES "(^|/)\\.\\.?(/|$)" OR
     STELLAR_RESEARCH_FILE MATCHES "[\\\\:;$<>]" OR
     NOT STELLAR_RESEARCH_FILE MATCHES "^[^/].*\\.json$")
    message(FATAL_ERROR "Unsafe Adaptive Research runtime-data path: ${STELLAR_RESEARCH_FILE}")
  endif()
  foreach(STELLAR_RESEARCH_CONTROL RANGE 1 31)
    string(ASCII ${STELLAR_RESEARCH_CONTROL} STELLAR_RESEARCH_CONTROL_CHARACTER)
    string(FIND "${STELLAR_RESEARCH_FILE}" "${STELLAR_RESEARCH_CONTROL_CHARACTER}"
      STELLAR_RESEARCH_CONTROL_POSITION)
    if(NOT STELLAR_RESEARCH_CONTROL_POSITION EQUAL -1)
      message(FATAL_ERROR "Control character in Adaptive Research runtime-data path")
    endif()
  endforeach()
  string(ASCII 127 STELLAR_RESEARCH_CONTROL_CHARACTER)
  string(FIND "${STELLAR_RESEARCH_FILE}" "${STELLAR_RESEARCH_CONTROL_CHARACTER}"
    STELLAR_RESEARCH_CONTROL_POSITION)
  if(NOT STELLAR_RESEARCH_CONTROL_POSITION EQUAL -1)
    message(FATAL_ERROR "Control character in Adaptive Research runtime-data path")
  endif()
  if(STELLAR_RESEARCH_PREVIOUS_FILE STRGREATER STELLAR_RESEARCH_FILE)
    message(FATAL_ERROR "Adaptive Research runtime-data paths must be sorted")
  endif()
  string(TOLOWER "${STELLAR_RESEARCH_FILE}" STELLAR_RESEARCH_FILE_LOWER)
  if(STELLAR_RESEARCH_FILE_LOWER IN_LIST STELLAR_RESEARCH_RUNTIME_FILES_LOWER)
    message(FATAL_ERROR "Duplicate Adaptive Research runtime-data path: ${STELLAR_RESEARCH_FILE}")
  endif()
  set(STELLAR_RESEARCH_SOURCE_FILE
    "${CMAKE_SOURCE_DIR}/data/research/v1/${STELLAR_RESEARCH_FILE}")
  if(NOT EXISTS "${STELLAR_RESEARCH_SOURCE_FILE}" OR
     IS_DIRECTORY "${STELLAR_RESEARCH_SOURCE_FILE}")
    message(FATAL_ERROR "Missing Adaptive Research runtime data: ${STELLAR_RESEARCH_FILE}")
  endif()
  cmake_path(ABSOLUTE_PATH STELLAR_RESEARCH_SOURCE_FILE NORMALIZE
    OUTPUT_VARIABLE STELLAR_RESEARCH_SOURCE_NORMALIZED)
  file(REAL_PATH "${STELLAR_RESEARCH_SOURCE_FILE}" STELLAR_RESEARCH_SOURCE_REAL)
  if(WIN32)
    string(TOLOWER "${STELLAR_RESEARCH_SOURCE_NORMALIZED}" STELLAR_RESEARCH_SOURCE_NORMALIZED)
    string(TOLOWER "${STELLAR_RESEARCH_SOURCE_REAL}" STELLAR_RESEARCH_SOURCE_REAL)
  endif()
  if(NOT STELLAR_RESEARCH_SOURCE_NORMALIZED STREQUAL STELLAR_RESEARCH_SOURCE_REAL)
    message(FATAL_ERROR "Linked Adaptive Research runtime data is not allowed: ${STELLAR_RESEARCH_FILE}")
  endif()
  list(APPEND STELLAR_RESEARCH_RUNTIME_FILES "${STELLAR_RESEARCH_FILE}")
  list(APPEND STELLAR_RESEARCH_RUNTIME_FILES_LOWER "${STELLAR_RESEARCH_FILE_LOWER}")
  list(APPEND STELLAR_RESEARCH_RUNTIME_COPY_COMMANDS
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:stellar-continuum>/Data/research/v1/$<PATH:GET_PARENT_PATH,${STELLAR_RESEARCH_FILE}>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/data/research/v1/${STELLAR_RESEARCH_FILE}"
      "$<TARGET_FILE_DIR:stellar-continuum>/Data/research/v1/${STELLAR_RESEARCH_FILE}")
  set(STELLAR_RESEARCH_PREVIOUS_FILE "${STELLAR_RESEARCH_FILE}")
endforeach()
file(GLOB_RECURSE STELLAR_RESEARCH_CANONICAL_FILES
  RELATIVE "${CMAKE_SOURCE_DIR}/data/research/v1"
  "${CMAKE_SOURCE_DIR}/data/research/v1/*.json")
list(SORT STELLAR_RESEARCH_CANONICAL_FILES)
if(NOT STELLAR_RESEARCH_RUNTIME_FILES STREQUAL STELLAR_RESEARCH_CANONICAL_FILES)
  message(FATAL_ERROR "Adaptive Research runtime-data declaration differs from canonical JSON inventory")
endif()

add_custom_target(stellar_runtime_data ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:stellar-continuum>/Data/astronomy"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json" "$<TARGET_FILE_DIR:stellar-continuum>/Data/astronomy/hyg-nearby-500-v1.json"
  ${STELLAR_RESEARCH_RUNTIME_COPY_COMMANDS}
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
foreach(STELLAR_RESEARCH_FILE IN LISTS STELLAR_RESEARCH_RUNTIME_FILES)
  cmake_path(GET STELLAR_RESEARCH_FILE PARENT_PATH STELLAR_RESEARCH_PARENT)
  install(FILES "data/research/v1/${STELLAR_RESEARCH_FILE}"
    DESTINATION "Data/research/v1/${STELLAR_RESEARCH_PARENT}")
endforeach()
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
  if(WIN32)
    add_executable(stellar_research_foreign_tests native-tests/adaptive_research_foreign_technology_tests.cpp)
    target_link_libraries(stellar_research_foreign_tests PRIVATE stellar_core stellar_json bcrypt)
    add_test(NAME adaptive_research_foreign_technology_parity COMMAND stellar_research_foreign_tests
      "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-foreign-technology.json"
      "${CMAKE_SOURCE_DIR}/data/research/v1")
    set_tests_properties(adaptive_research_foreign_technology_parity PROPERTIES TIMEOUT 180)
  endif()
  add_executable(stellar_research_outcome_tests native-tests/adaptive_research_outcomes_tests.cpp)
  target_link_libraries(stellar_research_outcome_tests PRIVATE stellar_core stellar_json)
  add_test(NAME adaptive_research_outcomes_parity COMMAND stellar_research_outcome_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-outcomes.json"
    "${CMAKE_SOURCE_DIR}/data/research/v1")
  set_tests_properties(adaptive_research_outcomes_parity PROPERTIES TIMEOUT 180)
  if(WIN32)
    add_executable(stellar_research_discovery_tests native-tests/adaptive_research_foreign_discovery_tests.cpp)
    target_link_libraries(stellar_research_discovery_tests PRIVATE stellar_core stellar_json bcrypt)
    add_test(NAME adaptive_research_foreign_discovery_parity COMMAND stellar_research_discovery_tests
      "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-foreign-discovery.json"
      "${CMAKE_SOURCE_DIR}/data/research/v1")
    set_tests_properties(adaptive_research_foreign_discovery_parity PROPERTIES TIMEOUT 180)
  endif()
  add_executable(stellar_research_owner_tests native-tests/adaptive_research_strategic_runtime_tests.cpp)
  target_link_libraries(stellar_research_owner_tests PRIVATE stellar_core stellar_json)
  add_test(NAME adaptive_research_strategic_runtime_parity COMMAND stellar_research_owner_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-strategic-owner.json"
    "${CMAKE_SOURCE_DIR}/data/research/v1")
  set_tests_properties(adaptive_research_strategic_runtime_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_research_v3_tests native-tests/adaptive_research_strategic_snapshot_tests.cpp)
  target_link_libraries(stellar_research_v3_tests PRIVATE stellar_core stellar_json)
  add_test(NAME adaptive_research_strategic_snapshot_parity COMMAND stellar_research_v3_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-strategic-snapshot.json"
    "${CMAKE_SOURCE_DIR}/data/research/v1")
  set_tests_properties(adaptive_research_strategic_snapshot_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_research_v4_tests native-tests/adaptive_research_foreign_technology_snapshot_tests.cpp)
  target_link_libraries(stellar_research_v4_tests PRIVATE stellar_core stellar_json)
  add_test(NAME adaptive_research_foreign_snapshot_parity COMMAND stellar_research_v4_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-foreign-snapshot.json"
    "${CMAKE_SOURCE_DIR}/data/research/v1")
  set_tests_properties(adaptive_research_foreign_snapshot_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_research_v5_tests native-tests/adaptive_research_outcome_snapshot_tests.cpp)
  target_link_libraries(stellar_research_v5_tests PRIVATE stellar_core stellar_json)
  add_test(NAME adaptive_research_outcome_snapshot_parity COMMAND stellar_research_v5_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-outcome-snapshot.json"
    "${CMAKE_SOURCE_DIR}/data/research/v1")
  set_tests_properties(adaptive_research_outcome_snapshot_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_research_campaign_tests native-tests/adaptive_research_campaign_tests.cpp)
  target_link_libraries(stellar_research_campaign_tests PRIVATE stellar_core stellar_json)
  add_test(NAME adaptive_research_campaign_parity COMMAND stellar_research_campaign_tests
    "${CMAKE_SOURCE_DIR}/data/research/v1"
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-campaign.json")
  set_tests_properties(adaptive_research_campaign_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_research_funding_tests native-tests/adaptive_research_funding_tests.cpp)
  target_link_libraries(stellar_research_funding_tests PRIVATE stellar_core stellar_json)
  add_test(NAME adaptive_research_funding_parity COMMAND stellar_research_funding_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-funding.json"
    "${CMAKE_SOURCE_DIR}/data/research/v1")
  set_tests_properties(adaptive_research_funding_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_research_capability_tests native-tests/adaptive_research_capability_adapters_tests.cpp)
  target_link_libraries(stellar_research_capability_tests PRIVATE stellar_core stellar_json)
  add_test(NAME adaptive_research_capability_adapters_parity COMMAND stellar_research_capability_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-capability-adapters.json"
    "${CMAKE_SOURCE_DIR}/data/research/v1")
  set_tests_properties(adaptive_research_capability_adapters_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_research_campaign_sim_tests native-tests/adaptive_research_campaign_simulation_tests.cpp)
  target_link_libraries(stellar_research_campaign_sim_tests PRIVATE stellar_core stellar_json)
  add_test(NAME adaptive_research_campaign_simulation_parity COMMAND stellar_research_campaign_sim_tests
    "${CMAKE_SOURCE_DIR}/data/research/v1"
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/adaptive-research-campaign-simulation.json")
  set_tests_properties(adaptive_research_campaign_simulation_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_diplomacy_state_tests native-tests/diplomacy_state_tests.cpp)
  target_link_libraries(stellar_diplomacy_state_tests PRIVATE stellar_core stellar_json)
  add_test(NAME diplomacy_state_parity COMMAND stellar_diplomacy_state_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/diplomacy-state.json"
    "${CMAKE_SOURCE_DIR}")
  set_tests_properties(diplomacy_state_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_diplomacy_simulation_tests native-tests/diplomacy_simulation_tests.cpp)
  target_link_libraries(stellar_diplomacy_simulation_tests PRIVATE stellar_core stellar_json)
  add_test(NAME diplomacy_simulation_parity COMMAND stellar_diplomacy_simulation_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/diplomacy-simulation.json"
    "${CMAKE_SOURCE_DIR}")
  set_tests_properties(diplomacy_simulation_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_diplomacy_snapshot_invariants_tests native-tests/diplomacy_snapshot_invariants_tests.cpp)
  target_link_libraries(stellar_diplomacy_snapshot_invariants_tests PRIVATE stellar_core stellar_json)
  add_test(NAME diplomacy_snapshot_invariants_parity COMMAND stellar_diplomacy_snapshot_invariants_tests
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/diplomacy-snapshot-invariants.json")
  set_tests_properties(diplomacy_snapshot_invariants_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_diplomacy_lifecycle_tests native-tests/diplomacy_lifecycle_tests.cpp)
  target_link_libraries(stellar_diplomacy_lifecycle_tests PRIVATE stellar_core stellar_json)
  add_test(NAME diplomacy_lifecycle_parity COMMAND stellar_diplomacy_lifecycle_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/diplomacy-lifecycle.json"
    "${CMAKE_SOURCE_DIR}")
  set_tests_properties(diplomacy_lifecycle_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_diplomacy_observer_commands_tests native-tests/diplomacy_observer_commands_tests.cpp)
  target_link_libraries(stellar_diplomacy_observer_commands_tests PRIVATE stellar_core stellar_json)
  add_test(NAME diplomacy_observer_commands_parity COMMAND stellar_diplomacy_observer_commands_tests
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/diplomacy-observer-commands.json")
  set_tests_properties(diplomacy_observer_commands_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_diplomacy_runtime_tests native-tests/diplomacy_runtime_tests.cpp)
  target_link_libraries(stellar_diplomacy_runtime_tests PRIVATE stellar_core stellar_json)
  add_test(NAME diplomacy_runtime_parity COMMAND stellar_diplomacy_runtime_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/diplomacy-runtime.json"
    "${CMAKE_SOURCE_DIR}")
  set_tests_properties(diplomacy_runtime_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_fleet_combat_intelligence_tests native-tests/fleet_combat_intelligence_tests.cpp)
  target_link_libraries(stellar_fleet_combat_intelligence_tests PRIVATE stellar_core stellar_json)
  add_test(NAME fleet_combat_intelligence_parity COMMAND stellar_fleet_combat_intelligence_tests
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/fleet-combat-intelligence.json")
  set_tests_properties(fleet_combat_intelligence_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_integrated_adaptive_campaign_tests native-tests/integrated_adaptive_campaign_tests.cpp)
  target_link_libraries(stellar_integrated_adaptive_campaign_tests PRIVATE stellar_core stellar_json)
  target_include_directories(stellar_integrated_adaptive_campaign_tests PRIVATE "${CMAKE_SOURCE_DIR}/native-tests")
  add_test(NAME integrated_adaptive_campaign_parity COMMAND stellar_integrated_adaptive_campaign_tests
    "${CMAKE_SOURCE_DIR}/data/research/v1"
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/integrated-adaptive-campaign.json")
  set_tests_properties(integrated_adaptive_campaign_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_galaxy_generation_metadata_tests native-tests/galaxy_generation_metadata_tests.cpp)
  target_link_libraries(stellar_galaxy_generation_metadata_tests PRIVATE stellar_core stellar_json)
  add_test(NAME galaxy_generation_metadata_parity COMMAND stellar_galaxy_generation_metadata_tests
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/galaxy-metadata-persistence.json")
  set_tests_properties(galaxy_generation_metadata_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_massive_combat_persistence_tests native-tests/massive_combat_persistence_tests.cpp)
  target_link_libraries(stellar_massive_combat_persistence_tests PRIVATE stellar_core stellar_json)
  add_test(NAME massive_combat_persistence_parity COMMAND stellar_massive_combat_persistence_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/massive-encounter.json"
    "${CMAKE_SOURCE_DIR}/src/Game")
  set_tests_properties(massive_combat_persistence_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_fleet_persistence_tests native-tests/fleet_persistence_tests.cpp)
  target_link_libraries(stellar_fleet_persistence_tests PRIVATE stellar_core stellar_json)
  add_test(NAME fleet_persistence_parity COMMAND stellar_fleet_persistence_tests
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/fleet-persistence.json")
  set_tests_properties(fleet_persistence_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_shipyard_persistence_tests native-tests/shipyard_persistence_tests.cpp)
  target_link_libraries(stellar_shipyard_persistence_tests PRIVATE stellar_core stellar_json)
  add_test(NAME shipyard_persistence_parity COMMAND stellar_shipyard_persistence_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/shipyard-persistence.json"
    "${CMAKE_SOURCE_DIR}/src/Game")
  set_tests_properties(shipyard_persistence_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_galaxy_economy_persistence_tests native-tests/galaxy_economy_persistence_tests.cpp)
  target_link_libraries(stellar_galaxy_economy_persistence_tests PRIVATE stellar_core stellar_json)
  add_test(NAME galaxy_economy_persistence_parity COMMAND stellar_galaxy_economy_persistence_tests
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/galaxy-economy-persistence.json")
  set_tests_properties(galaxy_economy_persistence_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_campaign_foundation_persistence_tests native-tests/campaign_foundation_persistence_tests.cpp)
  target_link_libraries(stellar_campaign_foundation_persistence_tests PRIVATE stellar_core stellar_json)
  add_test(NAME campaign_foundation_persistence_parity COMMAND stellar_campaign_foundation_persistence_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/campaign-foundation-persistence.json"
    "${CMAKE_SOURCE_DIR}/src/Game")
  set_tests_properties(campaign_foundation_persistence_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_planetary_body_persistence_tests native-tests/planetary_body_persistence_tests.cpp)
  target_link_libraries(stellar_planetary_body_persistence_tests PRIVATE stellar_core stellar_json)
  add_test(NAME planetary_body_persistence_parity COMMAND stellar_planetary_body_persistence_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/planetary-body-persistence.json"
    "${CMAKE_SOURCE_DIR}/src/Game")
  set_tests_properties(planetary_body_persistence_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_galaxy_reference_validation_tests native-tests/galaxy_reference_validation_tests.cpp)
  target_link_libraries(stellar_galaxy_reference_validation_tests PRIVATE stellar_core stellar_json)
  add_test(NAME galaxy_reference_validation_parity COMMAND stellar_galaxy_reference_validation_tests
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/galaxy-reference-validation.json")
  set_tests_properties(galaxy_reference_validation_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_knowledge_persistence_tests native-tests/knowledge_persistence_tests.cpp)
  target_link_libraries(stellar_knowledge_persistence_tests PRIVATE stellar_core stellar_json)
  add_test(NAME knowledge_persistence_parity COMMAND stellar_knowledge_persistence_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/knowledge-persistence.json"
    "${CMAKE_SOURCE_DIR}/src/Game")
  set_tests_properties(knowledge_persistence_parity PROPERTIES TIMEOUT 180)
  add_executable(stellar_legacy_campaign_recovery_tests native-tests/legacy_campaign_recovery_tests.cpp)
  target_link_libraries(stellar_legacy_campaign_recovery_tests PRIVATE stellar_core stellar_json)
  add_test(NAME legacy_campaign_recovery_parity COMMAND stellar_legacy_campaign_recovery_tests
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/legacy-campaign-recovery.json")
  set_tests_properties(legacy_campaign_recovery_parity PROPERTIES TIMEOUT 180)
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
