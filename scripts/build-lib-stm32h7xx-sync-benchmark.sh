#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
APP_DIR="$ROOT_DIR/examples/hardrt_h755_sync_benchmark"
SAMPLES=1000
FLASH=1
while (( $# )); do
  case "$1" in
    --samples) SAMPLES="$2"; shift 2 ;;
    --no-flash) FLASH=0; shift ;;
    -h|--help) echo "Usage: $0 [--samples 1..100000] [--no-flash]"; exit 0 ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
done
[[ "$SAMPLES" =~ ^[1-9][0-9]*$ ]] && (( SAMPLES <= 100000 )) || {
  echo "--samples must be in 1..100000" >&2; exit 2;
}
"$ROOT_DIR/scripts/build-lib-stm32h7xx.sh" \
  --hardrt "$ROOT_DIR" --app "$APP_DIR" --build-type Release \
  --hardrt-cmake-arg "-DHARDRT_TIMING_PROFILE=none" \
  --hardrt-cmake-arg "-DHARDRT_CORTEXM_CRIT_PROFILE=ON" \
  --app-cmake-arg "-DHARDRT_BENCH_SAMPLES=$SAMPLES"
ELF="$APP_DIR/build-cortex_m/hardrt_h755_sync_benchmark.elf"
[[ -s "$ELF" ]] || { echo "Missing benchmark ELF: $ELF" >&2; exit 1; }
if (( FLASH )); then
  openocd -s /usr/share/openocd/scripts \
    -f "$ROOT_DIR/scripts/openocd_h755_clean.cfg" \
    -c "init; reset halt; stm32h7x mass_erase 0; stm32h7x mass_erase 1; program $ELF verify; reset halt; shutdown"
fi
echo "ELF=$ELF"
echo "NOTE: diagnostic masked-window profile is NOT production WCET"
