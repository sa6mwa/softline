include_guard(GLOBAL)

# Private to non-shipped executables. Never attach these options to an SDK
# library or its exported interface.
function(softline_local_runtime target)
  set(flags "")
  # Local dlopen consumers need transitive lookup even on host-toolchain
  # fallback Linux builds. This does not select a foreign ELF interpreter.
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(flags "-Wl,--disable-new-dtags\n")
  endif()
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
     NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$" OR
     NOT SL_TARGET_ID MATCHES "^x86_64-linux-")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      target_link_options(${target} PRIVATE "LINKER:--disable-new-dtags")
    endif()
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${target}-runtime.flags" CONTENT "${flags}")
    return()
  endif()
  if(NOT CMAKE_SYSROOT)
    message(FATAL_ERROR "Local Bootlin runtime requires the selected sysroot")
  endif()
  if(SL_TARGET_ID MATCHES "musl$")
    set(loader "${CMAKE_SYSROOT}/lib/ld-musl-x86_64.so.1")
  else()
    set(loader "${CMAKE_SYSROOT}/lib/ld-linux-x86-64.so.2")
  endif()
  if(NOT EXISTS "${loader}")
    message(FATAL_ERROR "Selected Bootlin ELF interpreter is missing: ${loader}")
  endif()
  set(runtime_dirs "${CMAKE_SYSROOT}/lib" "${CMAKE_SYSROOT}/usr/lib")
  foreach(library libgcc_s.so.1 libstdc++.so.6 libasan.so libubsan.so)
    execute_process(COMMAND "${CMAKE_C_COMPILER}" "-print-file-name=${library}"
      OUTPUT_VARIABLE path OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(IS_ABSOLUTE "${path}" AND EXISTS "${path}")
      get_filename_component(directory "${path}" DIRECTORY)
      list(APPEND runtime_dirs "${directory}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES runtime_dirs)
  target_link_options(${target} PRIVATE "LINKER:--dynamic-linker,${loader}"
    "LINKER:--disable-new-dtags")
  # Compiler runtimes carry their own RUNPATH, which supersedes inherited
  # RPATH. Resolve libm directly before walking those indirect dependencies.
  target_link_libraries(${target} PRIVATE "-Wl,--push-state,--no-as-needed" m "-Wl,--pop-state")
  set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "${runtime_dirs}")
  # Also expose these exact settings to non-CMake verification links.
  string(APPEND flags "-Wl,--dynamic-linker,${loader}\n")
  string(APPEND flags "-Wl,--push-state,--no-as-needed\n-lm\n-Wl,--pop-state\n")
  foreach(directory IN LISTS runtime_dirs)
    string(APPEND flags "-Wl,-rpath,${directory}\n")
  endforeach()
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${target}-runtime.flags"
    CONTENT "${flags}")
endfunction()
