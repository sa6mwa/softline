add_executable(test_softline test_softline.c)
target_link_libraries(test_softline PRIVATE softline)
softline_apply_warnings(test_softline)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(test_softline PRIVATE util)
endif()
add_test(NAME softline_unit COMMAND test_softline)
set_tests_properties(softline_unit PROPERTIES LABELS "unit" TIMEOUT 10)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET example_simple AND TARGET example_chat)
  add_executable(test_examples test_examples.c)
  target_link_libraries(test_examples PRIVATE util)
  softline_apply_warnings(test_examples)
  add_test(NAME softline_examples COMMAND test_examples
    $<TARGET_FILE:example_simple>
    $<TARGET_FILE:example_chat>
  )
  set_tests_properties(softline_examples PROPERTIES LABELS "integration" TIMEOUT 10)
endif()
