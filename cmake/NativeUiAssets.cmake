# Only included by the opt-in Windows native client. Headless exports need no font.
file(READ "${CMAKE_SOURCE_DIR}/export/native-ui-assets.json" STELLAR_NATIVE_UI_ASSETS)
string(JSON STELLAR_UI_ASSET_SCHEMA GET "${STELLAR_NATIVE_UI_ASSETS}" schemaVersion)
if(NOT STELLAR_UI_ASSET_SCHEMA EQUAL 1)
  message(FATAL_ERROR "Unsupported native UI asset declaration")
endif()
add_custom_target(stellar_native_ui_assets)
foreach(STELLAR_UI_KIND IN ITEMS font license)
  string(JSON STELLAR_UI_SOURCE GET "${STELLAR_NATIVE_UI_ASSETS}" ${STELLAR_UI_KIND} source)
  string(JSON STELLAR_UI_RUNTIME GET "${STELLAR_NATIVE_UI_ASSETS}" ${STELLAR_UI_KIND} runtimePath)
  string(JSON STELLAR_UI_EXPECTED_HASH GET "${STELLAR_NATIVE_UI_ASSETS}" ${STELLAR_UI_KIND} sha256)
  if(STELLAR_UI_KIND STREQUAL "font")
    if(NOT STELLAR_UI_SOURCE STREQUAL "assets/visual/fonts/Rajdhani-SemiBold.ttf" OR
       NOT STELLAR_UI_RUNTIME STREQUAL "assets/visual/fonts/Rajdhani-SemiBold.ttf")
      message(FATAL_ERROR "Unreviewed native UI font path")
    endif()
  else()
    if(NOT STELLAR_UI_SOURCE STREQUAL "assets/visual/fonts/OFL-Rajdhani.txt" OR
       NOT STELLAR_UI_RUNTIME STREQUAL "Licenses/OFL-Rajdhani.txt")
      message(FATAL_ERROR "Unreviewed native UI font license path")
    endif()
  endif()
  file(SHA256 "${CMAKE_SOURCE_DIR}/${STELLAR_UI_SOURCE}" STELLAR_UI_ACTUAL_HASH)
  if(NOT STELLAR_UI_ACTUAL_HASH STREQUAL STELLAR_UI_EXPECTED_HASH)
    message(FATAL_ERROR "Native UI ${STELLAR_UI_KIND} differs from reviewed content")
  endif()
  get_filename_component(STELLAR_UI_DESTINATION "${CMAKE_BINARY_DIR}/${STELLAR_UI_RUNTIME}" DIRECTORY)
  add_custom_command(TARGET stellar_native_ui_assets POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${STELLAR_UI_DESTINATION}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/${STELLAR_UI_SOURCE}" "${CMAKE_BINARY_DIR}/${STELLAR_UI_RUNTIME}")
endforeach()
