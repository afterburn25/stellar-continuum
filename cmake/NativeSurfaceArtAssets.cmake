# Only the native graphical client packages this reviewed surface illustration.
file(READ "${CMAKE_SOURCE_DIR}/export/native-surface-art-assets.json" STELLAR_SURFACE_ART_ASSETS)
string(JSON STELLAR_SURFACE_ART_SCHEMA GET "${STELLAR_SURFACE_ART_ASSETS}" schemaVersion)
string(JSON STELLAR_SURFACE_ART_COUNT LENGTH "${STELLAR_SURFACE_ART_ASSETS}" assets)
if(NOT STELLAR_SURFACE_ART_SCHEMA EQUAL 1 OR NOT STELLAR_SURFACE_ART_COUNT EQUAL 2)
  message(FATAL_ERROR "Unsupported native surface art asset declaration")
endif()
add_custom_target(stellar_native_surface_art_assets)
foreach(STELLAR_SURFACE_ART_KEY IN ITEMS temperate-ground-albedo-v1 credits)
  if(STELLAR_SURFACE_ART_KEY STREQUAL "temperate-ground-albedo-v1")
    set(STELLAR_SURFACE_ART_EXPECTED_SOURCE
      "assets/visual/surface/temperate-ground-albedo-v1.png")
    set(STELLAR_SURFACE_ART_EXPECTED_RUNTIME
      "assets/visual/surface/temperate-ground-albedo-v1.png")
  else()
    set(STELLAR_SURFACE_ART_EXPECTED_SOURCE
      "docs/engine/NATIVE_SURFACE_ART_SOURCES.md")
    set(STELLAR_SURFACE_ART_EXPECTED_RUNTIME "Licenses/Surface-art-sources.md")
  endif()
  string(JSON STELLAR_SURFACE_ART_SOURCE GET "${STELLAR_SURFACE_ART_ASSETS}" assets
    ${STELLAR_SURFACE_ART_KEY} source)
  string(JSON STELLAR_SURFACE_ART_RUNTIME GET "${STELLAR_SURFACE_ART_ASSETS}" assets
    ${STELLAR_SURFACE_ART_KEY} runtimePath)
  string(JSON STELLAR_SURFACE_ART_EXPECTED_HASH GET "${STELLAR_SURFACE_ART_ASSETS}" assets
    ${STELLAR_SURFACE_ART_KEY} sha256)
  if(NOT STELLAR_SURFACE_ART_SOURCE STREQUAL STELLAR_SURFACE_ART_EXPECTED_SOURCE OR
     NOT STELLAR_SURFACE_ART_RUNTIME STREQUAL STELLAR_SURFACE_ART_EXPECTED_RUNTIME)
    message(FATAL_ERROR "Unreviewed native surface art ${STELLAR_SURFACE_ART_KEY} path")
  endif()
  file(SHA256 "${CMAKE_SOURCE_DIR}/${STELLAR_SURFACE_ART_SOURCE}" STELLAR_SURFACE_ART_ACTUAL_HASH)
  if(NOT STELLAR_SURFACE_ART_ACTUAL_HASH STREQUAL STELLAR_SURFACE_ART_EXPECTED_HASH)
    message(FATAL_ERROR "Native surface art ${STELLAR_SURFACE_ART_KEY} differs from reviewed content")
  endif()
  get_filename_component(STELLAR_SURFACE_ART_DESTINATION
    "${CMAKE_BINARY_DIR}/${STELLAR_SURFACE_ART_RUNTIME}" DIRECTORY)
  add_custom_command(TARGET stellar_native_surface_art_assets POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${STELLAR_SURFACE_ART_DESTINATION}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/${STELLAR_SURFACE_ART_SOURCE}"
      "${CMAKE_BINARY_DIR}/${STELLAR_SURFACE_ART_RUNTIME}")
endforeach()

if(BUILD_TESTING)
  add_executable(stellar_native_surface_art_tests
    native-tests/native_surface_art_tests.cpp
    app/native_client/native_surface_art_assets.cpp
    app/native_client/native_surface_workspace.cpp)
  target_include_directories(stellar_native_surface_art_tests PRIVATE app/native_client)
  target_link_libraries(stellar_native_surface_art_tests PRIVATE stellar_native_image stellar_core)
  add_test(NAME native_surface_art COMMAND stellar_native_surface_art_tests "${CMAKE_SOURCE_DIR}")
  set_tests_properties(native_surface_art PROPERTIES TIMEOUT 30)
  if(MSVC)
    target_compile_options(stellar_native_surface_art_tests PRIVATE /WX)
  endif()
endif()
