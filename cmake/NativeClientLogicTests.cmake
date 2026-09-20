# Pure client logic remains testable on headless CI without SDL, a font or a GPU.
add_executable(stellar_native_system_travel_tests
  native-tests/native_system_travel_tests.cpp
  app/native_client/native_system_travel.cpp app/native_client/native_system_view.cpp)
target_include_directories(stellar_native_system_travel_tests PRIVATE app/native_client engine/include)
target_link_libraries(stellar_native_system_travel_tests PRIVATE stellar_core)
add_test(NAME native_system_travel COMMAND stellar_native_system_travel_tests
  "${CMAKE_SOURCE_DIR}/data/research/v1"
  "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json")
set_tests_properties(native_system_travel PROPERTIES TIMEOUT 90)
if(MSVC)
  target_compile_options(stellar_native_system_travel_tests PRIVATE /WX)
endif()

add_executable(stellar_native_system_workspace_tests
  native-tests/native_system_workspace_tests.cpp
  app/native_client/native_system_workspace.cpp app/native_client/native_system_view.cpp
  app/native_client/native_system_travel.cpp app/native_client/native_fleet_controller.cpp)
target_include_directories(stellar_native_system_workspace_tests PRIVATE app/native_client engine/include)
target_link_libraries(stellar_native_system_workspace_tests PRIVATE stellar_core)
add_test(NAME native_system_workspace COMMAND stellar_native_system_workspace_tests
  "${CMAKE_SOURCE_DIR}/data/research/v1"
  "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json")
set_tests_properties(native_system_workspace PROPERTIES TIMEOUT 90)
if(MSVC)
  target_compile_options(stellar_native_system_workspace_tests PRIVATE /WX)
endif()

add_executable(stellar_native_system_view_tests
  native-tests/native_system_view_tests.cpp app/native_client/native_system_view.cpp)
target_include_directories(stellar_native_system_view_tests PRIVATE app/native_client)
target_link_libraries(stellar_native_system_view_tests PRIVATE stellar_core)
add_test(NAME native_system_view COMMAND stellar_native_system_view_tests
  "${CMAKE_SOURCE_DIR}/data/research/v1"
  "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json")
set_tests_properties(native_system_view PROPERTIES TIMEOUT 90)
if(MSVC)
  target_compile_options(stellar_native_system_view_tests PRIVATE /WX)
endif()

add_executable(stellar_native_research_controller_tests
  native-tests/native_research_controller_tests.cpp app/native_client/native_research_controller.cpp)
target_include_directories(stellar_native_research_controller_tests PRIVATE app/native_client)
target_link_libraries(stellar_native_research_controller_tests PRIVATE stellar_core stellar_json)
add_test(NAME native_research_controller COMMAND stellar_native_research_controller_tests
  "${CMAKE_SOURCE_DIR}/data/research/v1"
  "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json"
  "${CMAKE_SOURCE_DIR}/native-tests/fixtures/player-campaign-json.json"
  "${CMAKE_BINARY_DIR}/native-research-cases")
set_tests_properties(native_research_controller PROPERTIES TIMEOUT 90)

add_executable(stellar_native_research_workspace_tests
  native-tests/native_research_workspace_tests.cpp app/native_client/native_research_workspace.cpp)
target_include_directories(stellar_native_research_workspace_tests PRIVATE app/native_client)
target_link_libraries(stellar_native_research_workspace_tests PRIVATE stellar_core)
add_test(NAME native_research_workspace COMMAND stellar_native_research_workspace_tests)

add_executable(stellar_native_ui_layout_tests native-tests/native_ui_layout_tests.cpp)
target_include_directories(stellar_native_ui_layout_tests PRIVATE app/native_client engine/include)
add_test(NAME native_ui_layout COMMAND stellar_native_ui_layout_tests)

add_executable(stellar_native_client_input_tests native-tests/native_client_input_tests.cpp)
target_include_directories(stellar_native_client_input_tests PRIVATE app/native_client engine/include)
add_test(NAME native_client_input COMMAND stellar_native_client_input_tests)

add_executable(stellar_native_development_menu_tests
  native-tests/native_development_menu_tests.cpp app/native_client/native_development_menu.cpp)
target_include_directories(stellar_native_development_menu_tests PRIVATE app/native_client engine/include)
add_test(NAME native_development_menu COMMAND stellar_native_development_menu_tests)

add_executable(stellar_native_developer_tools_tests
  native-tests/native_developer_tools_tests.cpp app/native_client/native_developer_tools.cpp)
target_include_directories(stellar_native_developer_tools_tests PRIVATE app/native_client engine/include)
target_link_libraries(stellar_native_developer_tools_tests PRIVATE stellar_core)
add_test(NAME native_developer_tools COMMAND stellar_native_developer_tools_tests)

if(MSVC)
  target_compile_options(stellar_native_research_controller_tests PRIVATE /WX)
  target_compile_options(stellar_native_research_workspace_tests PRIVATE /WX)
  target_compile_options(stellar_native_ui_layout_tests PRIVATE /WX)
  target_compile_options(stellar_native_development_menu_tests PRIVATE /WX)
  target_compile_options(stellar_native_developer_tools_tests PRIVATE /WX)
  target_compile_options(stellar_native_client_input_tests PRIVATE /WX)
endif()

add_executable(stellar_native_fleet_controller_tests
  native-tests/native_fleet_controller_tests.cpp app/native_client/native_fleet_controller.cpp)
target_include_directories(stellar_native_fleet_controller_tests PRIVATE app/native_client)
target_link_libraries(stellar_native_fleet_controller_tests PRIVATE stellar_core stellar_json)
add_test(NAME native_fleet_controller COMMAND stellar_native_fleet_controller_tests
  "${CMAKE_SOURCE_DIR}/data/research/v1"
  "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json"
  "${CMAKE_SOURCE_DIR}/native-tests/fixtures/player-campaign-json.json"
  "${CMAKE_BINARY_DIR}/native-fleet-cases")
set_tests_properties(native_fleet_controller PROPERTIES TIMEOUT 90)
add_executable(stellar_native_fleet_workspace_tests
  native-tests/native_fleet_workspace_tests.cpp app/native_client/native_fleet_workspace.cpp
  app/native_client/native_overview.cpp app/native_client/native_ship_art_assets.cpp)
target_include_directories(stellar_native_fleet_workspace_tests PRIVATE app/native_client engine/include)
target_link_libraries(stellar_native_fleet_workspace_tests PRIVATE stellar_core stellar_native_image)
add_test(NAME native_fleet_workspace COMMAND stellar_native_fleet_workspace_tests)
add_executable(stellar_native_fleet_presentation_tests
  native-tests/native_fleet_presentation_tests.cpp app/native_client/native_fleet_presentation.cpp)
target_include_directories(stellar_native_fleet_presentation_tests PRIVATE app/native_client engine/include)
target_link_libraries(stellar_native_fleet_presentation_tests PRIVATE stellar_core)
add_test(NAME native_fleet_presentation COMMAND stellar_native_fleet_presentation_tests)
if(MSVC)
  target_compile_options(stellar_native_fleet_controller_tests PRIVATE /WX)
  target_compile_options(stellar_native_fleet_workspace_tests PRIVATE /WX)
  target_compile_options(stellar_native_fleet_presentation_tests PRIVATE /WX)
  target_compile_options(stellar_exploration_order_tests PRIVATE /WX)
endif()

add_executable(stellar_native_shipyard_controller_tests
  native-tests/native_shipyard_controller_tests.cpp app/native_client/native_shipyard_controller.cpp)
target_include_directories(stellar_native_shipyard_controller_tests PRIVATE app/native_client)
target_link_libraries(stellar_native_shipyard_controller_tests PRIVATE stellar_core stellar_json)
add_test(NAME native_shipyard_controller COMMAND stellar_native_shipyard_controller_tests
  "${CMAKE_SOURCE_DIR}/data/research/v1"
  "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json"
  "${CMAKE_SOURCE_DIR}/native-tests/fixtures/player-campaign-json.json"
  "${CMAKE_BINARY_DIR}/native-shipyard-cases")
set_tests_properties(native_shipyard_controller PROPERTIES TIMEOUT 90)
if(MSVC)
  target_compile_options(stellar_native_shipyard_controller_tests PRIVATE /WX)
endif()

add_executable(stellar_native_shipyard_workspace_tests
  native-tests/native_shipyard_workspace_tests.cpp app/native_client/native_shipyard_workspace.cpp
  app/native_client/native_ship_art_assets.cpp)
target_include_directories(stellar_native_shipyard_workspace_tests PRIVATE app/native_client engine/include)
target_link_libraries(stellar_native_shipyard_workspace_tests PRIVATE stellar_core stellar_native_image)
add_test(NAME native_shipyard_workspace COMMAND stellar_native_shipyard_workspace_tests)
if(MSVC)
  target_compile_options(stellar_native_shipyard_workspace_tests PRIVATE /WX)
endif()

add_executable(stellar_native_construction_controller_tests
  native-tests/native_construction_controller_tests.cpp app/native_client/native_construction_controller.cpp)
target_include_directories(stellar_native_construction_controller_tests PRIVATE app/native_client)
target_link_libraries(stellar_native_construction_controller_tests PRIVATE stellar_core stellar_json)
add_test(NAME native_construction_controller COMMAND stellar_native_construction_controller_tests
  "${CMAKE_SOURCE_DIR}/data/research/v1"
  "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json"
  "${CMAKE_SOURCE_DIR}/native-tests/fixtures/player-campaign-json.json"
  "${CMAKE_BINARY_DIR}/native-construction-cases")
set_tests_properties(native_construction_controller PROPERTIES TIMEOUT 90)
add_executable(stellar_native_construction_workspace_tests
  native-tests/native_construction_workspace_tests.cpp app/native_client/native_construction_workspace.cpp)
target_include_directories(stellar_native_construction_workspace_tests PRIVATE app/native_client engine/include)
target_link_libraries(stellar_native_construction_workspace_tests PRIVATE stellar_core)
add_test(NAME native_construction_workspace COMMAND stellar_native_construction_workspace_tests)
if(MSVC)
  target_compile_options(stellar_native_construction_controller_tests PRIVATE /WX)
  target_compile_options(stellar_native_construction_workspace_tests PRIVATE /WX)
endif()

add_executable(stellar_native_diplomacy_controller_tests
  native-tests/native_diplomacy_controller_tests.cpp
  app/native_client/native_diplomacy_controller.cpp)
target_include_directories(stellar_native_diplomacy_controller_tests PRIVATE app/native_client)
target_link_libraries(stellar_native_diplomacy_controller_tests PRIVATE stellar_core stellar_json)
add_test(NAME native_diplomacy_controller COMMAND stellar_native_diplomacy_controller_tests
  "${CMAKE_SOURCE_DIR}/data/research/v1"
  "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json")
set_tests_properties(native_diplomacy_controller PROPERTIES TIMEOUT 90)
add_executable(stellar_native_diplomacy_workspace_tests
  native-tests/native_diplomacy_workspace_tests.cpp
  app/native_client/native_diplomacy_workspace.cpp
  app/native_client/native_diplomacy_controller.cpp)
target_include_directories(stellar_native_diplomacy_workspace_tests PRIVATE app/native_client engine/include)
target_link_libraries(stellar_native_diplomacy_workspace_tests PRIVATE stellar_core)
add_test(NAME native_diplomacy_workspace COMMAND stellar_native_diplomacy_workspace_tests)
if(MSVC)
  target_compile_options(stellar_native_diplomacy_controller_tests PRIVATE /WX)
  target_compile_options(stellar_native_diplomacy_workspace_tests PRIVATE /WX)
endif()

add_executable(stellar_native_fresh_progression_tests
  native-tests/native_fresh_progression_tests.cpp
  app/native_client/native_research_controller.cpp
  app/native_client/native_construction_controller.cpp
  app/native_client/native_shipyard_controller.cpp
  app/native_client/native_fleet_controller.cpp)
target_include_directories(stellar_native_fresh_progression_tests PRIVATE app/native_client)
target_link_libraries(stellar_native_fresh_progression_tests PRIVATE stellar_core stellar_json)
add_test(NAME native_fresh_progression COMMAND stellar_native_fresh_progression_tests
  "${CMAKE_SOURCE_DIR}/data/research/v1"
  "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json")
# Two ordinary campaigns replay twice through thousands of bounded strategic
# days. The checked Debug build takes about three minutes locally.
set_tests_properties(native_fresh_progression PROPERTIES TIMEOUT 600)
if(MSVC)
  target_compile_options(stellar_native_fresh_progression_tests PRIVATE /WX)
endif()

add_executable(stellar_native_colony_controller_tests
  native-tests/native_colony_controller_tests.cpp
  app/native_client/native_colony_controller.cpp
  app/native_client/native_settlement_mission_controller.cpp
  app/native_client/native_system_view.cpp)
target_include_directories(stellar_native_colony_controller_tests PRIVATE app/native_client)
target_link_libraries(stellar_native_colony_controller_tests PRIVATE stellar_core)
add_test(NAME native_colony_controller COMMAND stellar_native_colony_controller_tests
  "${CMAKE_SOURCE_DIR}/data/research/v1"
  "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json")
set_tests_properties(native_colony_controller PROPERTIES TIMEOUT 90)
if(MSVC)
  target_compile_options(stellar_native_colony_controller_tests PRIVATE /WX)
endif()

add_executable(stellar_surface_ui_tests
  native-tests/native_surface_construction_controller_tests.cpp
  app/native_client/native_surface_construction_controller.cpp
  app/native_client/native_colony_controller.cpp
  app/native_client/native_system_view.cpp)
target_include_directories(stellar_surface_ui_tests PRIVATE app/native_client)
target_link_libraries(stellar_surface_ui_tests PRIVATE stellar_core)
add_test(NAME native_surface_construction_controller COMMAND stellar_surface_ui_tests
  "${CMAKE_SOURCE_DIR}/data/research/v1"
  "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json")
set_tests_properties(native_surface_construction_controller PROPERTIES TIMEOUT 90)
if(MSVC)
  target_compile_options(stellar_surface_ui_tests PRIVATE /WX)
endif()

add_executable(stellar_native_colony_workspace_tests
  native-tests/native_colony_workspace_tests.cpp
  app/native_client/native_colony_workspace.cpp)
target_include_directories(stellar_native_colony_workspace_tests PRIVATE
  app/native_client
  engine/include)
target_link_libraries(stellar_native_colony_workspace_tests PRIVATE stellar_core)
add_test(NAME native_colony_workspace COMMAND stellar_native_colony_workspace_tests)

add_executable(stellar_native_system_colony_entry_tests
  native-tests/native_system_colony_entry_tests.cpp
  app/native_client/native_system_view.cpp
  app/native_client/native_system_travel.cpp
  app/native_client/native_system_workspace.cpp
  app/native_client/native_fleet_controller.cpp)
target_include_directories(stellar_native_system_colony_entry_tests PRIVATE
  app/native_client
  engine/include)
target_link_libraries(stellar_native_system_colony_entry_tests PRIVATE stellar_core)
add_test(NAME native_system_colony_entry COMMAND stellar_native_system_colony_entry_tests)

if(MSVC)
  target_compile_options(stellar_native_colony_workspace_tests PRIVATE /WX)
  target_compile_options(stellar_native_system_colony_entry_tests PRIVATE /WX)
endif()

add_executable(stellar_settle_target_tests
  native-tests/native_settlement_targeting_tests.cpp
  app/native_client/native_settlement_mission_controller.cpp)
target_include_directories(stellar_settle_target_tests PRIVATE app/native_client)
target_link_libraries(stellar_settle_target_tests PRIVATE stellar_core)
add_test(NAME native_settlement_targeting COMMAND stellar_settle_target_tests
  "${CMAKE_SOURCE_DIR}/data/research/v1"
  "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json")
set_tests_properties(native_settlement_targeting PROPERTIES TIMEOUT 90)
if(MSVC)
  target_compile_options(stellar_settle_target_tests PRIVATE /WX)
endif()

add_executable(stellar_settle_ui_tests
  native-tests/native_settlement_workspace_tests.cpp
  app/native_client/native_settlement_workspace.cpp
  app/native_client/native_system_workspace.cpp
  app/native_client/native_system_view.cpp
  app/native_client/native_system_travel.cpp)
target_include_directories(stellar_settle_ui_tests PRIVATE
  app/native_client
  engine/include)
target_link_libraries(stellar_settle_ui_tests PRIVATE stellar_core)
if(MSVC)
  target_compile_options(stellar_settle_ui_tests PRIVATE /W4 /WX /permissive-)
endif()
add_test(NAME native_settlement_workspace COMMAND stellar_settle_ui_tests)

add_executable(stellar_surface_view_tests
  native-tests/native_surface_workspace_tests.cpp
  app/native_client/native_surface_workspace.cpp
  app/native_client/native_surface_relief.cpp
  app/native_client/native_surface_scene.cpp
  app/native_client/native_colony_workspace.cpp)
target_include_directories(stellar_surface_view_tests PRIVATE
  app/native_client
  engine/include)
target_link_libraries(stellar_surface_view_tests PRIVATE stellar_core stellar_native_image)
add_test(NAME native_surface_workspace COMMAND stellar_surface_view_tests)
if(MSVC)
  target_compile_options(stellar_surface_view_tests PRIVATE
    /W4 /WX /permissive-)
endif()

add_executable(stellar_battle_workspace_tests
  native-tests/native_battle_workspace_tests.cpp
  app/native_client/native_battle_workspace.cpp)
target_include_directories(stellar_battle_workspace_tests PRIVATE
  app/native_client
  engine/include)
target_link_libraries(stellar_battle_workspace_tests PRIVATE stellar_core)
add_test(NAME native_battle_workspace COMMAND stellar_battle_workspace_tests)
if(MSVC)
  target_compile_options(stellar_battle_workspace_tests PRIVATE
    /W4 /WX /permissive-)
endif()

add_executable(stellar_audio_settings_tests
  native-tests/native_audio_settings_tests.cpp
  app/native_client/native_audio_settings.cpp)
target_include_directories(stellar_audio_settings_tests PRIVATE
  app/native_client
  engine/include)
add_test(NAME native_audio_settings COMMAND stellar_audio_settings_tests)
if(MSVC)
  target_compile_options(stellar_audio_settings_tests PRIVATE
    /W4 /WX /permissive-)
endif()

add_executable(stellar_native_notification_tests
  native-tests/native_notification_tests.cpp
  app/native_client/native_notifications.cpp)
target_include_directories(stellar_native_notification_tests PRIVATE
  app/native_client
  engine/include)
add_test(NAME native_notifications COMMAND stellar_native_notification_tests)
if(MSVC)
  target_compile_options(stellar_native_notification_tests PRIVATE
    /W4 /WX /permissive-)
endif()

add_executable(stellar_native_support_tests
  native-tests/native_support_tests.cpp
  app/native_client/native_support.cpp)
target_include_directories(stellar_native_support_tests PRIVATE
  app/native_client
  engine/include)
add_test(NAME native_support COMMAND stellar_native_support_tests)
if(MSVC)
  target_compile_options(stellar_native_support_tests PRIVATE
    /W4 /WX /permissive-)
endif()


add_executable(stellar_new_setup_tests
  app/native_client/native_new_campaign_setup.cpp
  native-tests/native_new_campaign_setup_tests.cpp)
add_executable(stellar_new_generation_tests
  app/native_client/native_new_campaign_setup.cpp
  app/native_client/native_new_campaign_generation.cpp
  native-tests/native_new_campaign_generation_tests.cpp)
add_executable(stellar_new_ui_tests
  app/native_client/native_new_game_workspace.cpp
  native-tests/native_new_game_workspace_tests.cpp)
foreach(STELLAR_SETUP_TEST IN ITEMS stellar_new_setup_tests stellar_new_generation_tests stellar_new_ui_tests)
  target_include_directories(${STELLAR_SETUP_TEST} PRIVATE app/native_client engine/include)
  target_link_libraries(${STELLAR_SETUP_TEST} PRIVATE stellar_core)
  if(MSVC)
    target_compile_options(${STELLAR_SETUP_TEST} PRIVATE /W4 /WX /permissive-)
  endif()
endforeach()
add_test(NAME native_new_campaign_setup COMMAND stellar_new_setup_tests
  "${CMAKE_SOURCE_DIR}/data/research/v1"
  "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json")
add_test(NAME native_new_campaign_generation COMMAND stellar_new_generation_tests
  "${CMAKE_SOURCE_DIR}/data/research/v1"
  "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json")
add_test(NAME native_new_game_workspace COMMAND stellar_new_ui_tests)
set_tests_properties(native_new_campaign_setup native_new_campaign_generation PROPERTIES TIMEOUT 180)

add_executable(stellar_startup_tests
  app/native_client/native_new_campaign_setup.cpp
  app/native_client/native_new_campaign_generation.cpp
  app/native_client/native_campaign_session.cpp
  app/native_client/native_notifications.cpp
  app/native_client/native_startup_session.cpp
  native-tests/native_startup_session_tests.cpp)
target_include_directories(stellar_startup_tests PRIVATE app/native_client engine/include)
target_link_libraries(stellar_startup_tests PRIVATE stellar_core stellar_json Shell32 Ole32)
if(MSVC)
  target_compile_options(stellar_startup_tests PRIVATE /W4 /WX /permissive-)
endif()
add_test(NAME native_startup_session COMMAND stellar_startup_tests
  "${CMAKE_SOURCE_DIR}/data/research/v1"
  "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json"
  "${CMAKE_BINARY_DIR}/native-startup-session-scratch")
set_tests_properties(native_startup_session PROPERTIES TIMEOUT 240)

add_executable(stellar_startup_ui_tests
  app/native_client/native_new_game_workspace.cpp
  app/native_client/native_startup_workspace.cpp
  native-tests/native_startup_workspace_tests.cpp)
target_include_directories(stellar_startup_ui_tests PRIVATE app/native_client engine/include)
target_link_libraries(stellar_startup_ui_tests PRIVATE stellar_core)
if(MSVC)
  target_compile_options(stellar_startup_ui_tests PRIVATE /W4 /WX /permissive-)
endif()
add_test(NAME native_startup_workspace COMMAND stellar_startup_ui_tests)

# Approved menu/loading artwork and its responsive player controls.
add_executable(stellar_art_tests
  app/native_client/native_startup_artwork.cpp
  app/native_client/native_new_game_workspace.cpp
  app/native_client/native_startup_workspace.cpp
  native-tests/native_startup_artwork_tests.cpp)
target_include_directories(stellar_art_tests PRIVATE app/native_client)
target_link_libraries(stellar_art_tests PRIVATE stellar_core stellar_engine stellar_native_image)
if(MSVC)
  target_compile_options(stellar_art_tests PRIVATE /W4 /WX /permissive-)
endif()
add_test(NAME native_startup_artwork COMMAND stellar_art_tests "${CMAKE_SOURCE_DIR}")
target_sources(stellar_new_ui_tests PRIVATE app/native_client/native_startup_artwork.cpp)
target_sources(stellar_startup_ui_tests PRIVATE app/native_client/native_startup_artwork.cpp)
target_link_libraries(stellar_new_ui_tests PRIVATE stellar_native_image)
target_link_libraries(stellar_startup_ui_tests PRIVATE stellar_native_image)

if(BUILD_TESTING)
  add_executable(stellar_celestial_tests
    native-tests/native_celestial_appearance_tests.cpp
    app/native_client/native_celestial_appearance.cpp)
  target_include_directories(stellar_celestial_tests PRIVATE
    app/native_client engine/include core/include)
  target_link_libraries(stellar_celestial_tests PRIVATE
    stellar_native_image)
  add_test(NAME native_celestial_appearance
    COMMAND stellar_celestial_tests)
  target_sources(stellar_native_system_workspace_tests PRIVATE
    app/native_client/native_celestial_appearance.cpp
    app/native_client/native_orbital_structure.cpp)
  target_sources(stellar_native_system_colony_entry_tests PRIVATE
    app/native_client/native_celestial_appearance.cpp
    app/native_client/native_orbital_structure.cpp)
  target_sources(stellar_settle_ui_tests PRIVATE
    app/native_client/native_celestial_appearance.cpp
    app/native_client/native_orbital_structure.cpp)
  target_link_libraries(stellar_native_system_workspace_tests PRIVATE
    stellar_native_image)
  target_link_libraries(stellar_native_system_colony_entry_tests PRIVATE
    stellar_native_image)
  target_link_libraries(stellar_settle_ui_tests PRIVATE stellar_native_image)
  if(MSVC)
    target_compile_options(stellar_celestial_tests PRIVATE /W4 /WX /permissive-)
  endif()
endif()


add_executable(stellar_galaxy_backdrop_tests
  app/native_client/native_galaxy_backdrop.cpp native-tests/native_galaxy_backdrop_tests.cpp)
target_include_directories(stellar_galaxy_backdrop_tests PRIVATE app/native_client)
target_link_libraries(stellar_galaxy_backdrop_tests PRIVATE stellar_native_image)
add_test(NAME native_galaxy_backdrop COMMAND stellar_galaxy_backdrop_tests "${CMAKE_SOURCE_DIR}")

add_executable(stellar_ship_art_tests
  native-tests/native_ship_art_tests.cpp
  app/native_client/native_ship_art_assets.cpp
  app/native_client/native_fleet_route_effects.cpp
  app/native_client/native_fleet_workspace.cpp
  app/native_client/native_overview.cpp
  app/native_client/native_shipyard_workspace.cpp)
target_include_directories(stellar_ship_art_tests PRIVATE app/native_client engine/include)
target_link_libraries(stellar_ship_art_tests PRIVATE stellar_native_image stellar_core)
add_test(NAME native_ship_art COMMAND stellar_ship_art_tests "${CMAKE_SOURCE_DIR}")
set_tests_properties(native_ship_art PROPERTIES TIMEOUT 120)
if(MSVC)
  target_compile_options(stellar_ship_art_tests PRIVATE /W4 /WX /permissive-)
endif()

add_executable(stellar_galaxy_marker_tests
  app/native_client/native_galaxy_star_markers.cpp native-tests/native_galaxy_star_markers_tests.cpp)
target_include_directories(stellar_galaxy_marker_tests PRIVATE app/native_client)
target_link_libraries(stellar_galaxy_marker_tests PRIVATE stellar_native_image)
add_test(NAME native_galaxy_star_markers COMMAND stellar_galaxy_marker_tests)

add_executable(stellar_territory_tests
  app/native_client/native_territory_projection.cpp
  app/native_client/native_territory_overlay.cpp
  native-tests/native_territory_projection_tests.cpp)
target_include_directories(stellar_territory_tests PRIVATE app/native_client engine/include)
target_link_libraries(stellar_territory_tests PRIVATE stellar_native_image stellar_core)
add_test(NAME native_territory_projection COMMAND stellar_territory_tests)
set_tests_properties(native_territory_projection PROPERTIES TIMEOUT 120)

add_executable(stellar_orbital_structure_tests
  app/native_client/native_orbital_structure.cpp
  native-tests/native_orbital_structure_tests.cpp)
target_include_directories(stellar_orbital_structure_tests PRIVATE app/native_client engine/include)
target_link_libraries(stellar_orbital_structure_tests PRIVATE stellar_native_image)
add_test(NAME native_orbital_structure COMMAND stellar_orbital_structure_tests)
set_tests_properties(native_orbital_structure PROPERTIES TIMEOUT 60)
if(MSVC)
  target_compile_options(stellar_orbital_structure_tests PRIVATE /W4 /WX /permissive-)
endif()
add_executable(stellar_surface_scene_tests
  app/native_client/native_surface_scene.cpp
  native-tests/native_surface_scene_tests.cpp)
target_include_directories(stellar_surface_scene_tests PRIVATE app/native_client engine/include)
target_link_libraries(stellar_surface_scene_tests PRIVATE stellar_native_image)
add_test(NAME native_surface_scene COMMAND stellar_surface_scene_tests)
set_tests_properties(native_surface_scene PROPERTIES TIMEOUT 60)
if(MSVC)
  target_compile_options(stellar_surface_scene_tests PRIVATE /W4 /WX /permissive-)
endif()

add_executable(stellar_surface_relief_tests
  app/native_client/native_surface_relief.cpp
  native-tests/native_surface_relief_tests.cpp)
target_include_directories(stellar_surface_relief_tests PRIVATE
  app/native_client
  engine/include)
target_link_libraries(stellar_surface_relief_tests PRIVATE stellar_core stellar_native_image)
add_test(NAME native_surface_relief COMMAND stellar_surface_relief_tests)
if(MSVC)
  target_compile_options(stellar_surface_relief_tests PRIVATE /W4 /WX /permissive-)
endif()
if(MSVC)
  target_compile_options(stellar_galaxy_backdrop_tests PRIVATE /W4 /WX /permissive-)
  target_compile_options(stellar_galaxy_marker_tests PRIVATE /W4 /WX /permissive-)
  target_compile_options(stellar_territory_tests PRIVATE /W4 /WX /permissive-)
endif()
