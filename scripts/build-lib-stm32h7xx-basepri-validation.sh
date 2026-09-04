#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="$ROOT_DIR/examples/hardrt_h755_basepri_validation"
DO_FLASH=1

usage() {
  cat <<'USAGE'
Usage: scripts/build-lib-stm32h7xx-basepri-validation.sh [options]

Options:
  --no-flash
  -h, --help

Validates the Cortex-M HardRT critical-section BASEPRI contract on STM32H755:
  - unmasked entry installs the HardRT ceiling and restores zero;
  - a weaker existing BASEPRI is raised to the HardRT ceiling;
  - a stricter existing BASEPRI is preserved rather than weakened;
  - nested critical sections restore only at the outermost exit.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-flash) DO_FLASH=0; shift;;
    -h|--help) usage; exit 0;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 1;;
  esac
done

"$ROOT_DIR/scripts/build-lib-stm32h7xx.sh" \
  --hardrt "$ROOT_DIR" \
  --app "$APP_DIR" \
  --build-type Release \
  --hardrt-cmake-arg "-DHARDRT_TIMING_PROFILE=none"

ELF="$APP_DIR/build-cortex_m/hardrt_h755_basepri_validation.elf"
[[ -s "$ELF" ]] || { echo "[FAIL] BASEPRI validation ELF not found: $ELF" >&2; exit 1; }

if (( DO_FLASH )); then
  openocd -s /usr/share/openocd/scripts \
    -f "$ROOT_DIR/scripts/openocd_h755_clean.cfg" \
    -c "init; reset halt; stm32h7x mass_erase 0; stm32h7x mass_erase 1; program $ELF verify; reset halt; shutdown"
else
  echo "[INFO] --no-flash selected; build complete only"
fi

cat <<EOF

Automated validation:
  Terminal 1:
    openocd -s /usr/share/openocd/scripts -f "$ROOT_DIR/scripts/openocd_h755.cfg" -c "init; reset halt"

  Terminal 2:
    gdb-multiarch -q "$ELF" -batch -x "$ROOT_DIR/scripts/gdb/basepri_validation.dbg"
EOF
