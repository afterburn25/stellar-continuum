# Only the native graphical client packages these approved original images.
file(READ "${CMAKE_SOURCE_DIR}/export/native-ship-art-assets.json" STELLAR_SHIP_ART_ASSETS)
string(JSON STELLAR_SHIP_ART_SCHEMA GET "${STELLAR_SHIP_ART_ASSETS}" schemaVersion)
string(JSON STELLAR_SHIP_ART_COUNT LENGTH "${STELLAR_SHIP_ART_ASSETS}" assets)
if(NOT STELLAR_SHIP_ART_SCHEMA EQUAL 1 OR NOT STELLAR_SHIP_ART_COUNT EQUAL 7)
  message(FATAL_ERROR "Unsupported native ship art asset declaration")
endif()
add_custom_target(stellar_native_ship_art_assets)
foreach(STELLAR_SHIP_ART_KEY IN ITEMS pathfinder-scout deep-space-science-vessel
    patrol-corvette interstellar-colony-ship resource-outpost-ship
    interstellar-bulk-freighter credits)
  if(STELLAR_SHIP_ART_KEY STREQUAL "credits")
    set(STELLAR_SHIP_ART_EXPECTED_SOURCE "docs/engine/NATIVE_SHIP_ART_SOURCES.md")
    set(STELLAR_SHIP_ART_EXPECTED_RUNTIME "Licenses/Ship-art-sources.md")
  else()
    if(STELLAR_SHIP_ART_KEY STREQUAL "resource-outpost-ship" OR
       STELLAR_SHIP_ART_KEY STREQUAL "interstellar-bulk-freighter")
      set(STELLAR_SHIP_ART_EXTENSION "png")
    else()
      set(STELLAR_SHIP_ART_EXTENSION "jpg")
    endif()
    set(STELLAR_SHIP_ART_EXPECTED_SOURCE "assets/visual/ships/${STELLAR_SHIP_ART_KEY}.${STELLAR_SHIP_ART_EXTENSION}")
    set(STELLAR_SHIP_ART_EXPECTED_RUNTIME "${STELLAR_SHIP_ART_EXPECTED_SOURCE}")
  endif()
  string(JSON STELLAR_SHIP_ART_SOURCE GET "${STELLAR_SHIP_ART_ASSETS}" assets ${STELLAR_SHIP_ART_KEY} source)
  string(JSON STELLAR_SHIP_ART_RUNTIME GET "${STELLAR_SHIP_ART_ASSETS}" assets ${STELLAR_SHIP_ART_KEY} runtimePath)
  string(JSON STELLAR_SHIP_ART_EXPECTED_HASH GET "${STELLAR_SHIP_ART_ASSETS}" assets ${STELLAR_SHIP_ART_KEY} sha256)
  if(NOT STELLAR_SHIP_ART_SOURCE STREQUAL STELLAR_SHIP_ART_EXPECTED_SOURCE OR
     NOT STELLAR_SHIP_ART_RUNTIME STREQUAL STELLAR_SHIP_ART_EXPECTED_RUNTIME)
    message(FATAL_ERROR "Unreviewed native ship art ${STELLAR_SHIP_ART_KEY} path")
  endif()
  file(SHA256 "${CMAKE_SOURCE_DIR}/${STELLAR_SHIP_ART_SOURCE}" STELLAR_SHIP_ART_ACTUAL_HASH)
  if(NOT STELLAR_SHIP_ART_ACTUAL_HASH STREQUAL STELLAR_SHIP_ART_EXPECTED_HASH)
    message(FATAL_ERROR "Native ship art ${STELLAR_SHIP_ART_KEY} differs from reviewed content")
  endif()
  get_filename_component(STELLAR_SHIP_ART_DESTINATION "${CMAKE_BINARY_DIR}/${STELLAR_SHIP_ART_RUNTIME}" DIRECTORY)
  add_custom_command(TARGET stellar_native_ship_art_assets POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${STELLAR_SHIP_ART_DESTINATION}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/${STELLAR_SHIP_ART_SOURCE}" "${CMAKE_BINARY_DIR}/${STELLAR_SHIP_ART_RUNTIME}")
endforeach()
