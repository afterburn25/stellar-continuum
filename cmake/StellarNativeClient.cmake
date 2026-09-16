include("${CMAKE_CURRENT_LIST_DIR}/PinnedSDL3.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeAudio.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeAudioSettings.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeVideoSettings.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeAudioAssets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeSurfaceArtAssets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeNavigationAssets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeUiAssets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeCelestialAssets.cmake")
add_library(stellar_native_platform STATIC engine/src/native_map_platform.cpp)
target_include_directories(stellar_native_platform PUBLIC engine/include)
target_link_libraries(stellar_native_platform PUBLIC stellar_native_image
  PRIVATE SDL3::SDL3 Gdi32 User32)
add_executable(stellar-continuum-native app/native_client/main.cpp
  app/native_client/native_campaign_session.cpp app/native_client/native_research_controller.cpp
  app/native_client/native_research_workspace.cpp app/native_client/native_fleet_controller.cpp
  app/native_client/native_fleet_workspace.cpp app/native_client/native_fleet_presentation.cpp
  app/native_client/native_shipyard_controller.cpp app/native_client/native_shipyard_workspace.cpp
  app/native_client/native_construction_controller.cpp app/native_client/native_construction_workspace.cpp
  app/native_client/native_system_view.cpp app/native_client/native_system_workspace.cpp
  app/native_client/native_logistics.cpp app/native_client/native_logistics_workspace.cpp
  app/native_client/native_economy.cpp app/native_client/native_economy_workspace.cpp
  app/native_client/native_body_inspection.cpp app/native_client/native_body_inspection_panel.cpp
  app/native_client/native_inspection.cpp
  app/native_client/native_planet_disc_assets.cpp app/native_client/native_system_travel.cpp)
add_dependencies(stellar-continuum-native stellar_native_ui_assets stellar_native_celestial_assets stellar_runtime_data)
target_include_directories(stellar-continuum-native PRIVATE "${CMAKE_BINARY_DIR}/generated")
configure_file(app/native_client/windows_version.rc.in generated/native_client_version.rc @ONLY)
target_sources(stellar-continuum-native PRIVATE "${CMAKE_BINARY_DIR}/generated/native_client_version.rc")
target_link_libraries(stellar-continuum-native PRIVATE stellar_native_platform stellar_core Shell32 Ole32)
target_link_libraries(stellar-continuum-native PRIVATE stellar_native_navigation_art)
add_dependencies(stellar-continuum-native stellar_native_navigation_assets)
target_link_libraries(stellar-continuum-native PRIVATE stellar_native_audio stellar_native_audio_settings stellar_native_video_settings stellar_native_campaign_feedback)
target_sources(stellar-continuum-native PRIVATE app/native_client/native_audio_director.cpp)
target_sources(stellar-continuum-native PRIVATE
  app/native_client/native_notifications.cpp app/native_client/native_notification_events.cpp
  app/native_client/native_support.cpp app/native_client/native_support_service.cpp
  app/native_client/native_battle_workspace.cpp
  app/native_client/native_battle_art.cpp
  app/native_client/native_battle_sprites.cpp)
target_sources(stellar-continuum-native PRIVATE app/native_client/native_surface_art_assets.cpp)
add_dependencies(stellar-continuum-native stellar_native_surface_art_assets)
add_dependencies(stellar-continuum-native stellar_native_audio_assets)
add_custom_command(TARGET stellar-continuum-native POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${STELLAR_SDL_runtime}" "$<TARGET_FILE_DIR:stellar-continuum-native>/SDL3.dll")
if(BUILD_TESTING)
  add_executable(stellar_native_audio_director_tests
    native-tests/native_audio_director_tests.cpp app/native_client/native_audio_director.cpp)
  target_include_directories(stellar_native_audio_director_tests PRIVATE app/native_client)
  target_link_libraries(stellar_native_audio_director_tests PRIVATE stellar_native_audio stellar_engine)
  add_custom_command(TARGET stellar_native_audio_director_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${STELLAR_SDL_runtime}"
      "$<TARGET_FILE_DIR:stellar_native_audio_director_tests>/SDL3.dll")
  add_test(NAME native_audio_director COMMAND stellar_native_audio_director_tests "${CMAKE_SOURCE_DIR}")
  set_tests_properties(native_audio_director PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE
    ENVIRONMENT "SDL_AUDIODRIVER=dummy")
  if(MSVC)
    target_compile_options(stellar_native_audio_director_tests PRIVATE /WX)
  endif()
  add_executable(stellar_native_text_measure_tests native-tests/native_text_measure_tests.cpp)
  target_link_libraries(stellar_native_text_measure_tests PRIVATE stellar_native_platform)
  add_test(NAME native_text_measure COMMAND stellar_native_text_measure_tests
    "${CMAKE_SOURCE_DIR}/assets/visual/fonts/Rajdhani-SemiBold.ttf")
  set_tests_properties(native_text_measure PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
  if(MSVC)
    target_compile_options(stellar_native_text_measure_tests PRIVATE /WX)
  endif()
  add_executable(stellar_native_planet_disc_assets_tests
    native-tests/native_planet_disc_assets_tests.cpp app/native_client/native_planet_disc_assets.cpp)
  target_include_directories(stellar_native_planet_disc_assets_tests PRIVATE app/native_client)
  target_link_libraries(stellar_native_planet_disc_assets_tests PRIVATE stellar_native_platform stellar_core)
  add_test(NAME native_planet_disc_assets COMMAND stellar_native_planet_disc_assets_tests
    "${CMAKE_SOURCE_DIR}/assets/visual/sol")
  set_tests_properties(native_planet_disc_assets PROPERTIES TIMEOUT 90)
  if(MSVC)
    target_compile_options(stellar_native_planet_disc_assets_tests PRIVATE /WX)
  endif()
  add_executable(stellar_native_client_platform_tests native-tests/native_client_platform_tests.cpp)
  target_link_libraries(stellar_native_client_platform_tests PRIVATE stellar_native_platform SDL3::SDL3)
  add_test(NAME native_client_platform COMMAND stellar_native_client_platform_tests
    "${CMAKE_SOURCE_DIR}/assets/visual/fonts/Rajdhani-SemiBold.ttf"
    "${CMAKE_SOURCE_DIR}/assets/visual/sol/earth.jpg"
    "${CMAKE_SOURCE_DIR}/assets/visual/space/campaign-galaxy-four-arm-v1.png")
  set_tests_properties(native_client_platform PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
  add_executable(stellar_native_campaign_session_tests
    native-tests/native_campaign_session_tests.cpp app/native_client/native_campaign_session.cpp)
  target_include_directories(stellar_native_campaign_session_tests PRIVATE app/native_client)
  target_link_libraries(stellar_native_campaign_session_tests PRIVATE stellar_core stellar_json Shell32 Ole32)
  add_test(NAME native_campaign_session COMMAND stellar_native_campaign_session_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/player-campaign-json.json"
    "${CMAKE_SOURCE_DIR}/data/research/v1" "${CMAKE_BINARY_DIR}/native-session-cases")
  set_tests_properties(native_campaign_session PROPERTIES TIMEOUT 180)
endif()
if(MSVC)
  target_compile_options(stellar_native_platform PRIVATE /WX)
  target_compile_options(stellar-continuum-native PRIVATE /WX)
endif()

target_sources(stellar-continuum-native PRIVATE
  app/native_client/native_colony_controller.cpp
  app/native_client/native_colony_workspace.cpp)

target_sources(stellar-continuum-native PRIVATE
  app/native_client/native_settlement_mission_controller.cpp
  app/native_client/native_settlement_workspace.cpp)

target_sources(stellar-continuum-native PRIVATE
  app/native_client/native_surface_construction_controller.cpp
  app/native_client/native_surface_building_geometry.cpp
  app/native_client/native_surface_building_raster.cpp
  app/native_client/native_surface_building_assets.cpp
  app/native_client/native_surface_building_presentation.cpp
  app/native_client/native_surface_building_layer.cpp
  app/native_client/native_surface_relief.cpp
  app/native_client/native_surface_scene.cpp
  app/native_client/native_surface_workspace.cpp)


include("${CMAKE_CURRENT_LIST_DIR}/NativeSpeciesAssets.cmake")
add_dependencies(stellar-continuum-native stellar_native_species_assets)
target_sources(stellar-continuum-native PRIVATE
  app/native_client/native_new_campaign_setup.cpp
  app/native_client/native_new_game_workspace.cpp
  app/native_client/native_new_campaign_generation.cpp)

target_sources(stellar-continuum-native PRIVATE
  app/native_client/native_startup_session.cpp)

target_sources(stellar-continuum-native PRIVATE
  app/native_client/native_startup_workspace.cpp
  app/native_client/native_startup_host.cpp
  app/native_client/native_startup_entry.cpp)

include("${CMAKE_CURRENT_LIST_DIR}/NativeStartupArtAssets.cmake")
add_dependencies(stellar-continuum-native stellar_native_startup_art_assets)

target_sources(stellar-continuum-native PRIVATE app/native_client/native_startup_artwork.cpp)

target_sources(stellar-continuum-native PRIVATE app/native_client/native_celestial_appearance.cpp)
target_sources(stellar-continuum-native PRIVATE app/native_client/native_galaxy_labels.cpp)

include("${CMAKE_CURRENT_LIST_DIR}/NativeGalaxyArtAssets.cmake")
add_dependencies(stellar-continuum-native stellar_native_galaxy_art_assets)

target_sources(stellar-continuum-native PRIVATE
  app/native_client/native_galaxy_backdrop.cpp
  app/native_client/native_galaxy_star_markers.cpp
  app/native_client/native_territory_projection.cpp
  app/native_client/native_territory_overlay.cpp)

include("${CMAKE_CURRENT_LIST_DIR}/NativeShipArtAssets.cmake")
add_dependencies(stellar-continuum-native stellar_native_ship_art_assets)
target_sources(stellar-continuum-native PRIVATE
  app/native_client/native_ship_art_assets.cpp
  app/native_client/native_fleet_route_effects.cpp)

target_sources(stellar-continuum-native PRIVATE
  app/native_client/native_diplomacy_controller.cpp
  app/native_client/native_diplomacy_workspace.cpp)

include("${CMAKE_CURRENT_LIST_DIR}/NativeSurfaceVisualTests.cmake")

if(BUILD_TESTING)
  add_executable(stellar_battle_sprites_tests
    native-tests/native_battle_sprites_tests.cpp
    app/native_client/native_battle_sprites.cpp
    app/native_client/native_battle_workspace.cpp)
  target_include_directories(stellar_battle_sprites_tests PRIVATE app/native_client)
  target_link_libraries(stellar_battle_sprites_tests PRIVATE stellar_native_image stellar_core)
  if(MSVC)
    target_compile_options(stellar_battle_sprites_tests PRIVATE /W4 /WX /permissive-)
  endif()
  add_test(NAME native_battle_sprites COMMAND stellar_battle_sprites_tests "${CMAKE_SOURCE_DIR}")
endif()
