#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="$ROOT_DIR/examples/hardrt_h755_blinky_cpp"

"$ROOT_DIR/scripts/build-lib-stm32h7xx.sh" --hardrt "$ROOT_DIR" --app "$APP_DIR" --build-type Release

ELF="$APP_DIR/build-cortex_m/hardrt_h755_blinky_cpp.elf"
[[ -s "$ELF" ]] || { echo "[FAIL] C++ blinky ELF not found: $ELF" >&2; exit 1; }

openocd -s /usr/share/openocd/scripts \
  -f "$ROOT_DIR/scripts/openocd_h755_clean.cfg" \
  -c "init; reset halt; stm32h7x mass_erase 0; stm32h7x mass_erase 1; program $ELF verify; reset halt; shutdown"

echo "Image programmed and verified; target is halted for debugger/reset-controlled validation."
