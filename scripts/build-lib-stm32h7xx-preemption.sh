#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="$ROOT_DIR/examples/hardrt_h755_preemption"
CASE="priority"
DO_FLASH=1

usage() {
  cat <<'USAGE'
Usage: scripts/build-lib-stm32h7xx-preemption.sh [options]

Options:
  --case priority|priority_rr
  --no-flash
  -h, --help

Cases:
  priority     Hardware IRQ wakes a blocked higher-priority task while a lower
               priority task is busy. The high task must run before Thread mode
               resumes the interrupted low task.

  priority_rr  Adds an equal-priority low peer. Required order is A -> high -> A
               -> B, with A retaining its unused RR quantum across preemption.
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

case "$CASE" in
  priority|priority_rr) ;;
  *) echo "Unsupported preemption case: $CASE" >&2; usage >&2; exit 1;;
esac

"$ROOT_DIR/scripts/build-lib-stm32h7xx.sh" \
  --hardrt "$ROOT_DIR" \
  --app "$APP_DIR" \
  --build-type Release \
  --hardrt-cmake-arg "-DHARDRT_TIMING_PROFILE=none" \
  --app-cmake-arg "-DHARDRT_PREEMPT_CASE=$CASE"

ELF="$APP_DIR/build-cortex_m/hardrt_h755_preemption.elf"
[[ -s "$ELF" ]] || { echo "[FAIL] preemption ELF not found: $ELF" >&2; exit 1; }

if (( DO_FLASH )); then
  openocd -s /usr/share/openocd/scripts \
    -f "$ROOT_DIR/scripts/openocd_h755_clean.cfg" \
    -c "init; reset halt; stm32h7x mass_erase 0; stm32h7x mass_erase 1; program $ELF verify; reset halt; shutdown"
else
  echo "[INFO] --no-flash selected; build complete only"
fi

cat <<EOF

Manual validation:
  Terminal 1:
    openocd -s /usr/share/openocd/scripts -f "$ROOT_DIR/scripts/openocd_h755.cfg" -c "init; reset halt"

  Terminal 2:
    gdb-multiarch -q "$ELF" -batch -x "$ROOT_DIR/scripts/gdb/preemption.dbg"
EOF
