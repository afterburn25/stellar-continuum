add_library(stellar_native_audio STATIC engine/src/native_audio.cpp)
target_include_directories(stellar_native_audio PUBLIC engine/include)
target_link_libraries(stellar_native_audio PUBLIC SDL3::SDL3 PRIVATE stellar_engine mfplat mfreadwrite mfuuid ole32)

if(MSVC)
  target_compile_options(stellar_native_audio PRIVATE /WX)
endif()

if(BUILD_TESTING)
  add_executable(stellar_native_audio_tests native-tests/native_audio_tests.cpp)
  target_link_libraries(stellar_native_audio_tests PRIVATE stellar_native_audio)
  add_custom_command(TARGET stellar_native_audio_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${STELLAR_SDL_runtime}" "$<TARGET_FILE_DIR:stellar_native_audio_tests>/SDL3.dll")
  add_test(NAME native_audio COMMAND stellar_native_audio_tests
    "${CMAKE_SOURCE_DIR}/assets/audio/music/claimed-by-the-void-loop.mp3"
    "${CMAKE_SOURCE_DIR}/assets/audio/sfx/ui-hover.wav")
  set_tests_properties(native_audio PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE ENVIRONMENT "SDL_AUDIODRIVER=dummy")
  if(MSVC)
    target_compile_options(stellar_native_audio_tests PRIVATE /WX)
  endif()
endif()
