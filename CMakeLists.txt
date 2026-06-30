cmake_minimum_required(VERSION 3.16)
project(softline VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)
include(cmake/softline_version.cmake)

option(SL_BUILD_STATIC "Build softline as a static library" ON)
option(SL_BUILD_SHARED "Build softline as a shared library" ON)
option(SL_BUILD_EXAMPLES "Build softline examples" ON)
option(SL_BUILD_TESTS "Build softline tests" ON)
option(SL_INSTALL "Generate install rules" ON)
option(SL_ENABLE_COVERAGE "Enable code coverage" OFF)
option(SL_WARNINGS_AS_ERRORS "Treat project warnings as errors" ON)

if(NOT SL_BUILD_STATIC AND NOT SL_BUILD_SHARED)
  message(FATAL_ERROR "At least one of SL_BUILD_STATIC or SL_BUILD_SHARED must be ON")
endif()

set(SL_DIST_DIR "${CMAKE_SOURCE_DIR}/dist" CACHE PATH "Release artifact output directory")
set(SL_TARGET_ID "" CACHE STRING "Release target identifier")
set(SOFTLINE_ABI_VERSION "0" CACHE STRING "softline shared library ABI version")

if(NOT SOFTLINE_ABI_VERSION MATCHES "^[0-9]+$")
  message(FATAL_ERROR "SOFTLINE_ABI_VERSION must be a non-negative integer")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

function(softline_apply_warnings target)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(${target}
      PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter
    )
    if(SL_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

add_library(sl_lib_obj OBJECT src/softline.c)
set_target_properties(sl_lib_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(sl_lib_obj
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_definitions(sl_lib_obj PRIVATE _POSIX_C_SOURCE=200809L)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(sl_lib_obj
    PRIVATE -std=c89
  )
endif()
softline_apply_warnings(sl_lib_obj)

if(SL_BUILD_STATIC)
  add_library(softline_static STATIC)
  target_sources(softline_static PRIVATE $<TARGET_OBJECTS:sl_lib_obj>)
  target_include_directories(softline_static
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  set_target_properties(softline_static PROPERTIES OUTPUT_NAME softline)
  if(SL_BUILD_SHARED)
    set_target_properties(softline_static PROPERTIES EXPORT_NAME softline_static)
  else()
    set_target_properties(softline_static PROPERTIES EXPORT_NAME softline)
  endif()
  if(NOT SL_BUILD_SHARED)
    add_library(softline ALIAS softline_static)
  endif()
endif()

if(SL_BUILD_SHARED)
  add_library(softline_shared SHARED)
  target_sources(softline_shared PRIVATE $<TARGET_OBJECTS:sl_lib_obj>)
  target_include_directories(softline_shared
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  set_target_properties(softline_shared PROPERTIES
    EXPORT_NAME softline
    OUTPUT_NAME softline
    VERSION ${PROJECT_VERSION}
    SOVERSION ${SOFTLINE_ABI_VERSION}
    MACOSX_RPATH ON
    INSTALL_NAME_DIR "@rpath"
  )
  add_library(softline ALIAS softline_shared)
endif()

if(SL_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(SL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(SL_INSTALL)
  include(CMakePackageConfigHelpers)

  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/softline_version.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/softline
  )

  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  if(SL_BUILD_STATIC)
    install(TARGETS softline_static
      EXPORT softlineTargets
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
  endif()

  if(SL_BUILD_SHARED)
    install(TARGETS softline_shared
      EXPORT softlineTargets
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
  endif()

  if(SL_BUILD_STATIC OR SL_BUILD_SHARED)
    install(EXPORT softlineTargets
      NAMESPACE softline::
      DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/softline
      FILE softlineTargets.cmake
    )

    configure_package_config_file(
      cmake/softlineConfig.cmake.in
      ${CMAKE_CURRENT_BINARY_DIR}/softlineConfig.cmake
      INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/softline
    )

    write_basic_package_version_file(
      ${CMAKE_CURRENT_BINARY_DIR}/softlineConfigVersion.cmake
      VERSION ${PROJECT_VERSION}
      COMPATIBILITY SameMajorVersion
    )

    if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
      set(SOFTLINE_PC_PREFIX "${CMAKE_INSTALL_PREFIX}")
      set(SOFTLINE_PC_LIBDIR "${CMAKE_INSTALL_FULL_LIBDIR}")
    else()
      file(RELATIVE_PATH SOFTLINE_PC_PREFIX_REL
        "/${CMAKE_INSTALL_LIBDIR}/pkgconfig"
        "/"
      )
      set(SOFTLINE_PC_PREFIX "\${pcfiledir}/${SOFTLINE_PC_PREFIX_REL}")
      set(SOFTLINE_PC_LIBDIR "\${prefix}/${CMAKE_INSTALL_LIBDIR}")
    endif()
    if(IS_ABSOLUTE "${CMAKE_INSTALL_INCLUDEDIR}")
      set(SOFTLINE_PC_INCLUDEDIR "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
    else()
      set(SOFTLINE_PC_INCLUDEDIR "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}")
    endif()

    configure_file(
      cmake/softline.pc.in
      ${CMAKE_CURRENT_BINARY_DIR}/softline.pc
      @ONLY
    )
    configure_file(
      cmake/softline-package-metadata.json.in
      ${CMAKE_CURRENT_BINARY_DIR}/softline-package-metadata.json
      @ONLY
    )

    install(FILES
      ${CMAKE_CURRENT_BINARY_DIR}/softlineConfig.cmake
      ${CMAKE_CURRENT_BINARY_DIR}/softlineConfigVersion.cmake
      DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/softline
    )

    install(FILES
      ${CMAKE_CURRENT_BINARY_DIR}/softline.pc
      DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
    )

    install(FILES LICENSE README.md
      DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/softline
    )

    install(FILES
      ${CMAKE_CURRENT_BINARY_DIR}/softline-package-metadata.json
      DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/softline
      RENAME package-metadata.json
    )
  endif()
endif()
