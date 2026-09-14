include("${CMAKE_CURRENT_LIST_DIR}/PinnedSDL3.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/NativeUiAssets.cmake")
add_library(stellar_native_platform STATIC engine/src/native_map_platform.cpp)
target_include_directories(stellar_native_platform PUBLIC engine/include)
target_link_libraries(stellar_native_platform PRIVATE SDL3::SDL3 Gdi32 User32)
add_executable(stellar-continuum-native app/native_client/main.cpp
  app/native_client/native_campaign_session.cpp app/native_client/native_research_controller.cpp
  app/native_client/native_research_workspace.cpp app/native_client/native_fleet_controller.cpp
  app/native_client/native_fleet_workspace.cpp app/native_client/native_fleet_presentation.cpp)
add_dependencies(stellar-continuum-native stellar_native_ui_assets stellar_runtime_data)
target_include_directories(stellar-continuum-native PRIVATE "${CMAKE_BINARY_DIR}/generated")
configure_file(app/native_client/windows_version.rc.in generated/native_client_version.rc @ONLY)
target_sources(stellar-continuum-native PRIVATE "${CMAKE_BINARY_DIR}/generated/native_client_version.rc")
target_link_libraries(stellar-continuum-native PRIVATE stellar_native_platform stellar_core Shell32 Ole32)
add_custom_command(TARGET stellar-continuum-native POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${STELLAR_SDL_runtime}" "$<TARGET_FILE_DIR:stellar-continuum-native>/SDL3.dll")
if(BUILD_TESTING)
  add_executable(stellar_native_client_platform_tests native-tests/native_client_platform_tests.cpp)
  target_link_libraries(stellar_native_client_platform_tests PRIVATE stellar_native_platform SDL3::SDL3)
  add_test(NAME native_client_platform COMMAND stellar_native_client_platform_tests
    "${CMAKE_SOURCE_DIR}/assets/visual/fonts/Rajdhani-SemiBold.ttf")
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
