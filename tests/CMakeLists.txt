add_executable(test_softline test_softline.c)
target_link_libraries(test_softline PRIVATE softline)
softline_apply_warnings(test_softline)
softline_local_runtime(test_softline)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(test_softline PRIVATE util)
endif()
add_test(NAME softline_unit COMMAND test_softline)
set_tests_properties(softline_unit PROPERTIES LABELS "unit" TIMEOUT 30)

# RLIMIT_AS cannot bound ASan's reserved virtual address space.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   NOT CMAKE_C_FLAGS MATCHES "sanitize=address")
  add_executable(test_quoted_prompt_memory test_quoted_prompt_memory.c)
  target_link_libraries(test_quoted_prompt_memory PRIVATE softline)
  softline_apply_warnings(test_quoted_prompt_memory)
  softline_local_runtime(test_quoted_prompt_memory)
  add_test(NAME quoted_prompt_bounded_memory COMMAND test_quoted_prompt_memory)
  set_tests_properties(quoted_prompt_bounded_memory PROPERTIES LABELS "unit" TIMEOUT 30)
endif()

if(TARGET softline_shared AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME softline_exports COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check_exports.py"
    --nm "${CMAKE_NM}"
    --library "$<TARGET_FILE:softline_shared>"
    --allowlist "${PROJECT_SOURCE_DIR}/cmake/softline.exports")
  set_tests_properties(softline_exports PROPERTIES LABELS "unit;abi" TIMEOUT 10)
endif()

softline_test_libmdf_supported(_softline_test_libmdf_supported)
if(_softline_test_libmdf_supported)
  find_package(Threads REQUIRED)
  add_executable(test_libmdf_stream test_libmdf_stream.c)
  target_link_libraries(test_libmdf_stream PRIVATE softline Threads::Threads)
  softline_enable_dev_libmdf(test_libmdf_stream)
  softline_apply_warnings(test_libmdf_stream)
  softline_local_runtime(test_libmdf_stream)
  add_test(NAME softline_libmdf_stream COMMAND test_libmdf_stream)
  set_tests_properties(softline_libmdf_stream PROPERTIES
    LABELS "integration;streaming;libmdf" TIMEOUT 10)
else()
  message(STATUS
    "Skipping libmdf streaming integration test: unsupported target '${SL_TARGET_ID}'")
endif()

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
  set_tests_properties(softline_examples PROPERTIES LABELS "integration" TIMEOUT 30)
endif()
