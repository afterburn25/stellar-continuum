cmake_minimum_required(VERSION 3.24)
project(c-devin-e2e-hotrel LANGUAGES CXX)
# The exported SDK ships Release-built static-CRT libraries only.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
if(NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
if(NOT DEFINED STELLAR_ENGINE_SDK)
  if(DEFINED ENV{STELLAR_ENGINE_SDK})
    set(STELLAR_ENGINE_SDK "$ENV{STELLAR_ENGINE_SDK}")
  else()
    message(FATAL_ERROR "Set -DSTELLAR_ENGINE_SDK=<engine-sdk dir> or the STELLAR_ENGINE_SDK environment variable")
  endif()
endif()
include("${STELLAR_ENGINE_SDK}/cmake/StellarEngineSdk.cmake")
add_executable(c-devin-e2e-hotrel src/main.cpp)
target_link_libraries(c-devin-e2e-hotrel PRIVATE stellar::platform)
# SDL3.dll and the default font ship in the SDK's bin directory.
add_custom_command(TARGET c-devin-e2e-hotrel POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${STELLAR_ENGINE_SDK}/bin" "$<TARGET_FILE_DIR:c-devin-e2e-hotrel>")
