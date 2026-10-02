add_executable(test_softline test_softline.c)
target_link_libraries(test_softline PRIVATE softline)
softline_apply_warnings(test_softline)
softline_local_runtime(test_softline)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  if(TARGET example_chat AND NOT CMAKE_C_FLAGS MATCHES "sanitize")
    add_library(chat_no_sleep_guard SHARED chat_no_sleep_guard.c)
    softline_apply_warnings(chat_no_sleep_guard)
    add_test(NAME softline_chat_no_sleep COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check_chat_no_sleep.py"
      "$<TARGET_FILE:example_chat>" "$<TARGET_FILE:chat_no_sleep_guard>")
    set_tests_properties(softline_chat_no_sleep PROPERTIES
      LABELS "integration;streaming;example-smoke" TIMEOUT 12)
  endif()
  add_executable(feed_latency_fixture feed_latency_fixture.c)
  target_link_libraries(feed_latency_fixture PRIVATE softline)
  softline_apply_warnings(feed_latency_fixture)
  softline_local_runtime(feed_latency_fixture)
  add_test(NAME softline_feed_latency COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check_feed_latency.py" "$<TARGET_FILE:feed_latency_fixture>")
  set_tests_properties(softline_feed_latency PROPERTIES
    LABELS "integration;streaming" TIMEOUT 15)
  add_executable(late_scroll_region_fixture late_scroll_region_fixture.c)
  target_link_libraries(late_scroll_region_fixture PRIVATE softline)
  softline_apply_warnings(late_scroll_region_fixture)
  softline_local_runtime(late_scroll_region_fixture)
  add_test(NAME softline_late_scroll_region COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check_late_scroll_region.py" "$<TARGET_FILE:late_scroll_region_fixture>")
  set_tests_properties(softline_late_scroll_region PROPERTIES
    LABELS "integration;streaming" TIMEOUT 10)
  # Exercise the private wire protocol without requiring an X server or GTK.
  add_executable(clipboard_protocol_fixture clipboard_protocol_fixture.c
    "${PROJECT_SOURCE_DIR}/src/softline_resolver.c"
    $<TARGET_OBJECTS:sl_cares_obj>)
  target_include_directories(clipboard_protocol_fixture SYSTEM PRIVATE
    "${PROJECT_SOURCE_DIR}/vendor/c-ares/include" "${PROJECT_BINARY_DIR}/private-cares")
  softline_apply_warnings(clipboard_protocol_fixture)
  softline_local_runtime(clipboard_protocol_fixture)
  add_test(NAME softline_clipboard_protocol COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check_clipboard_protocol.py"
    "$<TARGET_FILE:clipboard_protocol_fixture>" "${PROJECT_BINARY_DIR}")
  set_tests_properties(softline_clipboard_protocol PROPERTIES
    LABELS "integration;clipboard" TIMEOUT 30)
  add_executable(resolver_fixture resolver_fixture.c resolver_collision.c
    "${PROJECT_SOURCE_DIR}/src/softline_resolver.c"
    $<TARGET_OBJECTS:sl_cares_obj>)
  target_include_directories(resolver_fixture PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_include_directories(resolver_fixture SYSTEM PRIVATE
    "${PROJECT_SOURCE_DIR}/vendor/c-ares/include" "${PROJECT_BINARY_DIR}/private-cares")
  softline_apply_warnings(resolver_fixture)
  softline_local_runtime(resolver_fixture)
  add_test(NAME softline_resolver COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check_resolver.py"
    "$<TARGET_FILE:resolver_fixture>" "${PROJECT_BINARY_DIR}")
  set_tests_properties(softline_resolver PROPERTIES LABELS "integration;clipboard" TIMEOUT 15)
  if(TARGET softline_static AND NOT CMAKE_C_FLAGS MATCHES "sanitize")
    add_executable(resolver_fixture_static resolver_fixture.c resolver_collision.c)
    target_include_directories(resolver_fixture_static PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_include_directories(resolver_fixture_static SYSTEM PRIVATE
      "${PROJECT_SOURCE_DIR}/vendor/c-ares/include" "${PROJECT_BINARY_DIR}/private-cares")
    target_link_libraries(resolver_fixture_static PRIVATE softline_static)
    target_link_options(resolver_fixture_static PRIVATE -static -Wl,--fatal-warnings)
    softline_apply_warnings(resolver_fixture_static)
    add_test(NAME softline_resolver_static COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check_resolver.py"
      "$<TARGET_FILE:resolver_fixture_static>" "${PROJECT_BINARY_DIR}")
    set_tests_properties(softline_resolver_static PROPERTIES
      LABELS "integration;clipboard;static" TIMEOUT 15)
  endif()
  if(TARGET softline_static)
    add_test(NAME softline_resolver_symbols COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check_resolver_symbols.py"
      "${CMAKE_NM}" "$<TARGET_FILE:softline_static>")
    set_tests_properties(softline_resolver_symbols PROPERTIES LABELS "unit;abi" TIMEOUT 10)
  endif()
  add_test(NAME softline_resolver_provenance COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check_resolver_provenance.py" "${PROJECT_SOURCE_DIR}")
  set_tests_properties(softline_resolver_provenance PROPERTIES LABELS "unit;dependency" TIMEOUT 10)
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
  add_executable(clipboard_fixture clipboard_fixture.c)
  target_link_libraries(clipboard_fixture PRIVATE softline)
  softline_apply_warnings(clipboard_fixture)
  softline_local_runtime(clipboard_fixture)
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
    set(terminal_python "${Python3_EXECUTABLE}")
    if(DEFINED ENV{SOFTLINE_TERMINAL_TEST_ROOT})
      # xvfb-run's host shell utilities keep their own runtime. The terminal
      # test interpreter loads staged GTK/VTE; fixture children keep their runtime.
      set(terminal_python "${PROJECT_SOURCE_DIR}/scripts/terminal-test-runtime.sh"
        "${Python3_EXECUTABLE}")
      set(terminal_test_environment "SOFTLINE_TERMINAL_TEST_ROOT=$ENV{SOFTLINE_TERMINAL_TEST_ROOT}")
      foreach(variable PATH FONTCONFIG_PATH FONTCONFIG_FILE XKB_CONFIG_ROOT NO_AT_BRIDGE TMPDIR)
        list(APPEND terminal_test_environment "${variable}=$ENV{${variable}}")
      endforeach()
    endif()
    function(softline_add_terminal_test name)
      add_test(NAME "${name}"
        COMMAND "${SOFTLINE_TEST_XVFB_RUN}" -a ${terminal_python} ${ARGN})
      if(terminal_test_environment)
        set_tests_properties("${name}" PROPERTIES ENVIRONMENT "${terminal_test_environment}")
      endif()
    endfunction()
    softline_add_terminal_test(softline_clipboard_image_paste
      "${CMAKE_CURRENT_SOURCE_DIR}/check_clipboard_image_paste.py"
      "$<TARGET_FILE:clipboard_fixture>" "${PROJECT_BINARY_DIR}")
    set_tests_properties(softline_clipboard_image_paste PROPERTIES
      LABELS "integration;clipboard" TIMEOUT 60)
    softline_add_terminal_test(softline_terminal_native_output
      "${CMAKE_CURRENT_SOURCE_DIR}/check_terminal_native_output.py"
      "$<TARGET_FILE:native_output_fixture>" "${PROJECT_BINARY_DIR}")
    set_tests_properties(softline_terminal_native_output PROPERTIES
      LABELS "integration;streaming" TIMEOUT 120)
    softline_add_terminal_test(softline_terminal_tab_expansion
      "${CMAKE_CURRENT_SOURCE_DIR}/check_terminal_tab_expansion.py"
      "$<TARGET_FILE:native_output_fixture>" "${PROJECT_BINARY_DIR}")
    set_tests_properties(softline_terminal_tab_expansion PROPERTIES
      LABELS "integration;streaming" TIMEOUT 30)
    if(terminal_test_environment)
      add_test(NAME softline_terminal_test_environment COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/check_terminal_test_environment.py"
        "${PROJECT_BINARY_DIR}" "${CMAKE_CTEST_COMMAND}")
      set_tests_properties(softline_terminal_test_environment PROPERTIES
        LABELS "integration;dependency" TIMEOUT 45)
    endif()
    softline_add_terminal_test(softline_terminal_opaque_feed
      "${CMAKE_CURRENT_SOURCE_DIR}/check_terminal_opaque_feed.py"
      "$<TARGET_FILE:native_output_fixture>" "${PROJECT_BINARY_DIR}")
    set_tests_properties(softline_terminal_opaque_feed PROPERTIES
      LABELS "integration;streaming" TIMEOUT 30)
    if(TARGET example_simple)
      softline_add_terminal_test(softline_terminal_readline_resize
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
      softline_add_terminal_test(softline_terminal_unicode_output
        "${CMAKE_CURRENT_SOURCE_DIR}/check_terminal_unicode_output.py"
        "$<TARGET_FILE:unicode_output_fixture>" "${PROJECT_BINARY_DIR}")
      set_tests_properties(softline_terminal_unicode_output PROPERTIES
        LABELS "integration;streaming;libmdf" TIMEOUT 120)
    endif()
    softline_add_terminal_test(softline_terminal_blank_reflow
      "${CMAKE_CURRENT_SOURCE_DIR}/check_terminal_blank_reflow.py"
      "$<TARGET_FILE:example_chat>" "${PROJECT_BINARY_DIR}")
    set_tests_properties(softline_terminal_blank_reflow PROPERTIES
      LABELS "integration;streaming" TIMEOUT 60)
    foreach(_softline_chat_theme plain riced)
      softline_add_terminal_test(softline_chat_width_${_softline_chat_theme}
        "${CMAKE_CURRENT_SOURCE_DIR}/check_chat_width_resize.py"
        "$<TARGET_FILE:example_chat>" "${PROJECT_BINARY_DIR}"
        "${_softline_chat_theme}")
      set_tests_properties(softline_chat_width_${_softline_chat_theme} PROPERTIES
        LABELS "integration;streaming;libmdf" TIMEOUT 40)
    endforeach()
    softline_add_terminal_test(softline_terminal_streaming_resize
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
  endif()
endif()
