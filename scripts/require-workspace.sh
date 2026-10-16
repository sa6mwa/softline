#!/usr/bin/env bash
# Source before creating generated state. A native CMake check is shared with
# direct configuration, including effective cache values from the CMake CLI.
softline_workspace_root=$(cd "${BASH_SOURCE[0]%/*}/.." && pwd -P) || return $?
softline_workspace_args=("-DSOFTLINE_WORKSPACE_ROOT=$softline_workspace_root")
if [[ -n ${SOFTLINE_GENERATED_PATH:-} ]]; then
  softline_workspace_args+=("-DSOFTLINE_GENERATED_PATH=$SOFTLINE_GENERATED_PATH")
fi
cmake "${softline_workspace_args[@]}" \
  -P "$softline_workspace_root/cmake/softline_workspace.cmake" || return $?
export CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-4}
unset softline_workspace_root softline_workspace_args
