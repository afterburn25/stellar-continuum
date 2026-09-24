include("${CMAKE_CURRENT_LIST_DIR}/NativeBrandingAssets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/PinnedSDL3.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeAudio.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeAudioSettings.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeGeneralSettings.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeVideoSettings.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeAudioAssets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeNavigationAssets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeResearchAssets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeUiAssets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeCelestialAssets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeSmallBodyAssets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativePlanetAssets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeScene3D.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeLeaderArtAssets.cmake")
add_library(stellar_native_platform STATIC engine/src/native_map_platform.cpp engine/src/native_scene3d_gpu.cpp)
target_include_directories(stellar_native_platform PUBLIC engine/include)
target_link_libraries(stellar_native_platform PUBLIC stellar_native_image
  PRIVATE SDL3::SDL3 Gdi32 User32 Shell32)

# Ready-made windowed 2D game host: owns the SDL loop, ECS world, scene
# documents, content resolution, audio and quicksave so game projects get a
# running loop from the engine instead of generated glue code.
add_library(stellar_engine_runtime STATIC engine/src/runtime_host.cpp)
target_include_directories(stellar_engine_runtime PUBLIC engine/include)
target_link_libraries(stellar_engine_runtime PUBLIC stellar_engine
  stellar_native_platform stellar_native_audio)
if(MSVC)
  target_compile_options(stellar_engine_runtime PRIVATE /WX)
endif()

# Standalone engine shell: a windowed host that links only the engine
# libraries. It exists so the engine can run, be demonstrated, and be
# shipped without the Stellar Continuum game module.
add_executable(stellar-engine app/engine_main.cpp)
target_include_directories(stellar-engine PRIVATE "${CMAKE_BINARY_DIR}/generated")
configure_file(app/engine_version.rc.in generated/engine_version.rc @ONLY)
if(WIN32)
  target_sources(stellar-engine PRIVATE "${CMAKE_BINARY_DIR}/generated/engine_version.rc")
endif()
target_link_libraries(stellar-engine PRIVATE stellar_native_platform stellar_engine)
if(WIN32)
  target_link_libraries(stellar-engine PRIVATE stellar_asset_cooker)
endif()
add_custom_command(TARGET stellar-engine POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${STELLAR_SDL_runtime}" "$<TARGET_FILE_DIR:stellar-engine>/SDL3.dll")
if(MSVC)
  target_compile_options(stellar-engine PRIVATE /W4 /WX /permissive-)
  target_compile_definitions(stellar-engine PRIVATE
    STELLAR_CMAKE_COMMAND="${CMAKE_COMMAND}")
endif()

# Per-tool smoke coverage: --frames N renders N frames and exits, so each
# tool's init+render path runs under ctest without interaction.
if(BUILD_TESTING)
  foreach(tool IN ITEMS Projects Dashboard Scene Scene3D Assets Profiler
                        Localization Simulation Colony Economy Planet AI
                        Warfare Missions Physics Galaxy)
    string(TOLOWER "${tool}" tool_lower)
    add_test(NAME "engine_shell_tool_${tool_lower}"
      COMMAND stellar-engine --tool "${tool}" --frames 20)
  endforeach()
endif()

# Engine SDK export: stages the redistributable headers, prebuilt libraries,
# SDL3 runtime and the consumer CMake config under engine-sdk/ beside the
# shell, so scaffolded game projects can compile and link without this
# source tree. Building stellar-engine keeps the SDK fresh.
if(WIN32 AND TARGET stellar_asset_cooker)
  set(STELLAR_ENGINE_SDK_DIR "${CMAKE_BINARY_DIR}/engine-sdk")
  add_custom_target(stellar-engine-sdk
    COMMAND ${CMAKE_COMMAND} -E make_directory "${STELLAR_ENGINE_SDK_DIR}/include"
      "${STELLAR_ENGINE_SDK_DIR}/lib" "${STELLAR_ENGINE_SDK_DIR}/bin"
      "${STELLAR_ENGINE_SDK_DIR}/cmake"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/engine/include"
      "${STELLAR_ENGINE_SDK_DIR}/include"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/third_party/nlohmann"
      "${STELLAR_ENGINE_SDK_DIR}/include/nlohmann"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${STELLAR_SDL_ROOT}/include"
      "${STELLAR_ENGINE_SDK_DIR}/include"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:stellar_engine>"
      "$<TARGET_FILE:stellar_native_platform>" "$<TARGET_FILE:stellar_native_image>"
      "$<TARGET_FILE:stellar_texture_codecs>" "$<TARGET_FILE:stellar_asset_cooker>"
      "$<TARGET_FILE:stellar_native_audio>" "$<TARGET_FILE:stellar_engine_runtime>"
      "${STELLAR_ENGINE_SDK_DIR}/lib/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${STELLAR_SDL_importLibrary}"
      "${STELLAR_ENGINE_SDK_DIR}/lib/SDL3.lib"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${STELLAR_SDL_runtime}"
      "${STELLAR_ENGINE_SDK_DIR}/bin/SDL3.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/assets/visual/fonts/Rajdhani-SemiBold.ttf"
      "${STELLAR_ENGINE_SDK_DIR}/bin/engine-default-font.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/assets/visual/fonts/OFL-Rajdhani.txt"
      "${STELLAR_ENGINE_SDK_DIR}/bin/OFL-Rajdhani.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_LIST_DIR}/StellarEngineSdk.cmake"
      "${STELLAR_ENGINE_SDK_DIR}/cmake/StellarEngineSdk.cmake"
    DEPENDS stellar_engine stellar_native_platform stellar_native_image
      stellar_texture_codecs stellar_asset_cooker stellar_native_audio
      stellar_engine_runtime
    COMMENT "Exporting engine SDK to engine-sdk/")
  add_dependencies(stellar-engine stellar-engine-sdk)
endif()

# Native C++23 editor host: engine + core libraries linked directly (the WPF
# 0.1.9 editor on work/stellar-engine-editor drove a pinned runtime as a
# hidden child process; this host calls the same generation APIs in-process).
add_executable(stellar-editor app/editor_main.cpp app/editor_project.cpp)
target_include_directories(stellar-editor PRIVATE "${CMAKE_BINARY_DIR}/generated")
configure_file(app/editor_version.rc.in generated/editor_version.rc @ONLY)
if(WIN32)
  target_sources(stellar-editor PRIVATE "${CMAKE_BINARY_DIR}/generated/editor_version.rc")
endif()
target_link_libraries(stellar-editor PRIVATE stellar_native_platform stellar_core stellar_engine stellar_json)
add_custom_command(TARGET stellar-editor POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${STELLAR_SDL_runtime}" "$<TARGET_FILE_DIR:stellar-editor>/SDL3.dll")
if(MSVC)
  target_compile_options(stellar-editor PRIVATE /W4 /WX /permissive-)
endif()

add_executable(stellar-continuum-native app/native_client/main.cpp
  app/native_client/native_phenomena.cpp
  app/native_client/native_campaign_session.cpp app/native_client/native_research_controller.cpp
  app/native_client/native_research_workspace.cpp app/native_client/native_fleet_controller.cpp
  app/native_client/native_fleet_workspace.cpp app/native_client/native_fleet_presentation.cpp
  app/native_client/native_shipyard_controller.cpp app/native_client/native_shipyard_workspace.cpp
  app/native_client/native_construction_controller.cpp app/native_client/native_construction_workspace.cpp
  app/native_client/native_system_view.cpp app/native_client/native_system_workspace.cpp
  app/native_client/native_small_body_renderer.cpp app/native_client/native_small_body_panel.cpp
  app/native_client/native_logistics.cpp app/native_client/native_logistics_workspace.cpp
  app/native_client/native_economy.cpp app/native_client/native_economy_workspace.cpp
  app/native_client/native_body_inspection.cpp app/native_client/native_body_inspection_panel.cpp
  app/native_client/native_inspection.cpp
  app/native_client/native_planet_disc_assets.cpp app/native_client/native_system_travel.cpp)
add_dependencies(stellar-continuum-native stellar_native_ui_assets stellar_native_celestial_assets stellar_runtime_data)
add_dependencies(stellar-continuum-native stellar_native_small_body_assets)
add_dependencies(stellar-continuum-native stellar_native_planet_assets)
target_include_directories(stellar-continuum-native PRIVATE "${CMAKE_BINARY_DIR}/generated")
configure_file(app/native_client/windows_version.rc.in generated/native_client_version.rc @ONLY)
target_sources(stellar-continuum-native PRIVATE "${CMAKE_BINARY_DIR}/generated/native_client_version.rc")
target_link_libraries(stellar-continuum-native PRIVATE stellar_native_platform stellar_core stellar_json Shell32 Ole32)
if(WIN32)
  target_sources(stellar-continuum-native PRIVATE app/native_client/native_accessibility_bridge.cpp)
  target_link_libraries(stellar-continuum-native PRIVATE Uiautomationcore OleAut32)
endif()
target_link_libraries(stellar-continuum-native PRIVATE stellar_native_navigation_art)
target_link_libraries(stellar-continuum-native PRIVATE stellar_native_research_art)
add_dependencies(stellar-continuum-native stellar_native_research_assets)
add_dependencies(stellar-continuum-native stellar_native_navigation_assets)
target_link_libraries(stellar-continuum-native PRIVATE stellar_native_audio stellar_native_audio_settings stellar_native_general_settings stellar_native_video_settings stellar_native_campaign_feedback)
target_sources(stellar-continuum-native PRIVATE app/native_client/native_audio_director.cpp)
target_sources(stellar-continuum-native PRIVATE
  app/native_client/native_notifications.cpp app/native_client/native_notification_events.cpp
  app/native_client/native_chronicle.cpp
  app/native_client/native_support.cpp app/native_client/native_support_service.cpp
  app/native_client/native_battle_workspace.cpp
  app/native_client/native_battle_art.cpp
  app/native_client/native_battle_sprites.cpp)
add_dependencies(stellar-continuum-native stellar_native_audio_assets)
target_sources(stellar-continuum-native PRIVATE
  app/native_client/native_orbital_structure.cpp)
add_custom_command(TARGET stellar-continuum-native POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${STELLAR_SDL_runtime}" "$<TARGET_FILE_DIR:stellar-continuum-native>/SDL3.dll")
if(BUILD_TESTING)
  add_executable(stellar_native_moon_tests native-tests/native_moon_tests.cpp
    app/native_client/native_system_view.cpp app/native_client/native_system_workspace.cpp
    app/native_client/native_system_travel.cpp app/native_client/native_fleet_controller.cpp
    app/native_client/native_celestial_appearance.cpp app/native_client/native_small_body_renderer.cpp
    app/native_client/native_small_body_panel.cpp app/native_client/native_body_inspection.cpp
    app/native_client/native_body_inspection_panel.cpp)
  target_include_directories(stellar_native_moon_tests PRIVATE app/native_client)
  target_link_libraries(stellar_native_moon_tests PRIVATE stellar_native_platform stellar_core stellar_json)
  add_test(NAME native_moons COMMAND stellar_native_moon_tests "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/moon-test-captures")
  set_tests_properties(native_moons PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE
    SKIP_REGULAR_EXPRESSION "GPU device creation failed")
  add_executable(stellar_native_giant_visual_tests native-tests/native_giant_visual_tests.cpp app/native_client/native_system_view.cpp app/native_client/native_small_body_renderer.cpp)
  target_include_directories(stellar_native_giant_visual_tests PRIVATE app/native_client)
  target_link_libraries(stellar_native_giant_visual_tests PRIVATE stellar_native_platform stellar_core stellar_json)
  add_test(NAME native_giant_visual COMMAND stellar_native_giant_visual_tests "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/giant-test-captures")
  set_tests_properties(native_giant_visual PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE
    SKIP_REGULAR_EXPRESSION "GPU device creation failed")
  add_executable(stellar_scene3d_gpu_tests native-tests/native_scene3d_gpu_tests.cpp)
  target_link_libraries(stellar_scene3d_gpu_tests PRIVATE stellar_native_platform)
  add_test(NAME native_scene3d_gpu COMMAND stellar_scene3d_gpu_tests
    "${CMAKE_SOURCE_DIR}/assets/visual/fonts/Rajdhani-SemiBold.ttf" "${CMAKE_BINARY_DIR}/scene3d-test-captures")
  set_tests_properties(native_scene3d_gpu PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE
    SKIP_REGULAR_EXPRESSION "GPU device creation failed")
  if(MSVC)
    target_compile_options(stellar_scene3d_gpu_tests PRIVATE /WX)
  endif()
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
  set_tests_properties(native_text_measure PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE
    SKIP_REGULAR_EXPRESSION "GPU device creation failed")
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
  if(WIN32)
    add_executable(stellar_native_accessibility_bridge_tests
      native-tests/native_accessibility_bridge_tests.cpp
      app/native_client/native_accessibility_bridge.cpp)
    target_include_directories(stellar_native_accessibility_bridge_tests PRIVATE app/native_client engine/include)
    target_link_libraries(stellar_native_accessibility_bridge_tests PRIVATE
      Uiautomationcore Ole32 OleAut32 User32)
    add_test(NAME native_accessibility_bridge COMMAND stellar_native_accessibility_bridge_tests)
    set_tests_properties(native_accessibility_bridge PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    if(MSVC)
      target_compile_options(stellar_native_accessibility_bridge_tests PRIVATE /WX)
    endif()
  endif()
  add_executable(stellar_native_client_platform_tests native-tests/native_client_platform_tests.cpp)
  target_link_libraries(stellar_native_client_platform_tests PRIVATE stellar_native_platform SDL3::SDL3)
  add_test(NAME native_client_platform COMMAND stellar_native_client_platform_tests
    "${CMAKE_SOURCE_DIR}/assets/visual/fonts/Rajdhani-SemiBold.ttf"
    "${CMAKE_SOURCE_DIR}/assets/visual/sol/earth.jpg"
    "${CMAKE_SOURCE_DIR}/assets/visual/space/campaign-galaxy-four-arm-v1.png")
  set_tests_properties(native_client_platform PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE
    SKIP_REGULAR_EXPRESSION "GPU device creation failed")
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
    add_executable(stellar_native_campaign_session_tests
    native-tests/native_campaign_session_tests.cpp app/native_client/native_campaign_session.cpp
    app/native_client/native_notifications.cpp)
  target_include_directories(stellar_native_campaign_session_tests PRIVATE app/native_client engine/include)
  target_link_libraries(stellar_native_campaign_session_tests PRIVATE stellar_core stellar_engine stellar_json Shell32 Ole32)
  add_test(NAME native_campaign_session COMMAND stellar_native_campaign_session_tests
    "${CMAKE_SOURCE_DIR}/native-tests/fixtures/player-campaign-json.json"
    "${CMAKE_SOURCE_DIR}/data/research/v1" "${CMAKE_BINARY_DIR}/native-session-cases")
  set_tests_properties(native_campaign_session PROPERTIES TIMEOUT 180)

endif()
if(MSVC)
  target_compile_options(stellar_native_platform PRIVATE /WX)
  target_compile_options(stellar-continuum-native PRIVATE /WX /bigobj)
endif()

target_sources(stellar-continuum-native PRIVATE
  app/native_client/native_colony_controller.cpp
  app/native_client/native_colony_roster.cpp app/native_client/native_controlled_assets.cpp
  app/native_client/native_outpost_freight_controller.cpp
  app/native_client/native_colony_workspace.cpp)

target_sources(stellar-continuum-native PRIVATE
  app/native_client/native_settlement_mission_controller.cpp
  app/native_client/native_settlement_preparation.cpp
  app/native_client/native_settlement_workspace.cpp)

target_sources(stellar-continuum-native PRIVATE
  app/native_client/native_surface_construction_controller.cpp
  app/native_client/native_surface_relief.cpp
  app/native_client/native_surface_scene.cpp)


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
include("${CMAKE_CURRENT_LIST_DIR}/NativePhenomenonArtAssets.cmake")
add_dependencies(stellar-continuum-native stellar_native_phenomenon_art_assets)
add_dependencies(stellar-continuum-native stellar_native_galaxy_art_assets)

target_sources(stellar-continuum-native PRIVATE
  app/native_client/native_galaxy_backdrop.cpp
  app/native_client/native_galaxy_star_markers.cpp
  app/native_client/native_stellar_art.cpp
  app/native_client/native_stellar_eruptions.cpp
  app/native_client/native_territory_projection.cpp
  app/native_client/native_territory_overlay.cpp)

# The supplied stellar artwork has its own content-addressed inventory and LOD manifest.
file(READ "${CMAKE_SOURCE_DIR}/assets/visual/stellar/manifest.json" STELLAR_ART_MANIFEST)
string(JSON STELLAR_ART_COUNT LENGTH "${STELLAR_ART_MANIFEST}" files)
math(EXPR STELLAR_ART_LAST "${STELLAR_ART_COUNT}-1")
foreach(INDEX RANGE 0 ${STELLAR_ART_LAST})
  string(JSON NAME GET "${STELLAR_ART_MANIFEST}" files ${INDEX} filename)
  string(JSON EXPECTED GET "${STELLAR_ART_MANIFEST}" files ${INDEX} sha256)
  file(SHA256 "${CMAKE_SOURCE_DIR}/assets/visual/stellar/${NAME}" ACTUAL)
  if(NOT ACTUAL STREQUAL EXPECTED)
    message(FATAL_ERROR "Supplied stellar artwork digest mismatch: ${NAME}")
  endif()
endforeach()
if(BUILD_TESTING)
  add_executable(stellar_native_stellar_art_tests native-tests/native_stellar_art_tests.cpp app/native_client/native_stellar_art.cpp)
  target_include_directories(stellar_native_stellar_art_tests PRIVATE app/native_client)
  target_link_libraries(stellar_native_stellar_art_tests PRIVATE stellar_native_platform stellar_json)
  add_test(NAME native_stellar_art COMMAND stellar_native_stellar_art_tests "${CMAKE_SOURCE_DIR}")
  set_tests_properties(native_stellar_art PROPERTIES TIMEOUT 90)
endif()
add_custom_target(stellar_native_stellar_art
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/visual/stellar" "${CMAKE_BINARY_DIR}/assets/visual/stellar")
add_dependencies(stellar-continuum-native stellar_native_stellar_art)

include("${CMAKE_CURRENT_LIST_DIR}/NativeShipArtAssets.cmake")
add_dependencies(stellar-continuum-native stellar_native_ship_art_assets)
add_dependencies(stellar-continuum-native stellar_native_leader_art_assets)
target_sources(stellar-continuum-native PRIVATE
  app/native_client/native_ship_art_assets.cpp
  app/native_client/native_fleet_route_effects.cpp)

target_sources(stellar-continuum-native PRIVATE
  app/native_client/native_diplomacy_controller.cpp
  app/native_client/native_diplomacy_workspace.cpp)


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

add_custom_target(stellar_native_eruption_assets
 COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/visual/stellar-eruptions" "$<TARGET_FILE_DIR:stellar-continuum-native>/assets/visual/stellar-eruptions")
add_dependencies(stellar-continuum-native stellar_native_eruption_assets)

if(BUILD_TESTING)
 add_executable(stellar_cooked_eruption_tests native-tests/cooked_eruption_tests.cpp app/native_client/native_stellar_eruptions.cpp)
 target_include_directories(stellar_cooked_eruption_tests PRIVATE app/native_client)
 target_link_libraries(stellar_cooked_eruption_tests PRIVATE stellar_core stellar_native_platform stellar_json)
 add_executable(stellar_native_eruption_tests native-tests/native_stellar_eruption_tests.cpp app/native_client/native_stellar_eruptions.cpp app/native_client/native_general_settings.cpp)
 target_include_directories(stellar_native_eruption_tests PRIVATE app/native_client)
 target_link_libraries(stellar_native_eruption_tests PRIVATE stellar_core stellar_native_platform stellar_json)
 add_test(NAME native_stellar_eruptions COMMAND stellar_native_eruption_tests "${CMAKE_SOURCE_DIR}")
 set_tests_properties(native_stellar_eruptions PROPERTIES TIMEOUT 180)
 add_executable(stellar_native_navigation_visual_tests native-tests/native_navigation_visual_tests.cpp app/native_client/native_system_travel.cpp app/native_client/native_system_view.cpp)
 target_include_directories(stellar_native_navigation_visual_tests PRIVATE app/native_client)
 target_link_libraries(stellar_native_navigation_visual_tests PRIVATE stellar_core stellar_native_platform)
 add_test(NAME native_navigation_visual COMMAND stellar_native_navigation_visual_tests "${CMAKE_SOURCE_DIR}/assets/visual/fonts/Rajdhani-SemiBold.ttf" "${CMAKE_BINARY_DIR}/navigation-visual")
 set_tests_properties(native_navigation_visual PROPERTIES
   SKIP_REGULAR_EXPRESSION "GPU device creation failed")
endif()

add_custom_target(stellar_native_starfield_assets ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/assets/visual/starfields"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/assets/visual/starfields/faint-map-reference.png" "${CMAKE_BINARY_DIR}/assets/visual/starfields/faint-map-reference.png")
add_dependencies(stellar-continuum-native stellar_native_starfield_assets)

if(BUILD_TESTING)
 add_executable(stellar_system_background_tests native-tests/system_background_tests.cpp app/native_client/native_phenomena.cpp)
 target_include_directories(stellar_system_background_tests PRIVATE app/native_client)
 target_link_libraries(stellar_system_background_tests PRIVATE stellar_native_platform stellar_core stellar_json)
 add_test(NAME system_background COMMAND stellar_system_background_tests "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/sky-test-captures")
 set_tests_properties(system_background PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE
   SKIP_REGULAR_EXPRESSION "GPU device creation failed")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/NativeVoiceAssets.cmake")
add_dependencies(stellar-continuum-native stellar_native_voice_assets)
target_sources(stellar-continuum-native PRIVATE
  app/native_client/native_audio.cpp
  app/native_client/native_audio_device.cpp
  app/native_client/native_audio_settings.cpp
  app/native_client/native_missions.cpp
  app/native_client/native_overview.cpp
  app/native_client/native_voice.cpp
  app/native_client/native_voice_bridge.cpp
  app/native_client/native_voice_playback.cpp
  app/native_client/native_voice_sapi.cpp
  app/native_client/native_voice_settings.cpp)
target_include_directories(stellar-continuum-native PRIVATE third_party)
