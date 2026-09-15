add_executable(test_softline test_softline.c)
target_link_libraries(test_softline PRIVATE softline)
softline_apply_warnings(test_softline)
softline_local_runtime(test_softline)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(test_softline PRIVATE util)
endif()
add_test(NAME softline_unit COMMAND test_softline)
set_tests_properties(softline_unit PROPERTIES LABELS "unit" TIMEOUT 10)

include(${PROJECT_SOURCE_DIR}/cmake/softline_test_libmdf.cmake)
find_package(Threads REQUIRED)
add_executable(test_libmdf_stream test_libmdf_stream.c)
target_link_libraries(test_libmdf_stream PRIVATE softline Threads::Threads)
softline_enable_test_libmdf(test_libmdf_stream)
softline_apply_warnings(test_libmdf_stream)
softline_local_runtime(test_libmdf_stream)
add_test(NAME softline_libmdf_stream COMMAND test_libmdf_stream)
set_tests_properties(softline_libmdf_stream PROPERTIES
  LABELS "integration;streaming;libmdf" TIMEOUT 10)

if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$" AND
   SL_TARGET_ID STREQUAL "x86_64-linux-gnu")
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_executable(runtime_probe runtime_probe.c)
  softline_apply_warnings(runtime_probe)
  softline_local_runtime(runtime_probe)
  set(runtime_examples)
  if(TARGET example_simple AND TARGET example_chat)
    list(APPEND runtime_examples $<TARGET_FILE:example_simple> $<TARGET_FILE:example_chat>)
  endif()
  add_test(NAME softline_runtime COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check_runtime.py" "${CMAKE_SYSROOT}"
    "${CMAKE_READELF}"
    $<TARGET_FILE:runtime_probe> $<TARGET_FILE:test_softline>
    $<TARGET_FILE:test_libmdf_stream> ${runtime_examples})
  set_tests_properties(softline_runtime PROPERTIES
    ENVIRONMENT "SOFTLINE_TEST_LIBMDF_RUNTIME_ROOT=${SOFTLINE_TEST_LIBMDF_RUNTIME_ROOT}"
    TIMEOUT 10 LABELS "runtime")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET example_simple AND TARGET example_chat)
  add_executable(test_examples test_examples.c)
  target_link_libraries(test_examples PRIVATE util)
  softline_apply_warnings(test_examples)
  softline_local_runtime(test_examples)
  add_test(NAME softline_examples COMMAND test_examples
    $<TARGET_FILE:example_simple>
    $<TARGET_FILE:example_chat>
  )
  set_tests_properties(softline_examples PROPERTIES LABELS "integration" TIMEOUT 10)
endif()
