include("${CMAKE_CURRENT_LIST_DIR}/PinnedSDL3.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeUiAssets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeCelestialAssets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativePlanetArtAssets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeStarArtAssets.cmake")
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
  app/native_client/native_orbital_structure.cpp
  app/native_client/native_planet_disc_assets.cpp app/native_client/native_system_travel.cpp)
add_dependencies(stellar-continuum-native stellar_native_ui_assets stellar_native_celestial_assets stellar_native_planet_art_assets stellar_native_star_art_assets stellar_runtime_data)
target_include_directories(stellar-continuum-native PRIVATE "${CMAKE_BINARY_DIR}/generated")
configure_file(app/native_client/windows_version.rc.in generated/native_client_version.rc @ONLY)
target_sources(stellar-continuum-native PRIVATE "${CMAKE_BINARY_DIR}/generated/native_client_version.rc")
target_link_libraries(stellar-continuum-native PRIVATE stellar_native_platform stellar_core SDL3::SDL3 Shell32 Ole32)
add_custom_command(TARGET stellar-continuum-native POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${STELLAR_SDL_runtime}" "$<TARGET_FILE_DIR:stellar-continuum-native>/SDL3.dll")
if(BUILD_TESTING)
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
    "${CMAKE_SOURCE_DIR}/assets/visual")
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
  add_executable(stellar_native_audio_tests
    native-tests/native_audio_tests.cpp app/native_client/native_audio.cpp)
  target_include_directories(stellar_native_audio_tests PRIVATE app/native_client third_party)
  add_test(NAME native_audio COMMAND stellar_native_audio_tests
    "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/native-audio-cases")
  set_tests_properties(native_audio PROPERTIES TIMEOUT 60)
  if(MSVC)
    target_compile_options(stellar_native_audio_tests PRIVATE /WX)
  endif()
  add_executable(stellar_native_voice_tests
    native-tests/native_voice_tests.cpp
    app/native_client/native_voice.cpp
    app/native_client/native_voice_playback.cpp
    app/native_client/native_audio.cpp)
  target_include_directories(stellar_native_voice_tests PRIVATE
    app/native_client engine/include third_party)
  target_link_libraries(stellar_native_voice_tests PRIVATE stellar_core)
  add_test(NAME native_voice COMMAND stellar_native_voice_tests
    "${CMAKE_BINARY_DIR}/native-voice-cases")
  set_tests_properties(native_voice PROPERTIES TIMEOUT 60)
  if(MSVC)
    target_compile_options(stellar_native_voice_tests PRIVATE /WX)
  endif()
  add_executable(stellar_native_voice_settings_tests
    native-tests/native_voice_settings_tests.cpp
    app/native_client/native_voice_settings.cpp
    app/native_client/native_voice.cpp)
  target_include_directories(stellar_native_voice_settings_tests PRIVATE
    app/native_client engine/include third_party)
  target_link_libraries(stellar_native_voice_settings_tests PRIVATE stellar_core)
  add_test(NAME native_voice_settings COMMAND stellar_native_voice_settings_tests)
  if(MSVC)
    target_compile_options(stellar_native_voice_settings_tests PRIVATE /WX)
  endif()
  add_executable(stellar_native_video_settings_tests
    native-tests/native_video_settings_tests.cpp
    app/native_client/native_video_settings.cpp)
  target_include_directories(stellar_native_video_settings_tests PRIVATE
    app/native_client engine/include third_party)
  target_link_libraries(stellar_native_video_settings_tests PRIVATE stellar_core)
  add_test(NAME native_video_settings COMMAND stellar_native_video_settings_tests)
  if(MSVC)
    target_compile_options(stellar_native_video_settings_tests PRIVATE /WX)
  endif()
  add_executable(stellar_native_inspection_tests
    native-tests/native_inspection_tests.cpp
    app/native_client/native_inspection.cpp)
  target_include_directories(stellar_native_inspection_tests PRIVATE
    app/native_client engine/include)
  target_link_libraries(stellar_native_inspection_tests PRIVATE stellar_core)
  add_test(NAME native_inspection COMMAND stellar_native_inspection_tests)
  if(MSVC)
    target_compile_options(stellar_native_inspection_tests PRIVATE /WX)
  endif()
  add_executable(stellar_native_logistics_tests
    native-tests/native_logistics_tests.cpp
    app/native_client/native_logistics.cpp)
  target_include_directories(stellar_native_logistics_tests PRIVATE
    app/native_client engine/include)
  target_link_libraries(stellar_native_logistics_tests PRIVATE stellar_core)
  add_test(NAME native_logistics COMMAND stellar_native_logistics_tests)
  if(MSVC)
    target_compile_options(stellar_native_logistics_tests PRIVATE /WX)
  endif()
  add_executable(stellar_native_overview_tests
    native-tests/native_overview_tests.cpp
    app/native_client/native_overview.cpp)
  target_include_directories(stellar_native_overview_tests PRIVATE
    app/native_client engine/include)
  target_link_libraries(stellar_native_overview_tests PRIVATE stellar_core)
  add_test(NAME native_overview COMMAND stellar_native_overview_tests)
  if(MSVC)
    target_compile_options(stellar_native_overview_tests PRIVATE /WX)
  endif()
  add_executable(stellar_native_missions_tests
    native-tests/native_missions_tests.cpp
    app/native_client/native_missions.cpp)
  target_include_directories(stellar_native_missions_tests PRIVATE
    app/native_client engine/include)
  target_link_libraries(stellar_native_missions_tests PRIVATE stellar_core)
  add_test(NAME native_missions COMMAND stellar_native_missions_tests)
  if(MSVC)
    target_compile_options(stellar_native_missions_tests PRIVATE /WX)
  endif()
  add_executable(stellar_native_economy_tests
    native-tests/native_economy_tests.cpp
    app/native_client/native_economy.cpp)
  target_include_directories(stellar_native_economy_tests PRIVATE
    app/native_client engine/include)
  target_link_libraries(stellar_native_economy_tests PRIVATE stellar_core)
  add_test(NAME native_economy COMMAND stellar_native_economy_tests)
  if(MSVC)
    target_compile_options(stellar_native_economy_tests PRIVATE /WX)
  endif()
  add_executable(stellar_native_campaign_session_tests
    native-tests/native_campaign_session_tests.cpp app/native_client/native_campaign_session.cpp
    app/native_client/native_notifications.cpp)
  target_include_directories(stellar_native_campaign_session_tests PRIVATE app/native_client engine/include)
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
  app/native_client/native_surface_workspace.cpp
  app/native_client/native_surface_scene.cpp)

target_sources(stellar-continuum-native PRIVATE
  app/native_client/native_battle_workspace.cpp)


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

include("${CMAKE_CURRENT_LIST_DIR}/NativeAudioAssets.cmake")
add_dependencies(stellar-continuum-native stellar_native_audio_assets)
include("${CMAKE_CURRENT_LIST_DIR}/NativeVoiceAssets.cmake")
add_dependencies(stellar-continuum-native stellar_native_voice_assets)
target_sources(stellar-continuum-native PRIVATE
  app/native_client/native_audio.cpp
  app/native_client/native_audio_device.cpp
  app/native_client/native_audio_settings.cpp
  app/native_client/native_inspection.cpp
  app/native_client/native_economy.cpp
  app/native_client/native_logistics.cpp
  app/native_client/native_missions.cpp
  app/native_client/native_notifications.cpp
  app/native_client/native_overview.cpp
  app/native_client/native_support.cpp
  app/native_client/native_video_settings.cpp
  app/native_client/native_voice.cpp
  app/native_client/native_voice_bridge.cpp
  app/native_client/native_voice_playback.cpp
  app/native_client/native_voice_sapi.cpp
  app/native_client/native_voice_settings.cpp)
target_include_directories(stellar-continuum-native PRIVATE third_party)
