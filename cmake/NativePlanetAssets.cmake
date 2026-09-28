# Prepared, reviewed albedo/response maps only; original user renders are not runtime inputs.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/export/native-moon-assets.json")
file(READ "${CMAKE_SOURCE_DIR}/export/native-moon-assets.json" STELLAR_MOON_ASSETS)
string(JSON STELLAR_MOON_COUNT LENGTH "${STELLAR_MOON_ASSETS}" files)
math(EXPR STELLAR_MOON_LAST "${STELLAR_MOON_COUNT}-1")
add_custom_target(stellar_native_moon_assets)
foreach(INDEX RANGE 0 ${STELLAR_MOON_LAST})
  string(JSON ASSET_PATH GET "${STELLAR_MOON_ASSETS}" files ${INDEX} path)
  string(JSON EXPECTED GET "${STELLAR_MOON_ASSETS}" files ${INDEX} sha256)
  file(SHA256 "${CMAKE_SOURCE_DIR}/${ASSET_PATH}" ACTUAL)
  if(NOT ACTUAL STREQUAL EXPECTED)
    message(FATAL_ERROR "Reviewed moon material digest mismatch: ${ASSET_PATH}")
  endif()
  get_filename_component(ASSET_DIRECTORY "${ASSET_PATH}" DIRECTORY)
  add_custom_command(TARGET stellar_native_moon_assets
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/${ASSET_DIRECTORY}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/${ASSET_PATH}" "${CMAKE_BINARY_DIR}/${ASSET_PATH}")
endforeach()
add_custom_target(stellar_native_planet_assets
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
    "${CMAKE_SOURCE_DIR}/assets/visual/planets" "${CMAKE_BINARY_DIR}/assets/visual/planets"
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
    "${CMAKE_SOURCE_DIR}/assets/visual/rings" "${CMAKE_BINARY_DIR}/assets/visual/rings"
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
    "${CMAKE_SOURCE_DIR}/data/planets" "${CMAKE_BINARY_DIR}/data/planets")
add_dependencies(stellar_native_planet_assets stellar_native_moon_assets)
