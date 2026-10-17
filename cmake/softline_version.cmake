# One version contract for configuration, packaging and extracted source.
find_program(SOFTLINE_VERSION_BASH bash REQUIRED)
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "SL_VERSION_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
    "${SOFTLINE_VERSION_BASH}" "${CMAKE_CURRENT_LIST_DIR}/../scripts/release_version.sh"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  RESULT_VARIABLE _softline_version_result
  OUTPUT_VARIABLE PROJECT_VERSION
  ERROR_VARIABLE _softline_version_error
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _softline_version_result EQUAL 0)
  message(FATAL_ERROR "Cannot determine Softline version: ${_softline_version_error}")
endif()
if(NOT PROJECT_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
  message(FATAL_ERROR "Invalid Softline version: ${PROJECT_VERSION}")
endif()
set(PROJECT_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(PROJECT_VERSION_MINOR "${CMAKE_MATCH_2}")
set(PROJECT_VERSION_PATCH "${CMAKE_MATCH_3}")

configure_file(
  "${CMAKE_CURRENT_LIST_DIR}/softline_version.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/softline_version.h"
  @ONLY
)
