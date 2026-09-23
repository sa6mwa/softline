include_guard(GLOBAL)

set(SOFTLINE_TEST_LIBMDF_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}")

# libmdf is only for examples and integration tests. It must never be linked
# by a Softline library target or appear in exported package metadata.
set(SOFTLINE_TEST_LIBMDF_VERSION "0.12.0")

function(softline_test_libmdf_supported output)
  if(SL_TARGET_ID MATCHES
      "^(x86_64-linux-gnu|x86_64-linux-musl|aarch64-linux-gnu|aarch64-linux-musl|armhf-linux-gnu|armhf-linux-musl|arm64-apple-darwin)$")
    set(${output} TRUE PARENT_SCOPE)
  else()
    set(${output} FALSE PARENT_SCOPE)
  endif()
endfunction()

function(softline_enable_dev_libmdf target)
  softline_test_libmdf_supported(_softline_libmdf_supported)
  if(NOT _softline_libmdf_supported)
    message(FATAL_ERROR
      "libmdf streaming integration requires a supported SL_TARGET_ID; got '${SL_TARGET_ID}'")
  endif()

  set(_softline_libmdf_name
    "libmdf-${SOFTLINE_TEST_LIBMDF_VERSION}-${SL_TARGET_ID}.tar.gz")
  if(SL_TARGET_ID STREQUAL "x86_64-linux-gnu")
    set(_softline_libmdf_sha "749fc16b96600afcaa885bbab9c5ad0192bc53626ab349d9c49200f0bdca697a")
  elseif(SL_TARGET_ID STREQUAL "x86_64-linux-musl")
    set(_softline_libmdf_sha "5846405faff4f94e4838eed1aebafe94889dd47db8921d6a28cbfa7443601021")
  elseif(SL_TARGET_ID STREQUAL "aarch64-linux-gnu")
    set(_softline_libmdf_sha "5fdc0b0fdd7e011377c31b23450f4f235267c37fdeb3e20505fb230ca151b292")
  elseif(SL_TARGET_ID STREQUAL "aarch64-linux-musl")
    set(_softline_libmdf_sha "f2f8d1b8f98187c1af5bc30c6d306807760ea383b0e5beae19d16a8275d4c59c")
  elseif(SL_TARGET_ID STREQUAL "armhf-linux-gnu")
    set(_softline_libmdf_sha "ef49525af2f22f10fc42d8492c0eb123e7db14b44ed79db800e6ba9f5bdddcca")
  elseif(SL_TARGET_ID STREQUAL "armhf-linux-musl")
    set(_softline_libmdf_sha "28ff6149d49e9a76a9e8e5f87602d3daf2ba234e790e05817241953ab3c4186c")
  else()
    set(_softline_libmdf_sha "3ce4ee8307b175d80742e9771027eabce7e2a6d401013511baaf90f9520626f2")
  endif()

  include(${SOFTLINE_TEST_LIBMDF_MODULE_DIR}/softline_verified_archive.cmake)
  softline_verified_archive(
    "libmdf"
    "https://github.com/sa6mwa/libmdf/releases/download/v${SOFTLINE_TEST_LIBMDF_VERSION}/${_softline_libmdf_name}"
    "${_softline_libmdf_sha}" "${_softline_libmdf_name}" _softline_libmdf_archive)

  set(_softline_libmdf_cache_root
    "${PROJECT_SOURCE_DIR}/.cache")
  set(_softline_libmdf_extract
    "${_softline_libmdf_cache_root}/deps-build/${SL_TARGET_ID}/libmdf")
  set(_softline_libmdf_root
    "${_softline_libmdf_cache_root}/deps/${SL_TARGET_ID}/libmdf/install")
  set(_softline_libmdf_contract
    "${_softline_libmdf_cache_root}/dependency-contracts/${SL_TARGET_ID}/libmdf.txt")
  set(_softline_libmdf_contract_value
    "version=${SOFTLINE_TEST_LIBMDF_VERSION}\narchive_sha256=${_softline_libmdf_sha}\n")
  set(_softline_libmdf_stale FALSE)
  if(NOT EXISTS "${_softline_libmdf_contract}")
    set(_softline_libmdf_stale TRUE)
  else()
    file(READ "${_softline_libmdf_contract}" _softline_libmdf_existing_contract)
    if(NOT _softline_libmdf_existing_contract STREQUAL _softline_libmdf_contract_value)
      set(_softline_libmdf_stale TRUE)
    endif()
  endif()
  if(NOT EXISTS "${_softline_libmdf_root}/lib/pkgconfig/libmdf.pc")
    set(_softline_libmdf_stale TRUE)
  endif()
  if(_softline_libmdf_stale)
    file(REMOVE_RECURSE "${_softline_libmdf_extract}" "${_softline_libmdf_root}")
    file(MAKE_DIRECTORY "${_softline_libmdf_extract}" "${_softline_libmdf_root}")
    file(ARCHIVE_EXTRACT INPUT "${_softline_libmdf_archive}"
      DESTINATION "${_softline_libmdf_extract}")
    set(_softline_libmdf_archive_root
      "${_softline_libmdf_extract}/libmdf-${SOFTLINE_TEST_LIBMDF_VERSION}-${SL_TARGET_ID}")
    if(NOT EXISTS "${_softline_libmdf_archive_root}/lib")
      message(FATAL_ERROR
        "libmdf SDK archive has an unexpected layout: ${_softline_libmdf_archive_root}")
    endif()
    file(COPY "${_softline_libmdf_archive_root}/" DESTINATION "${_softline_libmdf_root}")
    get_filename_component(_softline_libmdf_contract_dir
      "${_softline_libmdf_contract}" DIRECTORY)
    file(MAKE_DIRECTORY "${_softline_libmdf_contract_dir}")
    file(WRITE "${_softline_libmdf_contract}" "${_softline_libmdf_contract_value}")
  endif()
  if(NOT EXISTS "${_softline_libmdf_root}/lib/pkgconfig/libmdf.pc")
    message(FATAL_ERROR
      "libmdf SDK archive has no pkg-config metadata: ${_softline_libmdf_root}")
  endif()

  find_package(PkgConfig REQUIRED)
  set(_softline_old_pkg_config_path "$ENV{PKG_CONFIG_PATH}")
  set(_softline_old_pkg_config_libdir "$ENV{PKG_CONFIG_LIBDIR}")
  set(_softline_old_pkg_config_sysroot_dir "$ENV{PKG_CONFIG_SYSROOT_DIR}")
  set(ENV{PKG_CONFIG_PATH}
    "${_softline_libmdf_root}/lib/pkgconfig:${_softline_old_pkg_config_path}")
  set(ENV{PKG_CONFIG_LIBDIR} "${_softline_libmdf_root}/lib/pkgconfig")
  # The extracted SDK records absolute target paths. An active cross sysroot
  # would incorrectly prefix those paths during this private lookup.
  set(ENV{PKG_CONFIG_SYSROOT_DIR} "")
  pkg_check_modules(SOFTLINE_TEST_LIBMDF REQUIRED IMPORTED_TARGET
    "libmdf=${SOFTLINE_TEST_LIBMDF_VERSION}")
  set(ENV{PKG_CONFIG_PATH} "${_softline_old_pkg_config_path}")
  set(ENV{PKG_CONFIG_LIBDIR} "${_softline_old_pkg_config_libdir}")
  set(ENV{PKG_CONFIG_SYSROOT_DIR} "${_softline_old_pkg_config_sysroot_dir}")

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
