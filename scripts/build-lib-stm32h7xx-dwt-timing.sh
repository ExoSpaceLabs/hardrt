#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="$ROOT_DIR/examples/hardrt_h755_dwt_timing"
CASE="event_to_task"
EVENT_HZ="1000"
SAMPLES="10000"
DO_FLASH=1

usage() {
  cat <<'USAGE'
Usage: scripts/build-lib-stm32h7xx-dwt-timing.sh [options]

Options:
  --case event_to_task|sem_isr_ready|ready_to_task|scheduler_decision
  --event-hz HZ
  --samples N
  --no-flash
  -h, --help

Cases:
  event_to_task      Direct DWT timestamps only. HardRT timing profile is `none`.
                     Measures the composite interval from a software point in
                     TIM2 ISR to the latency task continuing after semaphore wake.

  sem_isr_ready      Builds HardRT with only the private `ipc` timing profile.
                     Measures hrt_sem_give_from_isr entry -> waiter marked READY.

  ready_to_task      Builds HardRT with the private `ipc` timing profile and a
                     waiter-READY start marker. Measures waiter marked READY ->
                     latency task continuation after the blocked semaphore returns.

  scheduler_decision Diagnostic-only PendSV timing image. Measures the direct
                     hrt__schedule() call plus outgoing save, incoming restore,
                     PendSV software span, and PendSV-entry -> task continuation.
                     Production HardRT kernel/scheduler code is unchanged.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --case) CASE="$2"; shift 2;;
    --event-hz) EVENT_HZ="$2"; shift 2;;
    --samples) SAMPLES="$2"; shift 2;;
    --no-flash) DO_FLASH=0; shift;;
    -h|--help) usage; exit 0;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 1;;
  esac
done

HARD_RT_ARGS=()
case "$CASE" in
  event_to_task|scheduler_decision)
    HARD_RT_ARGS+=(--hardrt-cmake-arg "-DHARDRT_TIMING_PROFILE=none")
    ;;
  sem_isr_ready)
    HARD_RT_ARGS+=(--hardrt-cmake-arg "-DHARDRT_TIMING_PROFILE=ipc")
    HARD_RT_ARGS+=(--hardrt-cmake-arg "-DHARDRT_TIMING_HOOK_HEADER=$APP_DIR/inc/hardrt_timing_hooks.h")
    ;;
  ready_to_task)
    HARD_RT_ARGS+=(--hardrt-cmake-arg "-DHARDRT_TIMING_PROFILE=ipc")
    HARD_RT_ARGS+=(--hardrt-cmake-arg "-DHARDRT_TIMING_HOOK_HEADER=$APP_DIR/inc/hardrt_timing_ready_hooks.h")
    ;;
  *)
    echo "Unsupported timing case: $CASE" >&2
    usage >&2
    exit 1
    ;;
esac

APP_ARGS=(
  --app-cmake-arg "-DHARDRT_TIMING_CASE=$CASE"
  --app-cmake-arg "-DHARDRT_TIMING_EVENT_HZ=$EVENT_HZ"
  --app-cmake-arg "-DHARDRT_TIMING_TARGET_SAMPLES=$SAMPLES"
)

echo "[INFO] Timing case    : $CASE"
echo "[INFO] Event rate     : $EVENT_HZ Hz"
echo "[INFO] Target samples : $SAMPLES"

"$ROOT_DIR/scripts/build-lib-stm32h7xx.sh" \
  --hardrt "$ROOT_DIR" \
  --app "$APP_DIR" \
  --build-type Release \
  "${HARD_RT_ARGS[@]}" \
  "${APP_ARGS[@]}"

ELF="$APP_DIR/build-cortex_m/hardrt_cm7_dwt_timing.elf"
[[ -f "$ELF" ]] || { echo "[FAIL] timing ELF not found: $ELF" >&2; exit 1; }

if (( DO_FLASH )); then
  echo "[INFO] Flashing $ELF"
  openocd -s /usr/share/openocd/scripts \
    -f "$ROOT_DIR/scripts/openocd_h755_clean.cfg" \
    -c "init; reset halt; stm32h7x mass_erase 0; stm32h7x mass_erase 1; program $ELF verify; reset halt; shutdown"
else
  echo "[INFO] --no-flash selected; build complete only"
fi

cat <<EOF

Manual timing run:
  Terminal 1:
    openocd -s /usr/share/openocd/scripts -f "$ROOT_DIR/scripts/openocd_h755.cfg" -c "init; reset halt"

  Terminal 2:
    gdb-multiarch -q "$ELF" -batch -x "$ROOT_DIR/scripts/gdb/timing.dbg"

The GDB script reads one result set only; no unrelated timing case runs in this image.
EOF