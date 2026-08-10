#!/usr/bin/env bash
# Configure + build TheXTech Dreamcast port
set -eo pipefail

export DREAMSDK_HOME="${DREAMSDK_HOME:-/i/sw/dc-dev/DreamSDK}"
# Avoid unbound vars while sourcing KOS environ
export KOS_INC_PATHS_CPP="${KOS_INC_PATHS_CPP:-}"
export KOS_CPPFLAGS="${KOS_CPPFLAGS:-}"
export KOS_CFLAGS="${KOS_CFLAGS:-}"
export KOS_LDFLAGS="${KOS_LDFLAGS:-}"
export KOS_INC_PATHS="${KOS_INC_PATHS:-}"

if [[ -f /opt/toolchains/dc/kos/environ.sh ]]; then
  # shellcheck disable=SC1091
  source /opt/toolchains/dc/kos/environ.sh
elif [[ -f "$DREAMSDK_HOME/opt/toolchains/dc/kos/environ.sh" ]]; then
  # shellcheck disable=SC1091
  source "$DREAMSDK_HOME/opt/toolchains/dc/kos/environ.sh"
else
  echo "Cannot find kos/environ.sh" >&2
  exit 1
fi

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build-dc"
LOG="$ROOT/build-dreamcast/cmake-dc.log"
mkdir -p "$ROOT/build-dreamcast"

echo "KOS_BASE=$KOS_BASE"
echo "KOS_CC=$KOS_CC"
which kos-cc || true
which cmake || true
cmake --version | head -1

# Prefer fresh configure if toolchain file changed
rm -rf "$BUILD"
cmake -S "$ROOT" -B "$BUILD" \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/dreamcast.toolchain.cmake" \
  -DDREAMCAST=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DTHEXTECH_ENABLE_LUNA_AUTOCODE=OFF \
  -DTHEXTECH_ENABLE_LUA=OFF \
  -DTHEXTECH_ENABLE_TTF_SUPPORT=OFF \
  2>&1 | tee "$LOG"

echo "==> Building..."
cmake --build "$BUILD" -j"$(nproc 2>/dev/null || echo 4)" 2>&1 | tee -a "$LOG"
echo "==> Build finished"
ls -lh "$BUILD"/bin/thextech.elf "$BUILD"/thextech.elf "$BUILD"/output/bin/thextech.elf 2>/dev/null || find "$BUILD" -name 'thextech*' 2>/dev/null | head
