function(softline_detect_version)
  set(PROJECT_VERSION_OVERRIDE "$ENV{SL_VERSION_OVERRIDE}")
  if(PROJECT_VERSION_OVERRIDE)
    if(NOT PROJECT_VERSION_OVERRIDE MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
      message(FATAL_ERROR "SL_VERSION_OVERRIDE must be X.Y.Z or vX.Y.Z")
    endif()
    set(PROJECT_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}" PARENT_SCOPE)
    set(PROJECT_VERSION_MAJOR "${CMAKE_MATCH_1}" PARENT_SCOPE)
    set(PROJECT_VERSION_MINOR "${CMAKE_MATCH_2}" PARENT_SCOPE)
    set(PROJECT_VERSION_PATCH "${CMAKE_MATCH_3}" PARENT_SCOPE)
    return()
  endif()

  find_package(Git QUIET)
  if(GIT_FOUND)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} rev-parse --show-toplevel
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      OUTPUT_VARIABLE _git_root
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE _git_worktree_result
    )
    if(_git_worktree_result EQUAL 0)
      file(REAL_PATH "${_git_root}" _git_root_real)
      file(REAL_PATH "${CMAKE_SOURCE_DIR}" _source_dir_real)
      if(_git_root_real STREQUAL _source_dir_real)
        set(_git_worktree "true")
      endif()
    endif()
  endif()

  if(GIT_FOUND AND _git_worktree_result EQUAL 0 AND _git_worktree STREQUAL "true")
    execute_process(
      COMMAND ${GIT_EXECUTABLE} tag --points-at HEAD
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      OUTPUT_VARIABLE _git_tags
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE _git_result
    )
    if(_git_result EQUAL 0)
      set(_found_version_tag "")
      string(REPLACE "\n" ";" _git_tag_list "${_git_tags}")
      foreach(_git_tag IN LISTS _git_tag_list)
        if(_git_tag MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
          execute_process(
            COMMAND ${GIT_EXECUTABLE} cat-file -t "refs/tags/${_git_tag}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE _git_tag_type
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
          )
          if(_git_tag_type STREQUAL "commit")
            if(_found_version_tag)
              message(FATAL_ERROR
                "Multiple lightweight version tags point at HEAD: ${_found_version_tag} ${_git_tag}")
            endif()
            set(_found_version_tag "${_git_tag}")
          endif()
        endif()
      endforeach()
      if(_found_version_tag MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
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
