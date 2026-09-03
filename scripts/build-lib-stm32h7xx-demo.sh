#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="$ROOT_DIR/examples/hardrt_h755_demo"
DO_FLASH=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-flash) DO_FLASH=0; shift ;;
    -h|--help)
      echo "Usage: scripts/build-lib-stm32h7xx-demo.sh [--no-flash]"
      exit 0
      ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
  esac
done

"$ROOT_DIR/scripts/build-lib-stm32h7xx.sh" \
  --hardrt "$ROOT_DIR" \
  --app "$APP_DIR" \
  --build-type Release

ELF="$APP_DIR/build-cortex_m/hardrt_cm7_demo.elf"
[[ -s "$ELF" ]] || { echo "[FAIL] demo ELF not found: $ELF" >&2; exit 1; }

if (( DO_FLASH )); then
  openocd -s /usr/share/openocd/scripts \
    -f "$ROOT_DIR/scripts/openocd_h755_clean.cfg" \
    -c "init; reset halt; stm32h7x mass_erase 0; stm32h7x mass_erase 1; program $ELF verify; reset halt; shutdown"
else
  echo "[INFO] --no-flash selected; build complete only"
fi
