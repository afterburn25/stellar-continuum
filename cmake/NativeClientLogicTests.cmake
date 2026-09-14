# Pure client logic remains testable on headless CI without SDL, a font or a GPU.
add_executable(stellar_native_system_workspace_tests
  native-tests/native_system_workspace_tests.cpp
  app/native_client/native_system_workspace.cpp app/native_client/native_system_view.cpp)
target_include_directories(stellar_native_system_workspace_tests PRIVATE app/native_client engine/include)
target_link_libraries(stellar_native_system_workspace_tests PRIVATE stellar_core)
add_test(NAME native_system_workspace COMMAND stellar_native_system_workspace_tests
  "${CMAKE_SOURCE_DIR}/data/research/v1"
  "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json")
set_tests_properties(native_system_workspace PROPERTIES TIMEOUT 90)
if(MSVC)
  target_compile_options(stellar_native_system_workspace_tests PRIVATE /WX)
endif()

add_executable(stellar_native_system_view_tests
  native-tests/native_system_view_tests.cpp app/native_client/native_system_view.cpp)
target_include_directories(stellar_native_system_view_tests PRIVATE app/native_client)
target_link_libraries(stellar_native_system_view_tests PRIVATE stellar_core)
add_test(NAME native_system_view COMMAND stellar_native_system_view_tests
  "${CMAKE_SOURCE_DIR}/data/research/v1"
  "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json")
set_tests_properties(native_system_view PROPERTIES TIMEOUT 90)
if(MSVC)
  target_compile_options(stellar_native_system_view_tests PRIVATE /WX)
endif()

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

add_executable(stellar_native_fleet_controller_tests
  native-tests/native_fleet_controller_tests.cpp app/native_client/native_fleet_controller.cpp)
target_include_directories(stellar_native_fleet_controller_tests PRIVATE app/native_client)
target_link_libraries(stellar_native_fleet_controller_tests PRIVATE stellar_core stellar_json)
add_test(NAME native_fleet_controller COMMAND stellar_native_fleet_controller_tests
  "${CMAKE_SOURCE_DIR}/data/research/v1"
  "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json"
  "${CMAKE_SOURCE_DIR}/native-tests/fixtures/player-campaign-json.json"
  "${CMAKE_BINARY_DIR}/native-fleet-cases")
set_tests_properties(native_fleet_controller PROPERTIES TIMEOUT 90)
add_executable(stellar_native_fleet_workspace_tests
  native-tests/native_fleet_workspace_tests.cpp app/native_client/native_fleet_workspace.cpp)
target_include_directories(stellar_native_fleet_workspace_tests PRIVATE app/native_client engine/include)
target_link_libraries(stellar_native_fleet_workspace_tests PRIVATE stellar_core)
add_test(NAME native_fleet_workspace COMMAND stellar_native_fleet_workspace_tests)
add_executable(stellar_native_fleet_presentation_tests
  native-tests/native_fleet_presentation_tests.cpp app/native_client/native_fleet_presentation.cpp)
target_include_directories(stellar_native_fleet_presentation_tests PRIVATE app/native_client engine/include)
target_link_libraries(stellar_native_fleet_presentation_tests PRIVATE stellar_core)
add_test(NAME native_fleet_presentation COMMAND stellar_native_fleet_presentation_tests)
if(MSVC)
  target_compile_options(stellar_native_fleet_controller_tests PRIVATE /WX)
  target_compile_options(stellar_native_fleet_workspace_tests PRIVATE /WX)
  target_compile_options(stellar_native_fleet_presentation_tests PRIVATE /WX)
  target_compile_options(stellar_exploration_order_tests PRIVATE /WX)
endif()

add_executable(stellar_native_shipyard_controller_tests
  native-tests/native_shipyard_controller_tests.cpp app/native_client/native_shipyard_controller.cpp)
target_include_directories(stellar_native_shipyard_controller_tests PRIVATE app/native_client)
target_link_libraries(stellar_native_shipyard_controller_tests PRIVATE stellar_core stellar_json)
add_test(NAME native_shipyard_controller COMMAND stellar_native_shipyard_controller_tests
  "${CMAKE_SOURCE_DIR}/data/research/v1"
  "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json"
  "${CMAKE_SOURCE_DIR}/native-tests/fixtures/player-campaign-json.json"
  "${CMAKE_BINARY_DIR}/native-shipyard-cases")
set_tests_properties(native_shipyard_controller PROPERTIES TIMEOUT 90)
if(MSVC)
  target_compile_options(stellar_native_shipyard_controller_tests PRIVATE /WX)
endif()

add_executable(stellar_native_shipyard_workspace_tests
  native-tests/native_shipyard_workspace_tests.cpp app/native_client/native_shipyard_workspace.cpp)
target_include_directories(stellar_native_shipyard_workspace_tests PRIVATE app/native_client engine/include)
target_link_libraries(stellar_native_shipyard_workspace_tests PRIVATE stellar_core)
add_test(NAME native_shipyard_workspace COMMAND stellar_native_shipyard_workspace_tests)
if(MSVC)
  target_compile_options(stellar_native_shipyard_workspace_tests PRIVATE /WX)
endif()

add_executable(stellar_native_construction_controller_tests
  native-tests/native_construction_controller_tests.cpp app/native_client/native_construction_controller.cpp)
target_include_directories(stellar_native_construction_controller_tests PRIVATE app/native_client)
target_link_libraries(stellar_native_construction_controller_tests PRIVATE stellar_core stellar_json)
add_test(NAME native_construction_controller COMMAND stellar_native_construction_controller_tests
  "${CMAKE_SOURCE_DIR}/data/research/v1"
  "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json"
  "${CMAKE_SOURCE_DIR}/native-tests/fixtures/player-campaign-json.json"
  "${CMAKE_BINARY_DIR}/native-construction-cases")
set_tests_properties(native_construction_controller PROPERTIES TIMEOUT 90)
add_executable(stellar_native_construction_workspace_tests
  native-tests/native_construction_workspace_tests.cpp app/native_client/native_construction_workspace.cpp)
target_include_directories(stellar_native_construction_workspace_tests PRIVATE app/native_client engine/include)
target_link_libraries(stellar_native_construction_workspace_tests PRIVATE stellar_core)
add_test(NAME native_construction_workspace COMMAND stellar_native_construction_workspace_tests)
if(MSVC)
  target_compile_options(stellar_native_construction_controller_tests PRIVATE /WX)
  target_compile_options(stellar_native_construction_workspace_tests PRIVATE /WX)
endif()

add_executable(stellar_native_fresh_progression_tests
  native-tests/native_fresh_progression_tests.cpp
  app/native_client/native_research_controller.cpp
  app/native_client/native_construction_controller.cpp
  app/native_client/native_shipyard_controller.cpp
  app/native_client/native_fleet_controller.cpp)
target_include_directories(stellar_native_fresh_progression_tests PRIVATE app/native_client)
target_link_libraries(stellar_native_fresh_progression_tests PRIVATE stellar_core stellar_json)
add_test(NAME native_fresh_progression COMMAND stellar_native_fresh_progression_tests
  "${CMAKE_SOURCE_DIR}/data/research/v1"
  "${CMAKE_SOURCE_DIR}/data/astronomy/hyg-nearby-500-v1.json")
# Two ordinary campaigns replay twice through thousands of bounded strategic
# days. The checked Debug build takes about three minutes locally.
set_tests_properties(native_fresh_progression PROPERTIES TIMEOUT 600)
if(MSVC)
  target_compile_options(stellar_native_fresh_progression_tests PRIVATE /WX)
endif()
