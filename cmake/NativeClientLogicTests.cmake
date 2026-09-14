# Pure client logic remains testable on headless CI without SDL, a font or a GPU.
add_executable(stellar_native_research_controller_tests
  native-tests/native_research_controller_tests.cpp app/native_client/native_research_controller.cpp)
target_include_directories(stellar_native_research_controller_tests PRIVATE app/native_client)
target_link_libraries(stellar_native_research_controller_tests PRIVATE stellar_core stellar_json)
add_test(NAME native_research_controller COMMAND stellar_native_research_controller_tests
  "${CMAKE_SOURCE_DIR}/data/research/v1"
  "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json"
  "${CMAKE_SOURCE_DIR}/native-tests/fixtures/player-campaign-json.json"
  "${CMAKE_BINARY_DIR}/native-research-cases")
set_tests_properties(native_research_controller PROPERTIES TIMEOUT 90)

add_executable(stellar_native_research_workspace_tests
  native-tests/native_research_workspace_tests.cpp app/native_client/native_research_workspace.cpp)
target_include_directories(stellar_native_research_workspace_tests PRIVATE app/native_client)
target_link_libraries(stellar_native_research_workspace_tests PRIVATE stellar_core)
add_test(NAME native_research_workspace COMMAND stellar_native_research_workspace_tests)

add_executable(stellar_native_ui_layout_tests native-tests/native_ui_layout_tests.cpp)
target_include_directories(stellar_native_ui_layout_tests PRIVATE app/native_client engine/include)
add_test(NAME native_ui_layout COMMAND stellar_native_ui_layout_tests)

add_executable(stellar_native_client_input_tests native-tests/native_client_input_tests.cpp)
target_include_directories(stellar_native_client_input_tests PRIVATE app/native_client engine/include)
add_test(NAME native_client_input COMMAND stellar_native_client_input_tests)

if(MSVC)
  target_compile_options(stellar_native_research_controller_tests PRIVATE /WX)
  target_compile_options(stellar_native_research_workspace_tests PRIVATE /WX)
  target_compile_options(stellar_native_ui_layout_tests PRIVATE /WX)
  target_compile_options(stellar_native_client_input_tests PRIVATE /WX)
endif()
