#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="$ROOT_DIR/examples/hardrt_h755_tick_benchmark"
SCENARIO="none"
TASKS="8"
EVENT_HZ="1000"
SAMPLES="10000"
DO_FLASH=1

usage() {
  cat <<'USAGE'
Usage: scripts/build-lib-stm32h7xx-tick-benchmark.sh [options]

Options:
  --scenario none|one_sleep|all_sleep|one_expiry|simultaneous|staggered
  --tasks 8|16|32
  --event-hz HZ
  --samples N
  --no-flash
  -h, --help

Scenarios:
  none          All worker task slots are occupied by semaphore-blocked tasks.
                Measures the unconditional task-table scan with no sleepers.
  one_sleep     One worker sleeps beyond the measurement window; the rest block.
  all_sleep     All workers sleep beyond the measurement window.
  one_expiry    One worker expires every tick and immediately sleeps for 1 tick.
  simultaneous  All workers expire together every tick and sleep again for 1 tick.
  staggered     Worker i sleeps for i+1 ticks, producing distributed expiries.

The HardRT library is rebuilt for the selected application-task capacity so the
measured O(HARDRT_MAX_TASKS) scan uses the real configured bound.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --scenario) SCENARIO="$2"; shift 2;;
    --tasks) TASKS="$2"; shift 2;;
    --event-hz) EVENT_HZ="$2"; shift 2;;
    --samples) SAMPLES="$2"; shift 2;;
    --no-flash) DO_FLASH=0; shift;;
    -h|--help) usage; exit 0;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 1;;
  esac
done

case "$SCENARIO" in
  none|one_sleep|all_sleep|one_expiry|simultaneous|staggered) ;;
  *) echo "Unsupported tick benchmark scenario: $SCENARIO" >&2; usage >&2; exit 1;;
esac
case "$TASKS" in
  8|16|32) ;;
  *) echo "Unsupported task capacity: $TASKS (expected 8, 16, or 32)" >&2; exit 1;;
esac
[[ "$EVENT_HZ" =~ ^[0-9]+$ ]] && (( EVENT_HZ > 0 )) || { echo "--event-hz must be > 0" >&2; exit 1; }
[[ "$SAMPLES" =~ ^[0-9]+$ ]] && (( SAMPLES > 0 )) || { echo "--samples must be > 0" >&2; exit 1; }

echo "[INFO] Tick scenario       : $SCENARIO"
echo "[INFO] App task capacity   : $TASKS"
echo "[INFO] External tick       : $EVENT_HZ Hz"
echo "[INFO] Target samples      : $SAMPLES"

"$ROOT_DIR/scripts/build-lib-stm32h7xx.sh" \
  --hardrt "$ROOT_DIR" \
  --app "$APP_DIR" \
  --build-type Release \
  --hardrt-cmake-arg "-DHARDRT_TIMING_PROFILE=none" \
  --hardrt-cmake-arg "-DHARDRT_CFG_MAX_TASKS=$TASKS" \
  --app-cmake-arg "-DHARDRT_TICK_BENCH_SCENARIO=$SCENARIO" \
  --app-cmake-arg "-DHARDRT_TICK_BENCH_EVENT_HZ=$EVENT_HZ" \
  --app-cmake-arg "-DHARDRT_TICK_BENCH_TARGET_SAMPLES=$SAMPLES"

ELF="$APP_DIR/build-cortex_m/hardrt_h755_tick_benchmark.elf"
[[ -s "$ELF" ]] || { echo "[FAIL] tick benchmark ELF not found: $ELF" >&2; exit 1; }

if (( DO_FLASH )); then
  openocd -s /usr/share/openocd/scripts \
    -f "$ROOT_DIR/scripts/openocd_h755_clean.cfg" \
    -c "init; reset halt; stm32h7x mass_erase 0; stm32h7x mass_erase 1; program $ELF verify; reset halt; shutdown"
else
  echo "[INFO] --no-flash selected; build complete only"
fi

cat <<EOF

Manual benchmark:
  Terminal 1:
    openocd -s /usr/share/openocd/scripts -f "$ROOT_DIR/scripts/openocd_h755.cfg" -c "init; reset halt"

  Terminal 2:
    gdb-multiarch -q "$ELF" -batch -x "$ROOT_DIR/scripts/gdb/tick_benchmark.dbg"
EOF
