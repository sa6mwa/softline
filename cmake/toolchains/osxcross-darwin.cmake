set(_softline_osxcross_root "$ENV{OSXCROSS_ROOT}")
if(NOT _softline_osxcross_root)
  set(_softline_osxcross_root "$ENV{HOME}/.local/cross/osxcross")
endif()

set(_softline_osxcross_host "$ENV{CPKT_OSXCROSS_HOST}")
if(NOT _softline_osxcross_host)
  set(_softline_osxcross_host "arm64-apple-darwin25")
endif()

set(_softline_osxcross_bin "${_softline_osxcross_root}/bin")
set(_softline_osxcross_sdk "${_softline_osxcross_root}/SDK")
file(GLOB _softline_osxcross_sdks
  LIST_DIRECTORIES true
  "${_softline_osxcross_sdk}/MacOSX*.sdk")
list(SORT _softline_osxcross_sdks)
list(REVERSE _softline_osxcross_sdks)
if(_softline_osxcross_sdks)
  list(GET _softline_osxcross_sdks 0 _softline_osxcross_sysroot)
else()
  message(FATAL_ERROR
    "No MacOSX SDK found under ${_softline_osxcross_sdk}")
endif()

set(ENV{PATH} "${_softline_osxcross_bin}:$ENV{PATH}")

set(CMAKE_SYSTEM_NAME Darwin)
string(REGEX REPLACE "-.*" "" CMAKE_SYSTEM_PROCESSOR
  "${_softline_osxcross_host}")

set(CMAKE_C_COMPILER
  "${_softline_osxcross_bin}/${_softline_osxcross_host}-clang"
  CACHE FILEPATH "Darwin C compiler")
set(CMAKE_LINKER
  "${_softline_osxcross_bin}/${_softline_osxcross_host}-ld"
  CACHE FILEPATH "Darwin linker")
set(CMAKE_AR
  "${_softline_osxcross_bin}/${_softline_osxcross_host}-ar"
  CACHE FILEPATH "Darwin archiver")
set(CMAKE_RANLIB
  "${_softline_osxcross_bin}/${_softline_osxcross_host}-ranlib"
  CACHE FILEPATH "Darwin ranlib")
set(CMAKE_STRIP
  "${_softline_osxcross_bin}/${_softline_osxcross_host}-strip"
  CACHE FILEPATH "Darwin strip")
set(CMAKE_INSTALL_NAME_TOOL
  "${_softline_osxcross_bin}/${_softline_osxcross_host}-install_name_tool"
  CACHE FILEPATH "Darwin install_name_tool")
set(CPKT_OTOOL
  "${_softline_osxcross_bin}/${_softline_osxcross_host}-otool"
  CACHE FILEPATH "Darwin otool")

set(CMAKE_OSX_SYSROOT "${_softline_osxcross_sysroot}" CACHE PATH "Darwin SDK")
set(CMAKE_FIND_ROOT_PATH
  "${CMAKE_FIND_ROOT_PATH}"
  "${_softline_osxcross_sysroot}"
  "${_softline_osxcross_root}/macports/pkgs/opt/local")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(_softline_fuse_ld "-fuse-ld=${CMAKE_LINKER}")
foreach(_softline_linker_flags
    CMAKE_EXE_LINKER_FLAGS_INIT
    CMAKE_SHARED_LINKER_FLAGS_INIT
    CMAKE_MODULE_LINKER_FLAGS_INIT)
  if(NOT "${${_softline_linker_flags}}" MATCHES "(^| )-fuse-ld=")
    set(${_softline_linker_flags}
      "${_softline_fuse_ld} ${${_softline_linker_flags}}")
  endif()
endforeach()

set(ENV{PKG_CONFIG_LIBDIR}
  "${_softline_osxcross_root}/macports/pkgs/opt/local/lib/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR}
  "${_softline_osxcross_root}/macports/pkgs")
