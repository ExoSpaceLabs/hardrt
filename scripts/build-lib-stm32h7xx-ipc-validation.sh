#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="$ROOT_DIR/examples/hardrt_h755_ipc_validation"
CASE="semaphore"
DO_FLASH=1

usage() {
  cat <<'USAGE'
Usage: scripts/build-lib-stm32h7xx-ipc-validation.sh [options]

Options:
  --case semaphore|queue|mutex
  --no-flash
  -h, --help
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --case) CASE="$2"; shift 2;;
    --no-flash) DO_FLASH=0; shift;;
    -h|--help) usage; exit 0;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 1;;
  esac
done

case "$CASE" in semaphore|queue|mutex) ;; *) echo "Unsupported IPC case: $CASE" >&2; exit 1;; esac

"$ROOT_DIR/scripts/build-lib-stm32h7xx.sh" \
  --hardrt "$ROOT_DIR" \
  --app "$APP_DIR" \
  --build-type Release \
  --hardrt-cmake-arg "-DHARDRT_TIMING_PROFILE=none" \
  --app-cmake-arg "-DHARDRT_IPC_CASE=$CASE"

ELF="$APP_DIR/build-cortex_m/hardrt_h755_ipc_validation.elf"
[[ -s "$ELF" ]] || { echo "[FAIL] IPC validation ELF not found: $ELF" >&2; exit 1; }

if (( DO_FLASH )); then
  openocd -s /usr/share/openocd/scripts \
    -f "$ROOT_DIR/scripts/openocd_h755_clean.cfg" \
    -c "init; reset halt; stm32h7x mass_erase 0; stm32h7x mass_erase 1; program $ELF verify; reset halt; shutdown"
else
  echo "[INFO] --no-flash selected; build complete only"
fi
