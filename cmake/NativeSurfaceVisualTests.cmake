if(BUILD_TESTING)
  add_executable(stellar_surface_visual_tests
    native-tests/native_surface_visual_tests.cpp
    app/native_client/native_surface_building_geometry.cpp
    app/native_client/native_surface_building_layer.cpp
    app/native_client/native_surface_workspace.cpp
    app/native_client/native_surface_scene.cpp)
  target_include_directories(stellar_surface_visual_tests PRIVATE
    app/native_client engine/include)
  target_link_libraries(stellar_surface_visual_tests PRIVATE
    stellar_native_platform stellar_core)
  if(MSVC)
    target_compile_options(stellar_surface_visual_tests PRIVATE /W4 /WX)
  endif()
  set_target_properties(stellar_surface_visual_tests PROPERTIES
    EXCLUDE_FROM_ALL TRUE)
endif()
