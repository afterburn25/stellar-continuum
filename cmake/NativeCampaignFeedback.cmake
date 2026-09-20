add_library(stellar_native_campaign_feedback STATIC
  app/native_client/native_campaign_feedback.cpp)
target_include_directories(stellar_native_campaign_feedback PUBLIC
  app/native_client engine/include)
target_link_libraries(stellar_native_campaign_feedback PUBLIC stellar_core)
if(MSVC)
  target_compile_options(stellar_native_campaign_feedback PRIVATE /WX)
endif()

if(BUILD_TESTING)
  add_executable(stellar_native_campaign_feedback_tests
    native-tests/native_campaign_feedback_tests.cpp)
  target_link_libraries(stellar_native_campaign_feedback_tests PRIVATE
    stellar_native_campaign_feedback)
  add_test(NAME native_campaign_feedback COMMAND stellar_native_campaign_feedback_tests)
  if(MSVC)
    target_compile_options(stellar_native_campaign_feedback_tests PRIVATE /WX)
  endif()
endif()
