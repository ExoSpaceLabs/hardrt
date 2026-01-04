#!/usr/bin/env bash
set -euo pipefail

# -------- Config knobs (override via env or CLI) --------
HARDRT_DIR="${HARDRT_DIR:-$(pwd)}"        # path to hardrt repo
APP_DIR="${APP_DIR:-$(pwd)/examples/hardrt_h755_demo}"  # path to your app repo
PORT="${PORT:-cortex_m}"                    # cortex_m | posix | null
TC_FILE="${TC_FILE:-$HARDRT_DIR/cmake/toolchains/arm-none-eabi.cmake}"  # only used for cortex_m
BUILD_TYPE="${BUILD_TYPE:-Release}"         # or Debug
GENERATOR="${GENERATOR:-Unix Makefiles}"
JOBS="${JOBS:-$(nproc)}"
STM32CUBE_H7_ROOT="${STM32CUBE_H7_ROOT:-/home/dev/STM32Cube/Repository/STM32CubeH7}"
CTESTS="${CTESTS:-OFF}"
# CLI overrides (optional)
usage() {
  echo "Usage: $0 [--hardrt DIR] [--app DIR] [--stm32h7 DIR] [--port cortex_m|posix|null] [--toolchain FILE] [--build-type TYPE] [--generator GEN] [--jobs N]"
  exit 1
}
while [[ $# -gt 0 ]]; do
  case "$1" in
    --hardrt) HARDRT_DIR="$2"; shift 2;;
    --app) APP_DIR="$2"; shift 2;;
    --stm32h7) STM32CUBE_H7_ROOT="$2"; shift 2;;
    --port) PORT="$2"; shift 2;;
    --toolchain) TC_FILE="$2"; shift 2;;
    --build-type) BUILD_TYPE="$2"; shift 2;;
    --generator) GENERATOR="$2"; shift 2;;
    --c-tests) CTESTS="ON"; shift 1;;
    --jobs) JOBS="$2"; shift 2;;
    -h|--help) usage;;
    *) echo "Unknown arg: $1"; usage;;
  esac
done

echo "[INFO] HardRT    : $HARDRT_DIR"
echo "[INFO] App       : $APP_DIR"
echo "[INFO] Toolchain : $TC_FILE"
echo "[INFO] STM32H7   : $STM32CUBE_H7_ROOT"
echo "[INFO] Port      : $PORT"
echo "[INFO] Build     : $BUILD_TYPE"
echo "[INFO] CTests    : $CTESTS"
echo "[INFO] Gen       : $GENERATOR"

# -------- Build HardRT static lib --------
pushd "$HARDRT_DIR" >/dev/null

BUILD_DIR="build-${PORT,,}"
INSTALL_DIR="$PWD/install-${PORT,,}"
rm -rf "$BUILD_DIR" "$INSTALL_DIR"
mkdir -p "$BUILD_DIR"
pushd "$BUILD_DIR" >/dev/null

CMAKE_ARGS=(
  -DHARDRT_PORT="$PORT"
  -DHARDRT_BUILD_EXAMPLES=OFF
  -DHARDRT_ENABLE_CPP=ON
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
  -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
  -DHARDRT_BUILD_TESTS="$CTESTS"
  -DHARDRT_STALL_ON_ERROR=ON
)

if [[ "$PORT" == "cortex_m" ]]; then
  CMAKE_ARGS+=(-DCMAKE_TOOLCHAIN_FILE="$TC_FILE")
fi

cmake -G "$GENERATOR" "${CMAKE_ARGS[@]}" ..
cmake --build . -j"$JOBS"
cmake --install .

# Paths to the built artifacts (for direct-link fallback)
HARDRT_LIB="$(find "$PWD" -name 'libhardrt.a' | head -n 1 || true)"
HARDRT_INC="$INSTALL_DIR/include"
HARDRT_PKG_DIR="$INSTALL_DIR/lib/cmake/HardRT"

echo "[INFO] libhardrt.a  : ${HARDRT_LIB:-NOT FOUND}"
echo "[INFO] include dir  : $HARDRT_INC"
echo "[INFO] package dir  : $HARDRT_PKG_DIR"

popd >/dev/null  # end hardrt build
popd >/dev/null

# -------- Build the application that links HardRT --------
pushd "$APP_DIR" >/dev/null
APP_BUILD_DIR="build-${PORT,,}"
rm -rf "$APP_BUILD_DIR"
mkdir -p "$APP_BUILD_DIR"
pushd "$APP_BUILD_DIR" >/dev/null

# Prefer clean package consumption via CMAKE_PREFIX_PATH.
# Also export raw hints for apps that still link the .a directly.
export CMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH:+$CMAKE_PREFIX_PATH;}$HARDRT_PKG_DIR"
export HARDRT_LIB_PATH="${HARDRT_LIB:-}"
export HARDRT_INCLUDE_PATH="$HARDRT_INC"

APP_ARGS=(
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
  -DSTM32CUBE_H7_ROOT="$STM32CUBE_H7_ROOT"
)

if [[ "$PORT" == "cortex_m" ]]; then
  APP_ARGS+=(-DCMAKE_TOOLCHAIN_FILE="$TC_FILE")
fi

# Optional: let the app choose the port at configure time as well
APP_ARGS+=(-DHARDRT_PORT="$PORT")

echo "[INFO] Configuring app with:"
printf '  %q\n' cmake -G "$GENERATOR" "${APP_ARGS[@]}" ..
cmake -G "$GENERATOR" "${APP_ARGS[@]}" ..

echo "[INFO] Building app…"
cmake --build . -j"$JOBS"

echo "[OK] HardRT + App build completed."
echo "[HINT] If your app uses find_package(HardRT), it should already be linked."
echo "[HINT] If it links directly, use HARDRT_LIB_PATH and HARDRT_INCLUDE_PATH in your app CMakeLists."

popd >/dev/null  # end app build
popd >/dev/null  # end app dir
