cmake_minimum_required(VERSION 3.24)
project(e2e-tile LANGUAGES CXX)
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
add_executable(e2e-tile src/main.cpp)
target_link_libraries(e2e-tile PRIVATE stellar::runtime)
# SDL3.dll and the default font ship in the SDK's bin directory.
add_custom_command(TARGET e2e-tile POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${STELLAR_ENGINE_SDK}/bin" "$<TARGET_FILE_DIR:e2e-tile>")
