#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="$ROOT_DIR/examples/hardrt_h755_fpu_validation"
OPENOCD_SCRIPTS="${OPENOCD_SCRIPTS:-/usr/share/openocd/scripts}"
DEBUG_TIMEOUT="${DEBUG_TIMEOUT:-90}"
DO_FLASH=1
OPENOCD_PID=""

usage() {
  cat <<'USAGE'
Usage: scripts/build-lib-stm32h7xx-fpu-validation.sh [options]

Options:
  --no-flash
  -h, --help

Builds the STM32H755 CM7 hard-float validation image. With the default flash
mode the script also runs the hardware validator automatically: two RR tasks
must preserve independent s0-s31 + FPSCR state for at least 1000 switches each.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-flash) DO_FLASH=0; shift;;
    -h|--help) usage; exit 0;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 1;;
  esac
done

cleanup_openocd() {
  if [[ -n "${OPENOCD_PID:-}" ]] && kill -0 "$OPENOCD_PID" >/dev/null 2>&1; then
    kill "$OPENOCD_PID" >/dev/null 2>&1 || true
    wait "$OPENOCD_PID" >/dev/null 2>&1 || true
  fi
  OPENOCD_PID=""
}
trap cleanup_openocd EXIT INT TERM

if command -v gdb-multiarch >/dev/null 2>&1; then
  GDB_BIN=gdb-multiarch
elif command -v arm-none-eabi-gdb >/dev/null 2>&1; then
  GDB_BIN=arm-none-eabi-gdb
else
  echo "[FAIL] install gdb-multiarch or arm-none-eabi-gdb for hardware validation" >&2
  exit 2
fi

"$ROOT_DIR/scripts/build-lib-stm32h7xx.sh" \
  --hardrt "$ROOT_DIR" \
  --app "$APP_DIR" \
  --build-type Release \
  --hardrt-cmake-arg "-DHARDRT_TIMING_PROFILE=none"

ELF="$APP_DIR/build-cortex_m/hardrt_h755_fpu_validation.elf"
[[ -s "$ELF" ]] || { echo "[FAIL] FPU validation ELF not found: $ELF" >&2; exit 1; }

if (( DO_FLASH == 0 )); then
  echo "[PASS] FPU validation image built: $ELF"
  exit 0
fi

openocd -s "$OPENOCD_SCRIPTS" \
  -f "$ROOT_DIR/scripts/openocd_h755_clean.cfg" \
  -c "init; reset halt; stm32h7x mass_erase 0; stm32h7x mass_erase 1; program $ELF verify; reset halt; shutdown"

OPENOCD_LOG="$APP_DIR/build-cortex_m/fpu_validation_openocd.log"
GDB_LOG="$APP_DIR/build-cortex_m/fpu_validation_gdb.log"

openocd -s "$OPENOCD_SCRIPTS" \
  -f "$ROOT_DIR/scripts/openocd_h755.cfg" \
  -c "init; reset halt" >"$OPENOCD_LOG" 2>&1 &
OPENOCD_PID=$!

for ((i=0; i<100; ++i)); do
  if grep -q "Listening on port 3333 for gdb connections" "$OPENOCD_LOG" 2>/dev/null; then
    break
  fi
  if ! kill -0 "$OPENOCD_PID" >/dev/null 2>&1; then
    cat "$OPENOCD_LOG" >&2
    echo "[FAIL] OpenOCD exited before accepting GDB" >&2
    exit 1
  fi
  sleep 0.1
  if (( i == 99 )); then
    echo "[FAIL] OpenOCD GDB server timeout" >&2
    exit 1
  fi
done

set +e
timeout "${DEBUG_TIMEOUT}s" "$GDB_BIN" -q "$ELF" -batch \
  -x "$ROOT_DIR/scripts/gdb/fpu_validation.dbg" 2>&1 | tee "$GDB_LOG"
GDB_RC=${PIPESTATUS[0]}
set -e
cleanup_openocd

if (( GDB_RC != 0 )) || ! grep -q '^RESULT: PASS$' "$GDB_LOG"; then
  echo "[FAIL] Cortex-M FP context validation failed" >&2
  echo "       GDB log: $GDB_LOG" >&2
  echo "       OpenOCD log: $OPENOCD_LOG" >&2
  exit 1
fi

echo "[PASS] Cortex-M FP context preserved across repeated HardRT switches"
echo "       $(grep '^iterations:' "$GDB_LOG" | tail -n1)"
