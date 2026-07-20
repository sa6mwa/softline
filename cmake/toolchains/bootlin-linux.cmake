# Resolve the complete, pinned Bootlin collection before project().  Explicit
# Linux targets fail closed; omitted targets resolve to the native supported
# Bootlin target when possible.
set(_softline_resolver "${CMAKE_CURRENT_LIST_DIR}/../../scripts/cpkt-toolchains.sh")

if(NOT DEFINED SL_TARGET_ID OR SL_TARGET_ID STREQUAL "")
  execute_process(
    COMMAND "${_softline_resolver}" native-linux-target
    RESULT_VARIABLE _softline_native_target_result
    OUTPUT_VARIABLE _softline_native_target
    ERROR_VARIABLE _softline_native_target_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _softline_native_target_result EQUAL 0)
    string(STRIP "${_softline_native_target_error}" _softline_native_target_error)
    message(STATUS
      "No supported native Bootlin Linux target selected; using host toolchain: "
      "${_softline_native_target_error}")
    return()
  endif()
  set(SL_TARGET_ID "${_softline_native_target}" CACHE STRING
      "Lifecycle-selected native Linux target identifier" FORCE)
endif()

execute_process(
  COMMAND "${_softline_resolver}" ensure "${SL_TARGET_ID}"
  RESULT_VARIABLE _softline_ensure_result
  OUTPUT_VARIABLE _softline_ensure_output
  ERROR_VARIABLE _softline_ensure_error)
if(NOT _softline_ensure_result EQUAL 0)
  message(FATAL_ERROR
    "Unable to provision the pinned Bootlin toolchain for ${SL_TARGET_ID}:\n"
    "${_softline_ensure_error}")
endif()

execute_process(
  COMMAND "${_softline_resolver}" discover "${SL_TARGET_ID}"
  RESULT_VARIABLE _softline_discover_result
  OUTPUT_VARIABLE _softline_description
  ERROR_VARIABLE _softline_discover_error)
if(NOT _softline_discover_result EQUAL 0)
  message(FATAL_ERROR
    "Unable to inspect the pinned Bootlin toolchain for ${SL_TARGET_ID}:\n"
    "${_softline_discover_error}")
endif()

function(_softline_toolchain_value key output)
  string(REPLACE "\n" ";" _softline_toolchain_lines "${_softline_description}")
  set(_softline_value "")
  foreach(_softline_line IN LISTS _softline_toolchain_lines)
    if(_softline_line MATCHES "^${key}=(.*)$")
      set(_softline_value "${CMAKE_MATCH_1}")
      break()
    endif()
  endforeach()
  if(_softline_value STREQUAL "")
    message(FATAL_ERROR
      "Bootlin resolver did not report ${key} for ${SL_TARGET_ID}")
  endif()
  set(${output} "${_softline_value}" PARENT_SCOPE)
endfunction()

foreach(_softline_key cc cxx ld ar ranlib strip nm objcopy objdump addr2line
        readelf sysroot root)
  _softline_toolchain_value("${_softline_key}" "_softline_${_softline_key}")
endforeach()

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES SL_TARGET_ID CACHE STRING
    "Variables propagated to lifecycle compiler checks" FORCE)
if(SL_TARGET_ID MATCHES "^x86_64-")
  set(CMAKE_SYSTEM_PROCESSOR x86_64)
elseif(SL_TARGET_ID MATCHES "^aarch64-")
  set(CMAKE_SYSTEM_PROCESSOR aarch64)
elseif(SL_TARGET_ID MATCHES "^armhf-")
  set(CMAKE_SYSTEM_PROCESSOR arm)
else()
  message(FATAL_ERROR "Unsupported Linux target ID: ${SL_TARGET_ID}")
endif()

set(CMAKE_C_COMPILER "${_softline_cc}" CACHE FILEPATH "Pinned Bootlin C compiler" FORCE)
set(CMAKE_CXX_COMPILER "${_softline_cxx}" CACHE FILEPATH "Pinned Bootlin C++ compiler" FORCE)
set(CMAKE_LINKER "${_softline_ld}" CACHE FILEPATH "Pinned Bootlin linker" FORCE)
set(CMAKE_AR "${_softline_ar}" CACHE FILEPATH "Pinned Bootlin archiver" FORCE)
set(CMAKE_RANLIB "${_softline_ranlib}" CACHE FILEPATH "Pinned Bootlin ranlib" FORCE)
set(CMAKE_STRIP "${_softline_strip}" CACHE FILEPATH "Pinned Bootlin strip" FORCE)
set(CMAKE_NM "${_softline_nm}" CACHE FILEPATH "Pinned Bootlin nm" FORCE)
set(CMAKE_OBJCOPY "${_softline_objcopy}" CACHE FILEPATH "Pinned Bootlin objcopy" FORCE)
set(CMAKE_OBJDUMP "${_softline_objdump}" CACHE FILEPATH "Pinned Bootlin objdump" FORCE)
set(CMAKE_ADDR2LINE "${_softline_addr2line}" CACHE FILEPATH "Pinned Bootlin addr2line" FORCE)
set(CMAKE_READELF "${_softline_readelf}" CACHE FILEPATH "Pinned Bootlin readelf" FORCE)
set(CMAKE_SYSROOT "${_softline_sysroot}" CACHE PATH "Pinned Bootlin sysroot" FORCE)
set(CMAKE_FIND_ROOT_PATH "${_softline_sysroot};${_softline_root}" CACHE STRING
    "Pinned Bootlin search roots" FORCE)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER CACHE STRING "" FORCE)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY CACHE STRING "" FORCE)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY CACHE STRING "" FORCE)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY CACHE STRING "" FORCE)
if(NOT SL_TARGET_ID STREQUAL "x86_64-linux-gnu")
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
endif()
