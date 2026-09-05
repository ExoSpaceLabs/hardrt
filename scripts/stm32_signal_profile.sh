#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STM32CUBE_H7_ROOT=""
OUTPUT_BASE="$ROOT_DIR/validation/stm32"
TIMING_SAMPLES=10000
DEBUG_TIMEOUT=90
OPENOCD_SCRIPTS="/usr/share/openocd/scripts"
GDB_BIN=""
OPENOCD_PID=""

TOTAL=16
NAMES=()
STATUSES=()
NOTES=()
EVIDENCE=()

usage() {
  cat <<'USAGE'
Usage:
  scripts/stm32_signal_profile.sh /path/to/STM32CubeH7 [options]
  scripts/stm32_signal_profile.sh --stm32h7-root /path/to/STM32CubeH7 [options]

Profiles the v0.5 event-flags and task-notification paths on STM32H755 CM7.
Every case is rebuilt and flashed as a separate Release image. Event scan cost
is measured with 1, 8, 16 and 32 actual waiters for no-match, one-match and
all-match scenarios.

Options:
  --stm32h7-root DIR      STM32CubeH7 checkout root.
  --output-dir DIR        Evidence root (default: validation/stm32).
  --timing-samples N      Samples per benchmark image (default: 10000).
  --debug-timeout N       GDB timeout per automated case (default: 90).
  --openocd-scripts DIR   OpenOCD scripts directory.
  -h, --help              Show help.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --stm32h7-root) STM32CUBE_H7_ROOT="$2"; shift 2 ;;
    --output-dir) OUTPUT_BASE="$2"; shift 2 ;;
    --timing-samples) TIMING_SAMPLES="$2"; shift 2 ;;
    --debug-timeout) DEBUG_TIMEOUT="$2"; shift 2 ;;
    --openocd-scripts) OPENOCD_SCRIPTS="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    --*) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    *)
      [[ -z "$STM32CUBE_H7_ROOT" ]] || { echo "Unexpected argument: $1" >&2; exit 2; }
      STM32CUBE_H7_ROOT="$1"; shift
      ;;
  esac
done

[[ -n "$STM32CUBE_H7_ROOT" ]] || { usage >&2; exit 2; }
STM32CUBE_H7_ROOT="$(cd "$STM32CUBE_H7_ROOT" 2>/dev/null && pwd)" || {
  echo "Invalid STM32CubeH7 root" >&2
  exit 2
}
export STM32CUBE_H7_ROOT

for n in "$TIMING_SAMPLES" "$DEBUG_TIMEOUT"; do
  [[ "$n" =~ ^[0-9]+$ ]] && (( n > 0 )) || {
    echo "Numeric options must be > 0" >&2
    exit 2
  }
done

need() { command -v "$1" >/dev/null 2>&1 || { echo "Missing command: $1" >&2; exit 2; }; }
for c in git cmake openocd arm-none-eabi-gcc timeout tee grep sed; do need "$c"; done
if command -v gdb-multiarch >/dev/null 2>&1; then GDB_BIN=gdb-multiarch
elif command -v arm-none-eabi-gdb >/dev/null 2>&1; then GDB_BIN=arm-none-eabi-gdb
else echo "Install gdb-multiarch or arm-none-eabi-gdb" >&2; exit 2
fi

for f in \
  "$STM32CUBE_H7_ROOT/Drivers/CMSIS/Core/Include/core_cm7.h" \
  "$STM32CUBE_H7_ROOT/Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h755xx.h"; do
  [[ -f "$f" ]] || { echo "Incomplete STM32CubeH7 checkout: missing $f" >&2; exit 2; }
done

cd "$ROOT_DIR"
SHA="$(git rev-parse HEAD)"
SHORT_SHA="$(git rev-parse --short=8 HEAD)"
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
TRACKED_STATUS="$(git status --porcelain --untracked-files=no)"
SOURCE_WORKTREE=clean; [[ -n "$TRACKED_STATUS" ]] && SOURCE_WORKTREE=DIRTY
STAMP="$(date -u +'%Y%m%dT%H%M%SZ')"
RUN_DIR="$OUTPUT_BASE/${STAMP}_${SHORT_SHA}_signals"
RAW="$RUN_DIR/raw"
REPORT="$RUN_DIR/profiling.md"
mkdir -p "$RAW"

cleanup_openocd() {
  if [[ -n "${OPENOCD_PID:-}" ]] && kill -0 "$OPENOCD_PID" >/dev/null 2>&1; then
    kill "$OPENOCD_PID" >/dev/null 2>&1 || true
    wait "$OPENOCD_PID" >/dev/null 2>&1 || true
  fi
  OPENOCD_PID=""
}
trap cleanup_openocd EXIT INT TERM

start_openocd() {
  local log="$1"
  cleanup_openocd
  openocd -s "$OPENOCD_SCRIPTS" -f "$ROOT_DIR/scripts/openocd_h755.cfg" \
    -c "init; reset halt" >"$log" 2>&1 &
  OPENOCD_PID=$!
  for ((i=0; i<100; ++i)); do
    grep -q "Listening on port 3333 for gdb connections" "$log" 2>/dev/null && return 0
    kill -0 "$OPENOCD_PID" >/dev/null 2>&1 || {
      cat "$log" >&2
      OPENOCD_PID=""
      return 1
    }
    sleep 0.1
  done
  echo "OpenOCD GDB server timeout" >&2
  cleanup_openocd
  return 1
}

run_gdb() {
  local elf="$1" prefix="$2"
  local olog="$RAW/${prefix}_openocd.log" glog="$RAW/${prefix}_gdb.log"
  [[ -s "$elf" ]] || return 1
  start_openocd "$olog" || return 1
  local rc
  set +e
  timeout "${DEBUG_TIMEOUT}s" "$GDB_BIN" -q "$elf" -batch \
    -x "$ROOT_DIR/scripts/gdb/timing.dbg" 2>&1 | tee "$glog"
  rc=${PIPESTATUS[0]}
  set -e
  cleanup_openocd
  return "$rc"
}

record_case() {
  local case="$1" waiters="$2"
  local suffix=""
  [[ "$case" == event_scan_* ]] && suffix="_waiters${waiters}"
  local label="${case}${suffix}"
  local prefix="signal_${label}"
  local elf="$ROOT_DIR/examples/hardrt_h755_dwt_timing/build-cortex_m/hardrt_cm7_dwt_timing.elf"
  local glog="$RAW/${prefix}_gdb.log"
  local status=PASS notes=""

  echo
  echo "========== DWT ${label} timing =========="
  set +e
  "$ROOT_DIR/scripts/build-lib-stm32h7xx-dwt-timing.sh" \
    --case "$case" --waiters "$waiters" --samples "$TIMING_SAMPLES" \
    2>&1 | tee "$RAW/${prefix}_build_flash.log"
  local build_rc=${PIPESTATUS[0]}
  set -e

  if (( build_rc != 0 )); then
    status=FAIL
    notes="Build/flash failed."
  elif ! run_gdb "$elf" "$prefix"; then
    status=FAIL
    notes="Timing GDB run failed or timed out."
  else
    local count target min avg max signal_waiters expected observed error
    count="$(sed -n 's/^count=\([0-9][0-9]*\)$/\1/p' "$glog" | tail -n1)"
    target="$(sed -n 's/^event_hz=[0-9][0-9]* target_samples=\([0-9][0-9]*\)$/\1/p' "$glog" | tail -n1)"
    read -r min avg max < <(sed -n 's/^min=\([0-9][0-9]*\) cycles, avg=\([0-9][0-9]*\) cycles (sum\/count=[0-9][0-9]*), max=\([0-9][0-9]*\) cycles$/\1 \2 \3/p' "$glog" | tail -n1) || true
    read -r signal_waiters expected observed < <(sed -n 's/^signal_waiters=\([0-9][0-9]*\) expected_wakes_per_sample=\([0-9][0-9]*\) observed_wakes=\([0-9][0-9]*\)$/\1 \2 \3/p' "$glog" | tail -n1) || true
    error="$(sed -n 's/^error=\([0-9][0-9]*\)$/\1/p' "$glog" | tail -n1)"

    if ! grep -q '^RESULT: PASS$' "$glog" || \
       [[ -z "$count" || -z "$target" || -z "${min:-}" || -z "${avg:-}" || -z "${max:-}" || -z "${signal_waiters:-}" || -z "${expected:-}" || -z "${observed:-}" || -z "$error" ]] || \
       (( count != TIMING_SAMPLES || target != TIMING_SAMPLES || min > avg || avg > max || error != 0 )); then
      status=FAIL
      notes="Automated signal profiling acceptance failed."
    elif [[ "$case" == event_scan_* ]] && (( signal_waiters != waiters )); then
      status=FAIL
      notes="Firmware waiter count did not match requested event scan load."
    else
      notes="waiters=$signal_waiters, expected_wakes/sample=$expected, observed_wakes=$observed, count=$count, min=$min cycles, avg=$avg cycles, max=$max cycles."
    fi
  fi

  NAMES+=("$label")
  STATUSES+=("$status")
  NOTES+=("$notes")
  EVIDENCE+=("raw/${prefix}_*.log")
}

cat > "$REPORT" <<EOF
# HardRT STM32H755 Event / Notification Profiling

- Run ID: \`${STAMP}_${SHORT_SHA}_signals\`
- UTC start: \`$(date -u --iso-8601=seconds)\`
- HardRT branch: \`$BRANCH\`
- HardRT SHA: \`$SHA\`
- Tracked source state: **$SOURCE_WORKTREE**
- Board/core: \`NUCLEO-H755ZI-Q / CM7\`
- Timing samples per image: \`$TIMING_SAMPLES\`
- Profile cases: **$TOTAL**

All cases use direct DWT timestamps with the production event/notification code
and `HARDRT_TIMING_PROFILE=none`. The event scan matrix varies actual registered
waiters, not merely the compile-time task capacity.

EOF

# Composite ISR-to-task latency.
record_case event_isr_to_task 1
record_case notify_isr_to_task 1

# Event-set operation cost versus waiter count and wake fan-out.
for waiters in 1 8 16 32; do
  record_case event_scan_none "$waiters"
  record_case event_scan_one "$waiters"
  record_case event_scan_all "$waiters"
done

# O(1) notification operation cost with and without a READY transition.
record_case notify_isr_no_wake 1
record_case notify_isr_wake 1

PASS=0
FAIL=0
{
  echo "## Results"
  echo
  echo "| Case | Result | Evidence | Timing / validation |"
  echo "|---|:---:|---|---|"
  for i in "${!NAMES[@]}"; do
    [[ "${STATUSES[$i]}" == PASS ]] && ((PASS+=1)) || ((FAIL+=1))
    note="${NOTES[$i]//|/\\|}"
    printf '| `%s` | **%s** | `%s` | %s |\n' \
      "${NAMES[$i]}" "${STATUSES[$i]}" "${EVIDENCE[$i]}" "$note"
  done
  echo
  echo "## Verdict"
  echo
  echo "- Passed: **$PASS / $TOTAL**"
  echo "- Failed: **$FAIL**"
  if (( FAIL == 0 && PASS == TOTAL )); then
    echo "- Overall: **PASS**"
  else
    echo "- Overall: **FAIL**"
  fi
  echo
  echo "- UTC end: \`$(date -u --iso-8601=seconds)\`"
} >> "$REPORT"

echo
echo "============================================================"
echo "HardRT event / notification profiling summary"
echo "============================================================"
echo "HardRT SHA : $SHA"
printf 'Profiles    : %d/%d PASS, %d FAIL\n' "$PASS" "$TOTAL" "$FAIL"
for i in "${!NAMES[@]}"; do
  printf '  %-38s %s  %s\n' "${NAMES[$i]}" "${STATUSES[$i]}" "${NOTES[$i]}"
done
echo "Report      : $REPORT"
echo "Raw logs    : $RAW"
echo "============================================================"

(( FAIL == 0 && PASS == TOTAL ))
