#!/bin/sh
set -eu

CC="${1:?missing compiler}"
CFLAGS="${2:?missing CFLAGS}"
LIBFLAG="${3:?missing LIBFLAG}"
OBJ_EXTENSION="${4:?missing object extension}"
LIB_EXTENSION="${5:?missing library extension}"
LUA_INCDIR="${6:?missing Lua include directory}"
LUA_VERSION="${7:?missing Lua version}"

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/lua-rock"
OBJ="${BUILD_DIR}/softline_lua.${OBJ_EXTENSION}"
MOD="${BUILD_DIR}/softline.${LIB_EXTENSION}"

mkdir -p "${BUILD_DIR}"

if [ "${LUA_VERSION}" != "5.5" ]; then
  echo "ERROR: softline Lua facade supports Lua 5.5; got Lua ${LUA_VERSION}" >&2
  exit 1
fi

if [ -n "${SOFTLINE_INCLUDE_DIR:-}" ] && [ -n "${SOFTLINE_LIB_DIR:-}" ]; then
  SOFTLINE_CFLAGS="-I${SOFTLINE_INCLUDE_DIR}"
  SOFTLINE_LIBS="-L${SOFTLINE_LIB_DIR} -lsoftline"
elif command -v pkg-config >/dev/null 2>&1 && pkg-config --exists softline; then
  SOFTLINE_CFLAGS="$(pkg-config --cflags softline)"
  SOFTLINE_LIBS="$(pkg-config --libs softline)"
elif [ -n "${SOFTLINE_DIR:-}" ]; then
  SOFTLINE_CFLAGS="-I${SOFTLINE_DIR}/include"
  if [ -d "${SOFTLINE_DIR}/lib" ]; then
    SOFTLINE_LIBS="-L${SOFTLINE_DIR}/lib -lsoftline"
  elif [ -d "${SOFTLINE_DIR}/lib64" ]; then
    SOFTLINE_LIBS="-L${SOFTLINE_DIR}/lib64 -lsoftline"
  else
    echo "ERROR: SOFTLINE_DIR does not contain lib/ or lib64/" >&2
    exit 1
  fi
else
  echo "ERROR: unable to find softline via pkg-config; set PKG_CONFIG_PATH or SOFTLINE_DIR" >&2
  exit 1
fi

"${CC}" ${CFLAGS} -Wall -Wextra -Werror -I"${LUA_INCDIR}" \
  ${SOFTLINE_CFLAGS} -c "${ROOT_DIR}/lua/softline_lua.c" -o "${OBJ}"
"${CC}" ${LIBFLAG} -o "${MOD}" "${OBJ}" ${SOFTLINE_LIBS}
