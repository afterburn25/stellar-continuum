# Stellar Engine SDK — consumer entry point.
#
# A game project sets STELLAR_ENGINE_SDK to the exported engine-sdk directory
# (cmake -DSTELLAR_ENGINE_SDK=... or the STELLAR_ENGINE_SDK environment
# variable) and includes this file. It defines imported interface targets:
#
#   stellar::engine    foundation, jobs, packages, project, save, replay, ...
#   stellar::cooker    asset cooking + cooked-package runtime registry
#   stellar::platform  windowed SDL3 host (Window, input, renderer)
#   stellar::audio     AudioOutput + Media Foundation clip decode
#
# Windows-only SDK; libraries are prebuilt and pinned to the engine build that
# produced this export.

get_filename_component(STELLAR_ENGINE_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/.."
                       ABSOLUTE)
if(NOT EXISTS "${STELLAR_ENGINE_SDK_ROOT}/lib/stellar_engine.lib")
  message(FATAL_ERROR
          "StellarEngineSdk.cmake: '${STELLAR_ENGINE_SDK_ROOT}' is not a "
          "complete engine SDK (lib/stellar_engine.lib missing)")
endif()

add_library(stellar::engine INTERFACE IMPORTED)
set_target_properties(stellar::engine PROPERTIES
  INTERFACE_COMPILE_FEATURES cxx_std_23
  INTERFACE_INCLUDE_DIRECTORIES "${STELLAR_ENGINE_SDK_ROOT}/include"
  INTERFACE_LINK_LIBRARIES
    "${STELLAR_ENGINE_SDK_ROOT}/lib/stellar_engine.lib;Ole32;Shell32;Dbghelp;Cabinet")

add_library(stellar::cooker INTERFACE IMPORTED)
set_target_properties(stellar::cooker PROPERTIES
  INTERFACE_LINK_LIBRARIES
    "${STELLAR_ENGINE_SDK_ROOT}/lib/stellar_asset_cooker.lib;${STELLAR_ENGINE_SDK_ROOT}/lib/stellar_native_image.lib;${STELLAR_ENGINE_SDK_ROOT}/lib/stellar_texture_codecs.lib;Windowscodecs;stellar::engine")

add_library(stellar::platform INTERFACE IMPORTED)
set_target_properties(stellar::platform PROPERTIES
  INTERFACE_LINK_LIBRARIES
    "${STELLAR_ENGINE_SDK_ROOT}/lib/stellar_native_platform.lib;${STELLAR_ENGINE_SDK_ROOT}/lib/stellar_native_image.lib;${STELLAR_ENGINE_SDK_ROOT}/lib/stellar_texture_codecs.lib;${STELLAR_ENGINE_SDK_ROOT}/lib/SDL3.lib;Gdi32;User32;Windowscodecs;stellar::engine")

add_library(stellar::audio INTERFACE IMPORTED)
set_target_properties(stellar::audio PROPERTIES
  INTERFACE_LINK_LIBRARIES
    "${STELLAR_ENGINE_SDK_ROOT}/lib/stellar_native_audio.lib;${STELLAR_ENGINE_SDK_ROOT}/lib/SDL3.lib;Mfplat;Mfreadwrite;Mfuuid;stellar::engine")
