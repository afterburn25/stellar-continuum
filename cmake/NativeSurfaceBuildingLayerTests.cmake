add_executable(stellar_native_surface_building_layer_tests
  app/native_client/native_surface_building_geometry.cpp
  app/native_client/native_surface_building_layer.cpp
  app/native_client/native_surface_scene.cpp
  native-tests/native_surface_building_layer_tests.cpp)
target_include_directories(stellar_native_surface_building_layer_tests PRIVATE
  app/native_client engine/include)
target_link_libraries(stellar_native_surface_building_layer_tests PRIVATE
  stellar_native_image stellar_core)
target_compile_features(stellar_native_surface_building_layer_tests PRIVATE
  cxx_std_23)
target_compile_options(stellar_native_surface_building_layer_tests PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX;/permissive->
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic;-Werror>)
add_test(NAME native_surface_building_layer
  COMMAND stellar_native_surface_building_layer_tests)
set_tests_properties(native_surface_building_layer PROPERTIES TIMEOUT 30)
