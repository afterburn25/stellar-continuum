# Only the native graphical client packages these approved generated planet discs.
file(READ "${CMAKE_SOURCE_DIR}/export/native-planet-art-assets.json" STELLAR_PLANET_ART_ASSETS)
string(JSON STELLAR_PLANET_ART_SCHEMA GET "${STELLAR_PLANET_ART_ASSETS}" schemaVersion)
string(JSON STELLAR_PLANET_ART_COUNT LENGTH "${STELLAR_PLANET_ART_ASSETS}" assets)
if(NOT STELLAR_PLANET_ART_SCHEMA EQUAL 1 OR NOT STELLAR_PLANET_ART_COUNT EQUAL 23)
  message(FATAL_ERROR "Unsupported native planet art asset declaration")
endif()
add_custom_target(stellar_native_planet_art_assets)
foreach(STELLAR_PLANET_ART_KEY IN ITEMS arid-world barren-world continental-world
    cracked-world desert-world frozen-world gaia-world inferno-world ocean-world
    tomb-world tropical-world volcano-world wormhole-anomaly
    mercury venus earth mars jupiter saturn uranus neptune moon credits)
  if(STELLAR_PLANET_ART_KEY STREQUAL "credits")
    set(STELLAR_PLANET_ART_EXPECTED_SOURCE "docs/engine/NATIVE_PLANET_ART_SOURCES.md")
    set(STELLAR_PLANET_ART_EXPECTED_RUNTIME "Licenses/Planet-art-sources.md")
  else()
    set(STELLAR_PLANET_ART_EXPECTED_SOURCE "assets/visual/planets/${STELLAR_PLANET_ART_KEY}.png")
    set(STELLAR_PLANET_ART_EXPECTED_RUNTIME "${STELLAR_PLANET_ART_EXPECTED_SOURCE}")
  endif()
  string(JSON STELLAR_PLANET_ART_SOURCE GET "${STELLAR_PLANET_ART_ASSETS}" assets ${STELLAR_PLANET_ART_KEY} source)
  string(JSON STELLAR_PLANET_ART_RUNTIME GET "${STELLAR_PLANET_ART_ASSETS}" assets ${STELLAR_PLANET_ART_KEY} runtimePath)
  string(JSON STELLAR_PLANET_ART_EXPECTED_HASH GET "${STELLAR_PLANET_ART_ASSETS}" assets ${STELLAR_PLANET_ART_KEY} sha256)
  if(NOT STELLAR_PLANET_ART_SOURCE STREQUAL STELLAR_PLANET_ART_EXPECTED_SOURCE OR
     NOT STELLAR_PLANET_ART_RUNTIME STREQUAL STELLAR_PLANET_ART_EXPECTED_RUNTIME)
    message(FATAL_ERROR "Unreviewed native planet art ${STELLAR_PLANET_ART_KEY} path")
  endif()
  file(SHA256 "${CMAKE_SOURCE_DIR}/${STELLAR_PLANET_ART_SOURCE}" STELLAR_PLANET_ART_ACTUAL_HASH)
  if(NOT STELLAR_PLANET_ART_ACTUAL_HASH STREQUAL STELLAR_PLANET_ART_EXPECTED_HASH)
    message(FATAL_ERROR "Native planet art ${STELLAR_PLANET_ART_KEY} differs from reviewed content")
  endif()
  get_filename_component(STELLAR_PLANET_ART_DESTINATION "${CMAKE_BINARY_DIR}/${STELLAR_PLANET_ART_RUNTIME}" DIRECTORY)
  add_custom_command(TARGET stellar_native_planet_art_assets POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${STELLAR_PLANET_ART_DESTINATION}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/${STELLAR_PLANET_ART_SOURCE}" "${CMAKE_BINARY_DIR}/${STELLAR_PLANET_ART_RUNTIME}")
endforeach()
