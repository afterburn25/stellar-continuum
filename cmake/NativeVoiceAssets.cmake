# Only the native graphical client packages these reviewed voice catalogues.
file(READ "${CMAKE_SOURCE_DIR}/export/native-voice-assets.json" STELLAR_VOICE_ASSETS)
string(JSON STELLAR_VOICE_SCHEMA GET "${STELLAR_VOICE_ASSETS}" schemaVersion)
string(JSON STELLAR_VOICE_COUNT LENGTH "${STELLAR_VOICE_ASSETS}" assets)
if(NOT STELLAR_VOICE_SCHEMA EQUAL 1 OR NOT STELLAR_VOICE_COUNT EQUAL 3)
  message(FATAL_ERROR "Unsupported native voice asset declaration")
endif()
add_custom_target(stellar_native_voice_assets)
foreach(STELLAR_VOICE_KEY IN ITEMS events human roles)
  set(STELLAR_VOICE_EXPECTED "data/voice_profiles/${STELLAR_VOICE_KEY}.json")
  set(STELLAR_VOICE_EXPECTED_RUNTIME "Data/voice_profiles/${STELLAR_VOICE_KEY}.json")
  string(JSON STELLAR_VOICE_SOURCE GET "${STELLAR_VOICE_ASSETS}" assets ${STELLAR_VOICE_KEY} source)
  string(JSON STELLAR_VOICE_RUNTIME GET "${STELLAR_VOICE_ASSETS}" assets ${STELLAR_VOICE_KEY} runtimePath)
  string(JSON STELLAR_VOICE_EXPECTED_HASH GET "${STELLAR_VOICE_ASSETS}" assets ${STELLAR_VOICE_KEY} sha256)
  if(NOT STELLAR_VOICE_SOURCE STREQUAL STELLAR_VOICE_EXPECTED OR
     NOT STELLAR_VOICE_RUNTIME STREQUAL STELLAR_VOICE_EXPECTED_RUNTIME)
    message(FATAL_ERROR "Unreviewed native voice ${STELLAR_VOICE_KEY} path")
  endif()
  file(SHA256 "${CMAKE_SOURCE_DIR}/${STELLAR_VOICE_SOURCE}" STELLAR_VOICE_ACTUAL_HASH)
  if(NOT STELLAR_VOICE_ACTUAL_HASH STREQUAL STELLAR_VOICE_EXPECTED_HASH)
    message(FATAL_ERROR "Native voice ${STELLAR_VOICE_KEY} differs from reviewed content")
  endif()
  get_filename_component(STELLAR_VOICE_DESTINATION "${CMAKE_BINARY_DIR}/${STELLAR_VOICE_EXPECTED_RUNTIME}" DIRECTORY)
  add_custom_command(TARGET stellar_native_voice_assets POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${STELLAR_VOICE_DESTINATION}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/${STELLAR_VOICE_SOURCE}" "${CMAKE_BINARY_DIR}/${STELLAR_VOICE_EXPECTED_RUNTIME}")
endforeach()
