# Only the native graphical client packages these approved generated star discs.
file(READ "${CMAKE_SOURCE_DIR}/export/native-star-art-assets.json" STELLAR_STAR_ART_ASSETS)
string(JSON STELLAR_STAR_ART_SCHEMA GET "${STELLAR_STAR_ART_ASSETS}" schemaVersion)
string(JSON STELLAR_STAR_ART_COUNT LENGTH "${STELLAR_STAR_ART_ASSETS}" assets)
if(NOT STELLAR_STAR_ART_SCHEMA EQUAL 1 OR NOT STELLAR_STAR_ART_COUNT EQUAL 12)
  message(FATAL_ERROR "Unsupported native star art asset declaration")
endif()
add_custom_target(stellar_native_star_art_assets)
foreach(STELLAR_STAR_ART_KEY IN ITEMS star-a-white star-f-dwarf star-g-dwarf
    star-giant star-hot-blue star-k-dwarf star-m-dwarf star-neutron
    star-protostar star-pulsar star-white-dwarf credits)
  if(STELLAR_STAR_ART_KEY STREQUAL "credits")
    set(STELLAR_STAR_ART_EXPECTED_SOURCE "docs/engine/NATIVE_STAR_ART_SOURCES.md")
    set(STELLAR_STAR_ART_EXPECTED_RUNTIME "Licenses/Star-art-sources.md")
  else()
    set(STELLAR_STAR_ART_EXPECTED_SOURCE "assets/visual/stars/${STELLAR_STAR_ART_KEY}.png")
    set(STELLAR_STAR_ART_EXPECTED_RUNTIME "${STELLAR_STAR_ART_EXPECTED_SOURCE}")
  endif()
  string(JSON STELLAR_STAR_ART_SOURCE GET "${STELLAR_STAR_ART_ASSETS}" assets ${STELLAR_STAR_ART_KEY} source)
  string(JSON STELLAR_STAR_ART_RUNTIME GET "${STELLAR_STAR_ART_ASSETS}" assets ${STELLAR_STAR_ART_KEY} runtimePath)
  string(JSON STELLAR_STAR_ART_EXPECTED_HASH GET "${STELLAR_STAR_ART_ASSETS}" assets ${STELLAR_STAR_ART_KEY} sha256)
  if(NOT STELLAR_STAR_ART_SOURCE STREQUAL STELLAR_STAR_ART_EXPECTED_SOURCE OR
     NOT STELLAR_STAR_ART_RUNTIME STREQUAL STELLAR_STAR_ART_EXPECTED_RUNTIME)
    message(FATAL_ERROR "Unreviewed native star art ${STELLAR_STAR_ART_KEY} path")
  endif()
  file(SHA256 "${CMAKE_SOURCE_DIR}/${STELLAR_STAR_ART_SOURCE}" STELLAR_STAR_ART_ACTUAL_HASH)
  if(NOT STELLAR_STAR_ART_ACTUAL_HASH STREQUAL STELLAR_STAR_ART_EXPECTED_HASH)
    message(FATAL_ERROR "Native star art ${STELLAR_STAR_ART_KEY} differs from reviewed content")
  endif()
  get_filename_component(STELLAR_STAR_ART_DESTINATION "${CMAKE_BINARY_DIR}/${STELLAR_STAR_ART_RUNTIME}" DIRECTORY)
  add_custom_command(TARGET stellar_native_star_art_assets POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${STELLAR_STAR_ART_DESTINATION}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/${STELLAR_STAR_ART_SOURCE}" "${CMAKE_BINARY_DIR}/${STELLAR_STAR_ART_RUNTIME}")
endforeach()
