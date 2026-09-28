add_executable(test_softline test_softline.c)
target_link_libraries(test_softline PRIVATE softline)
softline_apply_warnings(test_softline)
softline_local_runtime(test_softline)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(prompt_handoff_fixture prompt_handoff_fixture.c)
  target_link_libraries(prompt_handoff_fixture PRIVATE softline)
  softline_apply_warnings(prompt_handoff_fixture)
  softline_local_runtime(prompt_handoff_fixture)
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
  add_test(NAME softline_export_reconfigure COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check_export_reconfigure.py"
    "${PROJECT_SOURCE_DIR}" "${CMAKE_COMMAND}" "${CMAKE_NM}"
    "${SL_TARGET_ID}" "${CMAKE_TOOLCHAIN_FILE}" "${CMAKE_C_COMPILER}"
    "${CMAKE_LINKER}")
  set_tests_properties(softline_export_reconfigure PROPERTIES
    LABELS "unit;abi" TIMEOUT 90)
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

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(native_output_fixture native_output_fixture.c)
  target_link_libraries(native_output_fixture PRIVATE softline)
  softline_apply_warnings(native_output_fixture)
  softline_local_runtime(native_output_fixture)
  add_executable(test_native_resize test_native_resize.c
    "${PROJECT_SOURCE_DIR}/src/softline_surface.c")
  target_include_directories(test_native_resize PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(test_native_resize PRIVATE util)
  softline_apply_warnings(test_native_resize)
  softline_local_runtime(test_native_resize)
  add_test(NAME softline_native_resize COMMAND test_native_resize)
  set_tests_properties(softline_native_resize PROPERTIES
    LABELS "integration;streaming" TIMEOUT 10)
  add_executable(test_examples test_examples.c)
  target_link_libraries(test_examples PRIVATE util softline)
  softline_apply_warnings(test_examples)
  softline_local_runtime(test_examples)
  add_test(NAME softline_prompt_frames COMMAND test_examples frames)
  set_tests_properties(softline_prompt_frames PROPERTIES
    LABELS "integration;streaming" TIMEOUT 10)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET example_simple AND TARGET example_chat)
  add_test(NAME softline_examples COMMAND test_examples
    $<TARGET_FILE:example_simple>
    $<TARGET_FILE:example_chat>
  )
  set_tests_properties(softline_examples PROPERTIES LABELS "integration" TIMEOUT 60)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET example_chat)
  # A second real terminal engine catches erased blank-cell extent that tmux's
  # grid drops. Keep these host-only test dependencies out of the library.
  find_program(SOFTLINE_TEST_XVFB_RUN xvfb-run)
  find_library(SOFTLINE_TEST_VTE NAMES vte-2.91 libvte-2.91.so.0
    NO_CMAKE_FIND_ROOT_PATH)
  if(SOFTLINE_TEST_XVFB_RUN AND SOFTLINE_TEST_VTE)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME softline_terminal_chat_handoff_resize
      COMMAND "${SOFTLINE_TEST_XVFB_RUN}" -a "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check_chat_handoff_resize.py"
      "$<TARGET_FILE:example_chat>" "${PROJECT_BINARY_DIR}")
    set_tests_properties(softline_terminal_chat_handoff_resize PROPERTIES
      LABELS "integration;streaming" TIMEOUT 90)
    add_test(NAME softline_terminal_prompt_handoff
      COMMAND "${SOFTLINE_TEST_XVFB_RUN}" -a "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check_terminal_prompt_handoff.py"
      "$<TARGET_FILE:prompt_handoff_fixture>" "${PROJECT_BINARY_DIR}")
    set_tests_properties(softline_terminal_prompt_handoff PROPERTIES
      LABELS "integration;streaming" TIMEOUT 120)
    add_test(NAME softline_terminal_native_output
      COMMAND "${SOFTLINE_TEST_XVFB_RUN}" -a "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check_terminal_native_output.py"
      "$<TARGET_FILE:native_output_fixture>" "${PROJECT_BINARY_DIR}")
    set_tests_properties(softline_terminal_native_output PROPERTIES
      LABELS "integration;streaming" TIMEOUT 120)
    if(TARGET example_simple)
      add_test(NAME softline_terminal_readline_resize
        COMMAND "${SOFTLINE_TEST_XVFB_RUN}" -a "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/check_terminal_readline_resize.py"
        "$<TARGET_FILE:example_simple>")
      set_tests_properties(softline_terminal_readline_resize PROPERTIES
        LABELS "integration" TIMEOUT 120)
    endif()
    if(_softline_test_libmdf_supported)
      add_executable(unicode_output_fixture unicode_output_fixture.c)
      target_link_libraries(unicode_output_fixture PRIVATE softline)
      softline_enable_dev_libmdf(unicode_output_fixture)
      softline_apply_warnings(unicode_output_fixture)
      softline_local_runtime(unicode_output_fixture)
      add_test(NAME softline_terminal_unicode_output
        COMMAND "${SOFTLINE_TEST_XVFB_RUN}" -a "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/check_terminal_unicode_output.py"
        "$<TARGET_FILE:unicode_output_fixture>" "${PROJECT_BINARY_DIR}")
      set_tests_properties(softline_terminal_unicode_output PROPERTIES
        LABELS "integration;streaming;libmdf" TIMEOUT 120)
    endif()
    add_test(NAME softline_terminal_blank_reflow
      COMMAND "${SOFTLINE_TEST_XVFB_RUN}" -a "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check_terminal_blank_reflow.py"
      "$<TARGET_FILE:example_chat>" "${PROJECT_BINARY_DIR}")
    set_tests_properties(softline_terminal_blank_reflow PROPERTIES
      LABELS "integration;streaming" TIMEOUT 60)
    add_test(NAME softline_terminal_streaming_resize
      COMMAND "${SOFTLINE_TEST_XVFB_RUN}" -a "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check_terminal_blank_reflow.py"
      "$<TARGET_FILE:example_chat>" "${PROJECT_BINARY_DIR}" --streaming)
    set_tests_properties(softline_terminal_streaming_resize PROPERTIES
      LABELS "integration;streaming" TIMEOUT 60)
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET example_chat)
  find_program(SOFTLINE_TEST_TMUX tmux)
  if(SOFTLINE_TEST_TMUX)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME softline_terminal_native_output_tmux
      COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check_terminal_native_output_tmux.py"
      "${SOFTLINE_TEST_TMUX}" "$<TARGET_FILE:native_output_fixture>"
      "${PROJECT_BINARY_DIR}")
    set_tests_properties(softline_terminal_native_output_tmux PROPERTIES
      LABELS "integration;streaming" TIMEOUT 30)
    add_executable(terminal_resize_fixture terminal_resize_fixture.c)
    target_link_libraries(terminal_resize_fixture PRIVATE softline)
    softline_enable_dev_libmdf(terminal_resize_fixture)
    softline_apply_warnings(terminal_resize_fixture)
    softline_local_runtime(terminal_resize_fixture)
    add_test(NAME softline_terminal_producer_resize COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check_terminal_resize.py"
      "${SOFTLINE_TEST_TMUX}" "$<TARGET_FILE:example_chat>" "${PROJECT_BINARY_DIR}"
      "$<TARGET_FILE:terminal_resize_fixture>" --producer-only)
    set_tests_properties(softline_terminal_producer_resize PROPERTIES
      LABELS "integration;streaming" TIMEOUT 45)
    add_test(NAME softline_terminal_resize COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check_terminal_resize.py"
      "${SOFTLINE_TEST_TMUX}" "$<TARGET_FILE:example_chat>" "${PROJECT_BINARY_DIR}"
      "$<TARGET_FILE:terminal_resize_fixture>")
    add_test(NAME softline_terminal_growth COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check_terminal_resize.py"
      "${SOFTLINE_TEST_TMUX}" "$<TARGET_FILE:example_chat>" "${PROJECT_BINARY_DIR}"
      "$<TARGET_FILE:terminal_resize_fixture>" --grow-both)
    set_tests_properties(softline_terminal_growth PROPERTIES
      LABELS "integration;streaming" TIMEOUT 30)
    add_test(NAME softline_terminal_quoted_resize COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check_terminal_resize.py"
      "${SOFTLINE_TEST_TMUX}" "$<TARGET_FILE:example_chat>" "${PROJECT_BINARY_DIR}"
      "$<TARGET_FILE:terminal_resize_fixture>" --quoted)
    set_tests_properties(softline_terminal_quoted_resize PROPERTIES
      LABELS "integration;streaming" TIMEOUT 30)
    add_test(NAME softline_terminal_static_resize COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check_terminal_resize.py"
      "${SOFTLINE_TEST_TMUX}" "$<TARGET_FILE:example_chat>" "${PROJECT_BINARY_DIR}"
      "$<TARGET_FILE:terminal_resize_fixture>" --static)
    add_test(NAME softline_terminal_height_roundtrip COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check_terminal_resize.py"
      "${SOFTLINE_TEST_TMUX}" "$<TARGET_FILE:example_chat>" "${PROJECT_BINARY_DIR}"
      "$<TARGET_FILE:terminal_resize_fixture>" --height-roundtrip)
    set_tests_properties(softline_terminal_height_roundtrip PROPERTIES
      LABELS "integration;streaming" TIMEOUT 30)
    set_tests_properties(softline_terminal_static_resize PROPERTIES
      LABELS "integration;streaming" TIMEOUT 30)
    set_tests_properties(softline_terminal_resize PROPERTIES
      LABELS "integration;streaming" TIMEOUT 90)
    add_test(NAME softline_terminal_queued_resize
      COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check_terminal_resize.py"
      "${SOFTLINE_TEST_TMUX}" "$<TARGET_FILE:example_chat>"
      "${PROJECT_BINARY_DIR}" "$<TARGET_FILE:terminal_resize_fixture>"
      --queued-resize)
    set_tests_properties(softline_terminal_queued_resize PROPERTIES
      LABELS "integration;streaming" TIMEOUT 30)
  endif()
endif()
