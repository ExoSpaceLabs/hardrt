#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOCAL_ROOT="$ROOT_DIR/.qualification/stm32"
OPENOCD_SCRIPTS="/usr/share/openocd/scripts"
DEBUG_TIMEOUT=90
GDB_BIN=""
OPENOCD_PID=""
ARGS=()

usage() {
  cat <<'USAGE'
Usage:
  scripts/stm32_qualification.sh /path/to/STM32CubeH7 [base-runner options]

This is the preferred full hardware qualification entry point. It:
- forces development evidence under gitignored .qualification/stm32/;
- runs the existing base STM32 matrix;
- adds semaphore, queue, and mutex Cortex-M contract validators;
- appends a full-matrix PASS/FAIL marker required by release promotion.

Do not pass --output-dir; development evidence is intentionally local.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --output-dir) echo "--output-dir is managed by stm32_qualification.sh" >&2; exit 2;;
    --debug-timeout) DEBUG_TIMEOUT="$2"; ARGS+=("$1" "$2"); shift 2;;
    --openocd-scripts) OPENOCD_SCRIPTS="$2"; ARGS+=("$1" "$2"); shift 2;;
    -h|--help) usage; exit 0;;
    *) ARGS+=("$1"); shift;;
  esac
done

if command -v gdb-multiarch >/dev/null 2>&1; then GDB_BIN=gdb-multiarch
elif command -v arm-none-eabi-gdb >/dev/null 2>&1; then GDB_BIN=arm-none-eabi-gdb
else echo "Install gdb-multiarch or arm-none-eabi-gdb" >&2; exit 2; fi

mkdir -p "$LOCAL_ROOT"

echo "LED visual acceptance is qualitative: confirm that both LEDs toggle and that"
echo "the configured relative speed is obvious. You are not expected to distinguish"
echo "exact millisecond periods by eye; automated counters/timing cover that."
echo

"$ROOT_DIR/scripts/stm32_manual_test_full.sh" "${ARGS[@]}" --output-dir "$LOCAL_ROOT"

RUN_DIR="$(find "$LOCAL_ROOT" -mindepth 1 -maxdepth 1 -type d -printf '%T@ %p\n' | sort -nr | head -n1 | cut -d' ' -f2-)"
[[ -n "$RUN_DIR" && -f "$RUN_DIR/qualification.md" ]] || { echo "Could not locate base qualification output" >&2; exit 1; }
RAW="$RUN_DIR/raw"
REPORT="$RUN_DIR/qualification.md"

cleanup_openocd() {
  if [[ -n "${OPENOCD_PID:-}" ]] && kill -0 "$OPENOCD_PID" >/dev/null 2>&1; then
    kill "$OPENOCD_PID" >/dev/null 2>&1 || true
    wait "$OPENOCD_PID" >/dev/null 2>&1 || true
  fi
  OPENOCD_PID=""
}
trap cleanup_openocd EXIT INT TERM

start_openocd() {
  local log="$1"; cleanup_openocd
  openocd -s "$OPENOCD_SCRIPTS" -f "$ROOT_DIR/scripts/openocd_h755.cfg" -c "init; reset halt" >"$log" 2>&1 &
  OPENOCD_PID=$!
  for ((i=0; i<100; ++i)); do
    grep -q "Listening on port 3333 for gdb connections" "$log" 2>/dev/null && return 0
    kill -0 "$OPENOCD_PID" >/dev/null 2>&1 || { cat "$log" >&2; OPENOCD_PID=""; return 1; }
    sleep 0.1
  done
  cleanup_openocd
  return 1
}

run_ipc_case() {
  local case="$1" title="$2" prefix="ipc_$1"
  local elf="$ROOT_DIR/examples/hardrt_h755_ipc_validation/build-cortex_m/hardrt_h755_ipc_validation.elf"
  local blog="$RAW/${prefix}_build_flash.log" olog="$RAW/${prefix}_openocd.log" glog="$RAW/${prefix}_gdb.log"

  echo; echo "========== $title =========="
  set +e
  "$ROOT_DIR/scripts/build-lib-stm32h7xx-ipc-validation.sh" --case "$case" 2>&1 | tee "$blog"
  local rc=${PIPESTATUS[0]}
  set -e
  (( rc == 0 )) || return 1

  start_openocd "$olog" || return 1
  set +e
  timeout "${DEBUG_TIMEOUT}s" "$GDB_BIN" -q "$elf" -batch -x "$ROOT_DIR/scripts/gdb/ipc_validation.dbg" 2>&1 | tee "$glog"
  rc=${PIPESTATUS[0]}
  set -e
  cleanup_openocd
  (( rc == 0 )) && grep -q '^RESULT: PASS$' "$glog"
}

IPC_PASS=0
IPC_FAIL=0
declare -a IPC_NAMES=("Semaphore hardware contract" "Queue hardware contract" "Mutex hardware contract")
declare -a IPC_CASES=(semaphore queue mutex)
declare -a IPC_STATUS=()

for i in 0 1 2; do
  if run_ipc_case "${IPC_CASES[$i]}" "${IPC_NAMES[$i]}"; then
    IPC_STATUS+=(PASS); ((IPC_PASS+=1))
  else
    IPC_STATUS+=(FAIL); ((IPC_FAIL+=1))
  fi
done

{
  echo; echo "## Extended IPC hardware results"; echo
  echo "| Test | Result | Evidence |"; echo "|---|:---:|---|"
  for i in 0 1 2; do
    printf '| %s | **%s** | `raw/ipc_%s_*.log` |\n' "${IPC_NAMES[$i]}" "${IPC_STATUS[$i]}" "${IPC_CASES[$i]}"
  done
  echo; echo "### Coverage"
  echo
  echo "- Semaphore: counting/saturation checks plus real TIM2 ISR wake of a blocked higher-priority waiter and priority-aware \`need_switch\`."
  echo "- Queue: FIFO/full/empty checks, real TIM2 ISR send -> blocked receiver, and ISR receive -> blocked sender, including payload integrity and priority handoff."
  echo "- Mutex: owner tracking, blocking, direct handoff, and immediate higher-priority execution after lower-priority unlock."
  echo; echo "## Full qualification verdict"; echo
  echo "- IPC hardware passed: **$IPC_PASS**"
  echo "- IPC hardware failed: **$IPC_FAIL**"
  (( IPC_FAIL == 0 )) && echo "- Full matrix overall: **PASS**" || echo "- Full matrix overall: **FAIL**"
} >> "$REPORT"

echo
echo "Full qualification report: $REPORT"
if (( IPC_FAIL == 0 )); then
  echo "Full STM32 qualification: PASS"
  echo "Development evidence remains local. Promote only the final release-candidate run."
else
  echo "Full STM32 qualification: FAIL ($IPC_FAIL IPC case(s))"
  exit 1
fi
