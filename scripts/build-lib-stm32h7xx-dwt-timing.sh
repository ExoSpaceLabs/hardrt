#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="$ROOT_DIR/examples/hardrt_h755_dwt_timing"
CASE="event_to_task"
EVENT_HZ="1000"
SAMPLES="10000"
WAITERS="1"
DO_FLASH=1

usage() {
  cat <<'USAGE'
Usage: scripts/build-lib-stm32h7xx-dwt-timing.sh [options]

Options:
  --case CASE
  --event-hz HZ
  --samples N
  --waiters N
  --no-flash
  -h, --help

Cases:
  event_to_task       Existing semaphore-backed composite ISR -> task latency.
  sem_isr_ready       hrt_sem_give_from_isr entry -> waiter marked READY.
  ready_to_task       Waiter marked READY -> task continuation.
  scheduler_decision  Diagnostic PendSV scheduler/context-switch breakdown.

  event_isr_to_task   hrt_event_set_from_isr path plus dispatch to one waiter.
  notify_isr_to_task  hrt_task_notify_from_isr path plus dispatch to one waiter.

  event_scan_none     hrt_event_set_from_isr entry -> return, no waiter matches.
  event_scan_one      hrt_event_set_from_isr entry -> return, one waiter matches.
  event_scan_all      hrt_event_set_from_isr entry -> return, all waiters match.
                      Use --waiters 1|8|16|32 for the qualification matrix.

  notify_isr_no_wake  hrt_task_notify_from_isr entry -> return, target running.
  notify_isr_wake     hrt_task_notify_from_isr entry -> return, blocked waiter wakes.

All new event/notification cases use direct application-side DWT timestamps and
HARDRT_TIMING_PROFILE=none. Event scan builds size HARDRT_CFG_MAX_TASKS to
waiters + one controller task so the requested waiter count is actually present.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --case) CASE="$2"; shift 2;;
    --event-hz) EVENT_HZ="$2"; shift 2;;
    --samples) SAMPLES="$2"; shift 2;;
    --waiters) WAITERS="$2"; shift 2;;
    --no-flash) DO_FLASH=0; shift;;
    -h|--help) usage; exit 0;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 1;;
  esac
done

for value in "$EVENT_HZ" "$SAMPLES" "$WAITERS"; do
  [[ "$value" =~ ^[0-9]+$ ]] && (( value > 0 )) || {
    echo "event-hz, samples and waiters must be positive integers" >&2
    exit 1
  }
done
(( WAITERS <= 32 )) || { echo "--waiters must be <= 32" >&2; exit 1; }

HARD_RT_ARGS=()
case "$CASE" in
  event_to_task|scheduler_decision|event_isr_to_task|notify_isr_to_task|notify_isr_no_wake|notify_isr_wake)
    HARD_RT_ARGS+=(--hardrt-cmake-arg "-DHARDRT_TIMING_PROFILE=none")
    ;;
  event_scan_none|event_scan_one|event_scan_all)
    HARD_RT_ARGS+=(--hardrt-cmake-arg "-DHARDRT_TIMING_PROFILE=none")
    HARD_RT_ARGS+=(--hardrt-cmake-arg "-DHARDRT_CFG_MAX_TASKS=$((WAITERS + 1))")
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
  --app-cmake-arg "-DHARDRT_TIMING_SIGNAL_WAITERS=$WAITERS"
)

echo "[INFO] Timing case    : $CASE"
echo "[INFO] Event rate     : $EVENT_HZ Hz"
echo "[INFO] Target samples : $SAMPLES"
echo "[INFO] Signal waiters : $WAITERS"

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
