cmake_minimum_required(VERSION 3.24)
get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include(../softline_workspace.cmake)
softline_check_workspace("${root}")
project(softline_host_tests LANGUAGES NONE)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(BASH_EXECUTABLE bash REQUIRED)
execute_process(COMMAND "${BASH_EXECUTABLE}" "${root}/scripts/require-host-bash.sh"
  COMMAND_ERROR_IS_FATAL ANY)
enable_testing()
add_test(NAME clangd_gate COMMAND "${BASH_EXECUTABLE}"
  "${root}/tests/lifecycle/clangd-gate.sh" "${root}")
set_tests_properties(clangd_gate PROPERTIES TIMEOUT 30 LABELS "host;lifecycle")
add_test(NAME workspace_ownership COMMAND "${BASH_EXECUTABLE}"
  "${root}/tests/lifecycle/workspace.sh" "${root}")
set_tests_properties(workspace_ownership PROPERTIES TIMEOUT 30 LABELS "host;lifecycle")
function(host_shell name script)
  add_test(NAME "${name}" COMMAND "${BASH_EXECUTABLE}" "${root}/scripts/${script}")
  set_tests_properties("${name}" PROPERTIES TIMEOUT 90 LABELS "host;lifecycle"
    REQUIRED_FILES "${root}/scripts/${script};${root}/scripts/require-host-bash.sh;${ARGN}")
endfunction()
host_shell(tool_discovery test_discover_target_tools.sh "${root}/scripts/discover_target_tools.sh")
host_shell(toolchain_contract test_toolchain_contract.sh
  "${root}/scripts/cpkt-toolchains.sh" "${root}/scripts/cpkt-aflpp.sh"
  "${root}/scripts/cpkt-archive-cache.sh" "${root}/scripts/cpkt-afl-runtime.sh"
  "${root}/scripts/test-cpkt-toolchain-resolvers.sh" "${root}/scripts/test-cpkt-aflpp-resolver.sh")
host_shell(darwin_linker_route test_darwin_linker_route.sh
  "${root}/scripts/cpkt-toolchains.sh" "${root}/cmake/toolchains/osxcross-darwin.cmake")
add_test(NAME lifecycle_surface COMMAND "${CMAKE_COMMAND}" "-DROOT=${root}"
  -P "${root}/tests/lifecycle/presets.cmake")
host_shell(lua_artifact_privacy test_lua_artifact_privacy.sh
  "${root}/scripts/validate_luarocks.sh" "${root}/scripts/release_version.sh")
set(api_inventory "${CMAKE_BINARY_DIR}/api-inventory.json")
add_custom_command(OUTPUT "${api_inventory}"
  COMMAND "${Python3_EXECUTABLE}" "${root}/scripts/api-inventory.py" "${root}" "${api_inventory}"
  DEPENDS "${root}/scripts/api-inventory.py" "${root}/include/softline/softline.h"
    "${root}/lua/softline_lua.c" "${root}/CMakeLists.txt" VERBATIM)
add_custom_target(api_inventory ALL DEPENDS "${api_inventory}")
add_test(NAME public_header_docs COMMAND "${CMAKE_COMMAND}" "-DROOT=${root}"
  "-DINVENTORY=${api_inventory}" -P "${root}/tests/lifecycle/public-api.cmake")
set_tests_properties(public_header_docs PROPERTIES TIMEOUT 30 LABELS "host;api")
add_test(NAME public_header_docs_negative COMMAND "${CMAKE_COMMAND}" "-DROOT=${root}"
  "-DINVENTORY=${api_inventory}" -P "${root}/tests/lifecycle/header-doc-fixtures.cmake")
set_tests_properties(public_header_docs_negative PROPERTIES TIMEOUT 30 LABELS "host;api")
add_test(NAME runtime_config COMMAND "${BASH_EXECUTABLE}"
  "${root}/tests/lifecycle/cache.sh" "${root}")
add_test(NAME artifact_portability COMMAND "${BASH_EXECUTABLE}" "${root}/tests/lifecycle/artifact-portability.sh" "${root}")
add_test(NAME artifact_runtime COMMAND "${BASH_EXECUTABLE}" "${root}/tests/lifecycle/artifact-runtime.sh" "${root}")
add_test(NAME lua_env COMMAND "${BASH_EXECUTABLE}" "${root}/tests/lifecycle/lua-env.sh" "${root}")
add_test(NAME lua_runtime_selection COMMAND "${BASH_EXECUTABLE}" "${root}/tests/lifecycle/lua-selection.sh" "${root}")
add_test(NAME lua_platform COMMAND "${BASH_EXECUTABLE}" "${root}/tests/lifecycle/lua-platform.sh" "${root}")
set_tests_properties(artifact_portability artifact_runtime lua_runtime_selection lua_platform PROPERTIES TIMEOUT 90 LABELS "host;lifecycle")
add_test(NAME deps_target COMMAND "${BASH_EXECUTABLE}" "${root}/tests/lifecycle/targets.sh" "${root}")
add_test(NAME source_workspace COMMAND "${BASH_EXECUTABLE}"
  "${root}/tests/lifecycle/source-workspace.sh" "${root}")
add_test(NAME runtime_missing COMMAND "${CMAKE_COMMAND}" "-DROOT=${root}"
  -P "${root}/tests/lifecycle/runtime-missing.cmake")
set_tests_properties(runtime_missing PROPERTIES WILL_FAIL TRUE)
add_test(NAME resolver_provenance COMMAND "${CMAKE_COMMAND}" "-DROOT=${root}"
  -P "${root}/tests/lifecycle/vendor.cmake")
set_tests_properties(lifecycle_surface runtime_config runtime_missing lua_env resolver_provenance deps_target source_workspace
  PROPERTIES TIMEOUT 60 LABELS "host;lifecycle")

add_test(NAME terminal_cache COMMAND "${BASH_EXECUTABLE}" "${root}/tests/lifecycle/terminal-cache.sh" "${root}")
set_tests_properties(terminal_cache PROPERTIES TIMEOUT 60 LABELS "host;lifecycle")
add_test(NAME source_artifacts COMMAND "${BASH_EXECUTABLE}" "${root}/tests/lifecycle/source-artifacts.sh" "${root}")
set_tests_properties(source_artifacts PROPERTIES TIMEOUT 30 LABELS "host;packaging")
add_test(NAME checksum_inventory COMMAND "${BASH_EXECUTABLE}" "${root}/tests/lifecycle/checksum-inventory.sh" "${root}")
set_tests_properties(checksum_inventory PROPERTIES TIMEOUT 30 LABELS "host;packaging")
add_test(NAME release_sequence COMMAND "${BASH_EXECUTABLE}" "${root}/tests/lifecycle/release-sequence.sh" "${root}")
set_tests_properties(release_sequence PROPERTIES TIMEOUT 30 LABELS "host;packaging")
host_shell(package_source_worktree test_package_source_worktree.sh
  "${root}/scripts/package-source.sh" "${root}/scripts/release_version.sh")
host_shell(release_version test_release_version.sh "${root}/scripts/release_version.sh" "${root}/cmake/softline_version.cmake")
set_tests_properties(release_version PROPERTIES LABELS "host;version-contract")
add_test(NAME release_identity COMMAND "${BASH_EXECUTABLE}"
  "${root}/tests/lifecycle/release-identity.sh" "${root}")
set_tests_properties(release_identity PROPERTIES TIMEOUT 30 LABELS "host;version-contract")
