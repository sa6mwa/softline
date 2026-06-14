add_executable(test_softline test_softline.c)
target_link_libraries(test_softline PRIVATE softline)
add_test(NAME softline_unit COMMAND test_softline)
set_tests_properties(softline_unit PROPERTIES LABELS "unit")