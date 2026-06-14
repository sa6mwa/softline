function(softline_detect_version)
  set(PROJECT_VERSION_OVERRIDE "$ENV{SL_VERSION_OVERRIDE}")
  if(PROJECT_VERSION_OVERRIDE)
    set(PROJECT_VERSION "${PROJECT_VERSION_OVERRIDE}" PARENT_SCOPE)
    set(PROJECT_VERSION_MAJOR "" PARENT_SCOPE)
    set(PROJECT_VERSION_MINOR "" PARENT_SCOPE)
    set(PROJECT_VERSION_PATCH "" PARENT_SCOPE)
    return()
  endif()

  if(EXISTS "${CMAKE_SOURCE_DIR}/VERSION")
    file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION" _ver LIMIT_COUNT 1)
    string(STRIP "${_ver}" _ver)
    if(_ver MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)")
      set(PROJECT_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}" PARENT_SCOPE)
      set(PROJECT_VERSION_MAJOR "${CMAKE_MATCH_1}" PARENT_SCOPE)
      set(PROJECT_VERSION_MINOR "${CMAKE_MATCH_2}" PARENT_SCOPE)
      set(PROJECT_VERSION_PATCH "${CMAKE_MATCH_3}" PARENT_SCOPE)
      return()
    endif()
  endif()

  find_package(Git QUIET)
  if(GIT_FOUND)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} describe --tags --exact-match HEAD
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      OUTPUT_VARIABLE _git_tag
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE _git_result
    )
    if(_git_result EQUAL 0 AND _git_tag MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)")
      set(PROJECT_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}" PARENT_SCOPE)
      set(PROJECT_VERSION_MAJOR "${CMAKE_MATCH_1}" PARENT_SCOPE)
      set(PROJECT_VERSION_MINOR "${CMAKE_MATCH_2}" PARENT_SCOPE)
      set(PROJECT_VERSION_PATCH "${CMAKE_MATCH_3}" PARENT_SCOPE)
      return()
    endif()
  endif()

  set(PROJECT_VERSION "0.0.0" PARENT_SCOPE)
  set(PROJECT_VERSION_MAJOR "0" PARENT_SCOPE)
  set(PROJECT_VERSION_MINOR "0" PARENT_SCOPE)
  set(PROJECT_VERSION_PATCH "0" PARENT_SCOPE)
endfunction()

softline_detect_version()

configure_file(
  "${CMAKE_CURRENT_LIST_DIR}/softline_version.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/softline_version.h"
  @ONLY
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/softline_version.h"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/softline"
)