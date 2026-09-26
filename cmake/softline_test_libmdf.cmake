include_guard(GLOBAL)

set(SOFTLINE_TEST_LIBMDF_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}")

# libmdf is only for examples and integration tests. It must never be linked
# by a Softline library target or appear in exported package metadata.
set(SOFTLINE_TEST_LIBMDF_VERSION "0.13.0")

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
    set(_softline_libmdf_sha "6be317bb232702cab5fb2f7f6041fa6b6cc0cb6d3676cb1230f80a3778c1c4b4")
  elseif(SL_TARGET_ID STREQUAL "x86_64-linux-musl")
    set(_softline_libmdf_sha "1fd8758bed2eab39b84a6265dad32386531943c13ee400a0ab6663e98d2cd404")
  elseif(SL_TARGET_ID STREQUAL "aarch64-linux-gnu")
    set(_softline_libmdf_sha "4c4592a97369d2ed6f5b97c6fbcda8de3562fa21f3c1fd4d9383db7ddda9dc61")
  elseif(SL_TARGET_ID STREQUAL "aarch64-linux-musl")
    set(_softline_libmdf_sha "b521eb7efd5b94e6727647574ba2a2a566fdd10c18215e37f833e39545e3f4b4")
  elseif(SL_TARGET_ID STREQUAL "armhf-linux-gnu")
    set(_softline_libmdf_sha "ce880058303392436738c058416af2efab67fc0737695d5f5f4a0fd6ce75785a")
  elseif(SL_TARGET_ID STREQUAL "armhf-linux-musl")
    set(_softline_libmdf_sha "f1e079531d6f2c580df0a67d47ca8c9749b997f8e9965b8903c6bd33eb95f59f")
  else()
    set(_softline_libmdf_sha "84abad09c7d1eabf4b614fd42e11c5b7e3a9dec5fd33c2bbea5598c4bfac6a07")
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
