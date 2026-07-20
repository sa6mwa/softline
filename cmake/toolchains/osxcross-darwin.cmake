set(_softline_toolchain_resolver
  "${CMAKE_CURRENT_LIST_DIR}/../../scripts/cpkt-toolchains.sh")
execute_process(
  COMMAND "${_softline_toolchain_resolver}" discover arm64-apple-darwin
  RESULT_VARIABLE _softline_osxcross_result
  OUTPUT_VARIABLE _softline_osxcross_description
  ERROR_VARIABLE _softline_osxcross_error)
if(NOT _softline_osxcross_result EQUAL 0)
  message(FATAL_ERROR
    "Unable to inspect the Darwin toolchain: ${_softline_osxcross_error}")
endif()

function(_softline_osxcross_value key output)
  string(REGEX MATCH "(^|\n)${key}=([^\r\n]+)" _softline_match
    "${_softline_osxcross_description}")
  if(NOT _softline_match)
    message(FATAL_ERROR "Darwin resolver did not report ${key}")
  endif()
  set(${output} "${CMAKE_MATCH_2}" PARENT_SCOPE)
endfunction()

_softline_osxcross_value(status _softline_osxcross_status)
if(NOT _softline_osxcross_status STREQUAL "ready")
  message(FATAL_ERROR
    "Darwin osxcross toolchain is unavailable:\n${_softline_osxcross_description}")
endif()

_softline_osxcross_value(root _softline_osxcross_root)
_softline_osxcross_value(prefix _softline_osxcross_host)
_softline_osxcross_value(sdk _softline_osxcross_sysroot)
_softline_osxcross_value(cc _softline_osxcross_cc)
_softline_osxcross_value(cxx _softline_osxcross_cxx)
_softline_osxcross_value(ld _softline_osxcross_ld)
_softline_osxcross_value(ar _softline_osxcross_ar)
_softline_osxcross_value(ranlib _softline_osxcross_ranlib)
_softline_osxcross_value(strip _softline_osxcross_strip)
_softline_osxcross_value(otool _softline_osxcross_otool)
_softline_osxcross_value(install_name_tool _softline_osxcross_install_name_tool)

get_filename_component(_softline_osxcross_bin
  "${_softline_osxcross_cc}" DIRECTORY)

set(ENV{PATH} "${_softline_osxcross_bin}:$ENV{PATH}")

set(CMAKE_SYSTEM_NAME Darwin)
string(REGEX REPLACE "-.*" "" CMAKE_SYSTEM_PROCESSOR
  "${_softline_osxcross_host}")

set(CMAKE_C_COMPILER
  "${_softline_osxcross_cc}"
  CACHE FILEPATH "Darwin C compiler" FORCE)
set(CMAKE_LINKER
  "${_softline_osxcross_ld}"
  CACHE FILEPATH "Darwin linker" FORCE)
set(CMAKE_AR
  "${_softline_osxcross_ar}"
  CACHE FILEPATH "Darwin archiver" FORCE)
set(CMAKE_RANLIB
  "${_softline_osxcross_ranlib}"
  CACHE FILEPATH "Darwin ranlib" FORCE)
set(CMAKE_STRIP
  "${_softline_osxcross_strip}"
  CACHE FILEPATH "Darwin strip" FORCE)
set(CMAKE_INSTALL_NAME_TOOL
  "${_softline_osxcross_install_name_tool}"
  CACHE FILEPATH "Darwin install_name_tool" FORCE)
set(CPKT_OTOOL
  "${_softline_osxcross_otool}"
  CACHE FILEPATH "Darwin otool" FORCE)

set(CMAKE_OSX_SYSROOT "${_softline_osxcross_sysroot}" CACHE PATH "Darwin SDK" FORCE)
set(CMAKE_FIND_ROOT_PATH
  "${_softline_osxcross_sysroot};${_softline_osxcross_root}/macports/pkgs/opt/local"
  CACHE STRING "Darwin target search roots" FORCE)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER CACHE STRING "" FORCE)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY CACHE STRING "" FORCE)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY CACHE STRING "" FORCE)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY CACHE STRING "" FORCE)

set(_softline_ld_path "--ld-path=${CMAKE_LINKER}")
foreach(_softline_linker_flags
    CMAKE_EXE_LINKER_FLAGS
    CMAKE_SHARED_LINKER_FLAGS
    CMAKE_MODULE_LINKER_FLAGS
    CMAKE_EXE_LINKER_FLAGS_INIT
    CMAKE_SHARED_LINKER_FLAGS_INIT
    CMAKE_MODULE_LINKER_FLAGS_INIT)
  string(REGEX REPLACE "(^| )--ld-path=[^ ]+" " "
    _softline_linker_flags_without_ld_path
    "${${_softline_linker_flags}}")
  string(STRIP "${_softline_linker_flags_without_ld_path}"
    _softline_linker_flags_without_ld_path)
  if(_softline_linker_flags_without_ld_path)
    set(_softline_linker_flags_value
      "${_softline_ld_path} ${_softline_linker_flags_without_ld_path}")
  else()
    set(_softline_linker_flags_value "${_softline_ld_path}")
  endif()
  set(${_softline_linker_flags} "${_softline_linker_flags_value}"
    CACHE STRING "Darwin linker flags" FORCE)
endforeach()

set(ENV{PKG_CONFIG_LIBDIR}
  "${_softline_osxcross_root}/macports/pkgs/opt/local/lib/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR}
  "${_softline_osxcross_root}/macports/pkgs")
