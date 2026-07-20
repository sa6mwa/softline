#!/bin/sh
set -eu

CC="${SOFTLINE_LUA_CC:-${1:?missing compiler}}"
CFLAGS="${2:?missing CFLAGS}"
LIBFLAG="${3:?missing LIBFLAG}"
OBJ_EXTENSION="${4:?missing object extension}"
LIB_EXTENSION="${5:?missing library extension}"
LUA_INCDIR="${6:?missing Lua include directory}"

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/lua-rock"
OBJ="${BUILD_DIR}/softline_lua.${OBJ_EXTENSION}"
MOD="${BUILD_DIR}/softline.${LIB_EXTENSION}"
LUA_COMPILE_INCDIR="${LUA_INCDIR}"

mkdir -p "${BUILD_DIR}"

LUA_HEADER="${LUA_INCDIR}/lua.h"
if [ ! -f "${LUA_HEADER}" ]; then
  echo "ERROR: LuaRocks selected Lua headers are missing ${LUA_HEADER}" >&2
  exit 1
fi

# LUA_INCDIR is LuaRocks' selected runtime contract. Do not consult an ambient
# lua executable: it can select a different installed runtime.
LUA_VERSION_MAJOR="$(awk '$1 == "#define" && $2 == "LUA_VERSION_MAJOR_N" { print $3; exit }' "${LUA_HEADER}")"
LUA_VERSION_MINOR="$(awk '$1 == "#define" && $2 == "LUA_VERSION_MINOR_N" { print $3; exit }' "${LUA_HEADER}")"
LUA_VERSION_NUM="$(awk '$1 == "#define" && $2 == "LUA_VERSION_NUM" { print $3; exit }' "${LUA_HEADER}")"
if [ -z "${LUA_VERSION_MAJOR}" ] || [ -z "${LUA_VERSION_MINOR}" ]; then
  if [ "${LUA_VERSION_NUM}" = "505" ]; then
    LUA_VERSION_MAJOR=5
    LUA_VERSION_MINOR=5
  else
    LUA_VERSION_MAJOR="${LUA_VERSION_NUM:-missing}"
    LUA_VERSION_MINOR=""
  fi
fi
if [ "${LUA_VERSION_MAJOR}" != "5" ] || [ "${LUA_VERSION_MINOR}" != "5" ]; then
  echo "ERROR: softline Lua facade supports Lua 5.5 only; selected headers report ${LUA_VERSION_MAJOR:-missing}.${LUA_VERSION_MINOR:-missing}" >&2
  exit 1
fi

if [ -n "${SOFTLINE_LUA_CC:-}" ]; then
  LUA_COMPILE_INCDIR="${BUILD_DIR}/lua-include"
  rm -rf "${LUA_COMPILE_INCDIR}"
  mkdir -p "${LUA_COMPILE_INCDIR}"
  cp "${LUA_INCDIR}/"*.h "${LUA_COMPILE_INCDIR}/"
  if grep -q 'lua5.5-deb-multiarch.h' "${LUA_COMPILE_INCDIR}/"*.h; then
    LUA_MULTIARCH_HEADER="$(find /usr/include -name lua5.5-deb-multiarch.h -print | sed -n '1p')"
    if [ -z "${LUA_MULTIARCH_HEADER}" ]; then
      echo "ERROR: Lua headers reference lua5.5-deb-multiarch.h but it was not found under /usr/include" >&2
      exit 1
    fi
    cp "${LUA_MULTIARCH_HEADER}" "${LUA_COMPILE_INCDIR}/"
  fi
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

"${CC}" ${CFLAGS} -Wall -Wextra -Werror -I"${LUA_COMPILE_INCDIR}" \
  ${SOFTLINE_CFLAGS} -c "${ROOT_DIR}/lua/softline_lua.c" -o "${OBJ}"
"${CC}" ${LIBFLAG} -o "${MOD}" "${OBJ}" ${SOFTLINE_LIBS}
