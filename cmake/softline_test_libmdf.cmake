include_guard(GLOBAL)

set(SOFTLINE_TEST_LIBMDF_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}")

# libmdf is an integration-test-only SDK.  It must never be linked by a
# Softline library target or appear in exported package metadata.
set(SOFTLINE_TEST_LIBMDF_VERSION "0.8.0")

function(softline_enable_test_libmdf target)
  if(NOT SL_TARGET_ID MATCHES
      "^(x86_64-linux-gnu|x86_64-linux-musl|aarch64-linux-gnu|aarch64-linux-musl|armhf-linux-gnu|armhf-linux-musl|arm64-apple-darwin)$")
    message(FATAL_ERROR
      "libmdf streaming integration requires a supported SL_TARGET_ID; got '${SL_TARGET_ID}'")
  endif()

  set(_softline_libmdf_name
    "libmdf-${SOFTLINE_TEST_LIBMDF_VERSION}-${SL_TARGET_ID}.tar.gz")
  if(SL_TARGET_ID STREQUAL "x86_64-linux-gnu")
    set(_softline_libmdf_sha "1ec76113c8326fa80ff85f15728bcd5128718a380a16fb3ff70ca169b297ba6d")
  elseif(SL_TARGET_ID STREQUAL "x86_64-linux-musl")
    set(_softline_libmdf_sha "d0e48f2f3049d1d9c101314320eb7281d4ef983a081c8bf86ac56171f1879fba")
  elseif(SL_TARGET_ID STREQUAL "aarch64-linux-gnu")
    set(_softline_libmdf_sha "d176ece0d8cf0e70268afdb6151ee17c4a03017dd1b0e453f06d318ce6cd02cd")
  elseif(SL_TARGET_ID STREQUAL "aarch64-linux-musl")
    set(_softline_libmdf_sha "7556ae3bdb57d3ed4dfdb1468b29962a71cddca2b00b92935f67f57c97bec8b2")
  elseif(SL_TARGET_ID STREQUAL "armhf-linux-gnu")
    set(_softline_libmdf_sha "c675573c46095ba62bb26164a1c07c29da8ecb1d6c173ebfa28a53341b091f3a")
  elseif(SL_TARGET_ID STREQUAL "armhf-linux-musl")
    set(_softline_libmdf_sha "6b1cfd9aae1fdb3426776cf3a10a960e0f8e5f0f2a82266b49c8300793a3ff9b")
  else()
    set(_softline_libmdf_sha "99504a42eec7ee21dedbda724211e43673fc7f134926111ed15b21ea6e19d49b")
  endif()

  include(${SOFTLINE_TEST_LIBMDF_MODULE_DIR}/softline_verified_archive.cmake)
  softline_verified_archive(
    "https://github.com/sa6mwa/libmdf/releases/download/v${SOFTLINE_TEST_LIBMDF_VERSION}/${_softline_libmdf_name}"
    "${_softline_libmdf_sha}" "${_softline_libmdf_name}" _softline_libmdf_archive)

  set(_softline_libmdf_extract
    "${CMAKE_BINARY_DIR}/test-deps/libmdf-${SOFTLINE_TEST_LIBMDF_VERSION}-${SL_TARGET_ID}")
  set(_softline_libmdf_root
    "${_softline_libmdf_extract}/libmdf-${SOFTLINE_TEST_LIBMDF_VERSION}-${SL_TARGET_ID}")
  if(NOT EXISTS "${_softline_libmdf_root}/lib")
    file(MAKE_DIRECTORY "${_softline_libmdf_extract}")
    file(ARCHIVE_EXTRACT INPUT "${_softline_libmdf_archive}"
      DESTINATION "${_softline_libmdf_extract}")
  endif()
  if(NOT EXISTS "${_softline_libmdf_root}/lib/pkgconfig/libmdf.pc")
    message(FATAL_ERROR
      "libmdf SDK archive has no pkg-config metadata: ${_softline_libmdf_root}")
  endif()

  find_package(PkgConfig REQUIRED)
  set(_softline_old_pkg_config_path "$ENV{PKG_CONFIG_PATH}")
  set(_softline_old_pkg_config_libdir "$ENV{PKG_CONFIG_LIBDIR}")
  set(ENV{PKG_CONFIG_PATH}
    "${_softline_libmdf_root}/lib/pkgconfig:${_softline_old_pkg_config_path}")
  set(ENV{PKG_CONFIG_LIBDIR} "${_softline_libmdf_root}/lib/pkgconfig")
  pkg_check_modules(SOFTLINE_TEST_LIBMDF REQUIRED IMPORTED_TARGET
    "libmdf=${SOFTLINE_TEST_LIBMDF_VERSION}")
  set(ENV{PKG_CONFIG_PATH} "${_softline_old_pkg_config_path}")
  set(ENV{PKG_CONFIG_LIBDIR} "${_softline_old_pkg_config_libdir}")

  target_link_libraries(${target} PRIVATE PkgConfig::SOFTLINE_TEST_LIBMDF)
  # CMake's sysroot-only library search can retain -lmdf while dropping the
  # relocatable -L entry from a third-party .pc file. Keep pkg-config as the
  # authoritative include/link contract and make its SDK library directory
  # explicit for this private test executable.
  target_link_directories(${target} PRIVATE "${_softline_libmdf_root}/lib")
  set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH
    "${_softline_libmdf_root}/lib")
  set(SOFTLINE_TEST_LIBMDF_RUNTIME_ROOT "${_softline_libmdf_root}" PARENT_SCOPE)
endfunction()
