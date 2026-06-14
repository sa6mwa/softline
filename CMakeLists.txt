cmake_minimum_required(VERSION 3.16)
project(softline VERSION 0.1.0 LANGUAGES C)

include(cmake/softline_version.cmake)

option(SL_BUILD_STATIC "Build softline as a static library" ON)
option(SL_BUILD_SHARED "Build softline as a shared library" ON)
option(SL_BUILD_EXAMPLES "Build softline examples" ON)
option(SL_BUILD_TESTS "Build softline tests" ON)
option(SL_INSTALL "Generate install rules" ON)
option(SL_ENABLE_COVERAGE "Enable code coverage" OFF)

set(SL_DIST_DIR "${CMAKE_SOURCE_DIR}/dist" CACHE PATH "Release artifact output directory")
set(SL_TARGET_ID "" CACHE STRING "Release target identifier")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(sl_vendor_obj OBJECT vendor/linenoise/linenoise.c)
set_target_properties(sl_vendor_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(sl_vendor_obj
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/linenoise
)
target_compile_definitions(sl_vendor_obj PRIVATE _POSIX_C_SOURCE=200809L _GNU_SOURCE)
target_compile_options(sl_vendor_obj
  PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-pedantic -Wno-variadic-macros
)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|AppleClang")
  target_compile_options(sl_vendor_obj PRIVATE -Wno-c99-extensions)
endif()

add_library(sl_lib_obj OBJECT src/softline.c)
set_target_properties(sl_lib_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(sl_lib_obj
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(sl_lib_obj PUBLIC sl_vendor_obj)
target_compile_definitions(sl_lib_obj PRIVATE _POSIX_C_SOURCE=200809L)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(sl_lib_obj
    PRIVATE -std=c89 -Wall -Wextra -Wpedantic -Wno-unused-parameter
  )
endif()

if(SL_BUILD_STATIC)
  add_library(softline_static STATIC)
  target_sources(softline_static PRIVATE $<TARGET_OBJECTS:sl_lib_obj> $<TARGET_OBJECTS:sl_vendor_obj>)
  target_include_directories(softline_static
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  set_target_properties(softline_static PROPERTIES OUTPUT_NAME softline)
  if(NOT SL_BUILD_SHARED)
    add_library(softline ALIAS softline_static)
  endif()
endif()

if(SL_BUILD_SHARED)
  add_library(softline_shared SHARED)
  target_sources(softline_shared PRIVATE $<TARGET_OBJECTS:sl_lib_obj> $<TARGET_OBJECTS:sl_vendor_obj>)
  target_include_directories(softline_shared
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  set_target_properties(softline_shared PROPERTIES
    OUTPUT_NAME softline
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
  )
  add_library(softline ALIAS softline_shared)
endif()

if(SL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(SL_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(SL_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

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

    install(FILES
      ${CMAKE_CURRENT_BINARY_DIR}/softlineConfig.cmake
      ${CMAKE_CURRENT_BINARY_DIR}/softlineConfigVersion.cmake
      DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/softline
    )

    install(FILES LICENSE
      DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/softline
    )
  endif()
endif()