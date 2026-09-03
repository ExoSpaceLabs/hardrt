#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOCAL_ROOT="$ROOT_DIR/.qualification/stm32"
OPENOCD_SCRIPTS="/usr/share/openocd/scripts"
DEBUG_TIMEOUT=90
GDB_BIN=""
OPENOCD_PID=""
STM32CUBE_H7_ROOT=""
EXTENDED_ONLY=0
BASE_REPORT=""
ARGS=()

usage() {
  cat <<'USAGE'
Usage:
  scripts/stm32_qualification.sh /path/to/STM32CubeH7 [base-runner options]
  scripts/stm32_qualification.sh /path/to/STM32CubeH7 --extended-only --base-report /path/to/qualification.md

Preferred full hardware qualification entry point. It:
- forces development evidence under gitignored .qualification/stm32/;
- runs the existing nine-case STM32 base matrix;
- adds semaphore, queue, mutex, and external-tick Cortex-M validators;
- prints a compact per-case/timing summary;
- appends a full-matrix PASS/FAIL marker required by release promotion.

Options added by this wrapper:
  --extended-only          Run only semaphore, queue, mutex and external-tick validators.
  --base-report FILE       With --extended-only, append the four cases to an existing
                           matching 9-case report and print the combined 13-case summary.

Do not pass --output-dir; development evidence is intentionally local when the full
wrapper owns the run. The base runner remains a nine-case helper, not a complete
hardware qualification by itself.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --extended-only) EXTENDED_ONLY=1; shift;;
    --base-report) BASE_REPORT="$2"; shift 2;;
    --output-dir) echo "--output-dir is managed by stm32_qualification.sh" >&2; exit 2;;
    --stm32h7-root)
      STM32CUBE_H7_ROOT="$2"
      ARGS+=("$1" "$2")
      shift 2
      ;;
    --debug-timeout)
      DEBUG_TIMEOUT="$2"
      ARGS+=("$1" "$2")
      shift 2
      ;;
    --openocd-scripts)
      OPENOCD_SCRIPTS="$2"
      ARGS+=("$1" "$2")
      shift 2
      ;;
    --timing-samples|--observe-seconds)
      ARGS+=("$1" "$2")
      shift 2
      ;;
    --clean-builds|--no-clean-builds)
      ARGS+=("$1")
      shift
      ;;
    -h|--help) usage; exit 0;;
    --*) echo "Unknown option: $1" >&2; usage >&2; exit 2;;
    *)
      if [[ -z "$STM32CUBE_H7_ROOT" ]]; then
        STM32CUBE_H7_ROOT="$1"
      fi
      ARGS+=("$1")
      shift
      ;;
  esac
done

[[ -n "$STM32CUBE_H7_ROOT" ]] || { usage >&2; exit 2; }
STM32CUBE_H7_ROOT="$(cd "$STM32CUBE_H7_ROOT" 2>/dev/null && pwd)" || {
  echo "Invalid STM32CubeH7 root" >&2
  exit 2
}
export STM32CUBE_H7_ROOT

if (( EXTENDED_ONLY == 0 )) && [[ -n "$BASE_REPORT" ]]; then
  echo "--base-report is only valid with --extended-only" >&2
  exit 2
fi

if command -v gdb-multiarch >/dev/null 2>&1; then GDB_BIN=gdb-multiarch
elif command -v arm-none-eabi-gdb >/dev/null 2>&1; then GDB_BIN=arm-none-eabi-gdb
else echo "Install gdb-multiarch or arm-none-eabi-gdb" >&2; exit 2; fi

mkdir -p "$LOCAL_ROOT"

echo "LED visual acceptance is qualitative: confirm that both LEDs toggle and that"
echo "the configured relative speed is obvious. Exact millisecond periods are not"
echo "a human acceptance criterion; automated counters and DWT timing cover that."
echo

HEAD_SHA="$(git -C "$ROOT_DIR" rev-parse HEAD)"
SHORT_SHA="$(git -C "$ROOT_DIR" rev-parse --short=8 HEAD)"

if (( EXTENDED_ONLY == 0 )); then
  "$ROOT_DIR/scripts/stm32_manual_test_full.sh" "${ARGS[@]}" --output-dir "$LOCAL_ROOT"

  RUN_DIR="$(find "$LOCAL_ROOT" -mindepth 1 -maxdepth 1 -type d -printf '%T@ %p\n' | sort -nr | head -n1 | cut -d' ' -f2-)"
  [[ -n "$RUN_DIR" && -f "$RUN_DIR/qualification.md" ]] || { echo "Could not locate base qualification output" >&2; exit 1; }
  REPORT="$RUN_DIR/qualification.md"
  RAW="$RUN_DIR/raw"
else
  if [[ -n "$BASE_REPORT" ]]; then
    BASE_REPORT="$(cd "$(dirname "$BASE_REPORT")" && pwd)/$(basename "$BASE_REPORT")"
    [[ -f "$BASE_REPORT" ]] || { echo "Base report not found: $BASE_REPORT" >&2; exit 2; }
    grep -q '^## Extended Cortex-M hardware results$' "$BASE_REPORT" && {
      echo "Base report already contains extended Cortex-M results" >&2
      exit 2
    }
    REPORT_SHA="$(sed -n 's/^- HardRT SHA: `\([^`]*\)`$/\1/p' "$BASE_REPORT" | head -n1)"
    [[ "$REPORT_SHA" == "$HEAD_SHA" ]] || {
      echo "Base report SHA $REPORT_SHA does not match current HEAD $HEAD_SHA" >&2
      exit 2
    }
    BASE_PASS="$(awk -F'|' '/^\|/ {r=$3; gsub(/[ *]/,"",r); if (r=="PASS") p++} END {print p+0}' "$BASE_REPORT")"
    BASE_FAIL="$(awk -F'|' '/^\|/ {r=$3; gsub(/[ *]/,"",r); if (r=="FAIL") f++} END {print f+0}' "$BASE_REPORT")"
    if (( BASE_PASS != 9 || BASE_FAIL != 0 )); then
      echo "--base-report must be a passing 9-case base report; got ${BASE_PASS} PASS / ${BASE_FAIL} FAIL" >&2
      exit 2
    fi
    RUN_DIR="$(dirname "$BASE_REPORT")"
    REPORT="$BASE_REPORT"
    RAW="$RUN_DIR/raw"
    mkdir -p "$RAW"
    echo "Extending existing 9-case report at: $REPORT"
  else
    STAMP="$(date -u +'%Y%m%dT%H%M%SZ')"
    RUN_DIR="$LOCAL_ROOT/${STAMP}_${SHORT_SHA}_extended"
    RAW="$RUN_DIR/raw"
    REPORT="$RUN_DIR/qualification.md"
    mkdir -p "$RAW"
    cat > "$REPORT" <<EOF
# HardRT STM32H755 Extended Qualification Report

- Run ID: \`${STAMP}_${SHORT_SHA}_extended\`
- HardRT SHA: \`$HEAD_SHA\`
- HardRT tracked source state: **$(git -C "$ROOT_DIR" status --porcelain --untracked-files=no | grep -q . && echo DIRTY || echo clean)**
- Board: \`NUCLEO-H755ZI-Q\`
- STM32CubeH7 SHA/state: \`$(git -C "$STM32CUBE_H7_ROOT" rev-parse HEAD 2>/dev/null || echo unknown)\` / \`$(git -C "$STM32CUBE_H7_ROOT" status --porcelain --untracked-files=no 2>/dev/null | grep -q . && echo DIRTY || echo clean)\`

This run contains only the four extended Cortex-M hardware cases. It is not a substitute
for the complete 13-case release-candidate qualification.

EOF
  fi
fi

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

run_gdb_case() {
  local elf="$1" script="$2" prefix="$3"
  local olog="$RAW/${prefix}_openocd.log" glog="$RAW/${prefix}_gdb.log"
  start_openocd "$olog" || return 1
  set +e
  timeout "${DEBUG_TIMEOUT}s" "$GDB_BIN" -q "$elf" -batch -x "$script" 2>&1 | tee "$glog"
  local rc=${PIPESTATUS[0]}
  set -e
  cleanup_openocd
  (( rc == 0 )) && grep -q '^RESULT: PASS$' "$glog"
}

run_ipc_case() {
  local case="$1" title="$2" prefix="ipc_$1"
  local elf="$ROOT_DIR/examples/hardrt_h755_ipc_validation/build-cortex_m/hardrt_h755_ipc_validation.elf"
  local blog="$RAW/${prefix}_build_flash.log"

  echo; echo "========== $title =========="
  set +e
  "$ROOT_DIR/scripts/build-lib-stm32h7xx-ipc-validation.sh" --case "$case" 2>&1 | tee "$blog"
  local rc=${PIPESTATUS[0]}
  set -e
  (( rc == 0 )) || return 1
  run_gdb_case "$elf" "$ROOT_DIR/scripts/gdb/ipc_validation.dbg" "$prefix"
}

run_external_tick_case() {
  local prefix="external_tick" title="External tick hardware contract"
  local elf="$ROOT_DIR/examples/hardrt_h755_external_tick/build-cortex_m/hardrt_h755_external_tick.elf"
  local blog="$RAW/${prefix}_build_flash.log"

  echo; echo "========== $title =========="
  set +e
  "$ROOT_DIR/scripts/build-lib-stm32h7xx-external-tick.sh" 2>&1 | tee "$blog"
  local rc=${PIPESTATUS[0]}
  set -e
  (( rc == 0 )) || return 1
  run_gdb_case "$elf" "$ROOT_DIR/scripts/gdb/external_tick_validation.dbg" "$prefix"
}

EXT_PASS=0
EXT_FAIL=0
declare -a EXT_NAMES=(
  "Semaphore hardware contract"
  "Queue hardware contract"
  "Mutex hardware contract"
  "External tick hardware contract"
)
declare -a EXT_KEYS=(semaphore queue mutex external_tick)
declare -a EXT_STATUS=()

for case in semaphore queue mutex; do
  index=${#EXT_STATUS[@]}
  if run_ipc_case "$case" "${EXT_NAMES[$index]}"; then
    EXT_STATUS+=(PASS); ((EXT_PASS+=1))
  else
    EXT_STATUS+=(FAIL); ((EXT_FAIL+=1))
  fi
done

if run_external_tick_case; then
  EXT_STATUS+=(PASS); ((EXT_PASS+=1))
else
  EXT_STATUS+=(FAIL); ((EXT_FAIL+=1))
fi

{
  echo; echo "## Extended Cortex-M hardware results"; echo
  echo "| Test | Result | Evidence |"; echo "|---|:---:|---|"
  for i in 0 1 2 3; do
    printf '| %s | **%s** | `raw/%s_*.log` |\n' "${EXT_NAMES[$i]}" "${EXT_STATUS[$i]}" "${EXT_KEYS[$i]}"
  done
  echo; echo "### Coverage"; echo
  echo "- Semaphore: counting/saturation checks plus real TIM2 ISR wake of a blocked higher-priority waiter and priority-aware \`need_switch\`."
  echo "- Queue: FIFO/full/empty checks, real TIM2 ISR send -> blocked receiver, and ISR receive -> blocked sender, including payload integrity and priority handoff."
  echo "- Mutex: owner tracking, blocking, direct handoff, and immediate higher-priority execution after lower-priority unlock."
  echo "- External tick: SysTick remains disabled; periodic TIM2 interrupts drive \`hrt_tick_from_isr()\`, sleeping high-priority work wakes at the requested tick count, and preempts a running lower-priority task."
  echo; echo "## Full qualification verdict"; echo
  echo "- Extended Cortex-M passed: **$EXT_PASS**"
  echo "- Extended Cortex-M failed: **$EXT_FAIL**"
  if (( EXTENDED_ONLY == 1 )) && [[ -z "$BASE_REPORT" ]]; then
    (( EXT_FAIL == 0 )) && echo "- Extended-only matrix overall: **PASS**" || echo "- Extended-only matrix overall: **FAIL**"
    echo "- Complete 13-case matrix: **NOT EVALUATED**"
  else
    (( EXT_FAIL == 0 )) && echo "- Full matrix overall: **PASS**" || echo "- Full matrix overall: **FAIL**"
  fi
} >> "$REPORT"

print_summary() {
  local expected=13
  if (( EXTENDED_ONLY == 1 )) && [[ -z "$BASE_REPORT" ]]; then expected=4; fi

  local pass fail run not_run
  pass="$(awk -F'|' '/^\|/ {r=$3; gsub(/[ *]/,"",r); if (r=="PASS") p++} END {print p+0}' "$REPORT")"
  fail="$(awk -F'|' '/^\|/ {r=$3; gsub(/[ *]/,"",r); if (r=="FAIL") f++} END {print f+0}' "$REPORT")"
  run=$((pass + fail))
  not_run=0
  (( run < expected )) && not_run=$((expected - run))

  echo
  echo "============================================================"
  echo "HardRT STM32 hardware qualification summary"
  echo "============================================================"
  echo "HardRT SHA : $HEAD_SHA"
  echo "Cube root  : $STM32CUBE_H7_ROOT"
  printf 'Cases      : %d/%d PASS' "$pass" "$expected"
  (( fail > 0 )) && printf ', %d FAIL' "$fail"
  (( not_run > 0 )) && printf ', %d NOT RUN' "$not_run"
  echo
  echo
  printf '%-44s %s\n' "CASE" "RESULT"
  printf '%-44s %s\n' "--------------------------------------------" "------"
  awk -F'|' '/^\|/ {
    name=$2; r=$3;
    gsub(/^[[:space:]]+|[[:space:]]+$/, "", name);
    gsub(/[ *]/, "", r);
    if (r=="PASS" || r=="FAIL") printf "%-44s %s\n", name, r;
  }' "$REPORT"

  if grep -q '^| DWT ' "$REPORT"; then
    echo
    echo "Timing (cycles, min / avg / max):"
    grep '^| DWT ' "$REPORT" | while IFS='|' read -r _ name _ _ _ notes _; do
      name="$(sed -E 's/^[[:space:]]+|[[:space:]]+$//g; s/^DWT //; s/ timing$//' <<< "$name")"
      min="$(sed -n 's/.*min=\([0-9][0-9]*\) cycles.*/\1/p' <<< "$notes")"
      avg="$(sed -n 's/.*avg=\([0-9][0-9]*\) cycles.*/\1/p' <<< "$notes")"
      max="$(sed -n 's/.*max=\([0-9][0-9]*\) cycles.*/\1/p' <<< "$notes")"
      [[ -n "$min" && -n "$avg" && -n "$max" ]] && printf '  %-24s %s / %s / %s\n' "$name" "$min" "$avg" "$max"
    done
  fi

  echo
  if (( fail == 0 && not_run == 0 )); then
    echo "Overall     : PASS (${pass}/${expected})"
  elif (( fail == 0 )); then
    echo "Overall     : PARTIAL (${pass}/${expected} passed; ${not_run} not run)"
  else
    echo "Overall     : FAIL (${fail} failed case(s))"
  fi
  echo "Report      : $REPORT"
  echo "Raw evidence: $RAW"
  echo "============================================================"
}

print_summary

if (( EXT_FAIL == 0 )); then
  if (( EXTENDED_ONLY == 1 )) && [[ -z "$BASE_REPORT" ]]; then
    echo "Extended STM32 qualification: PASS"
  else
    echo "Full STM32 qualification: PASS"
  fi
  echo "Development evidence remains local. Promote only the final release-candidate run."
else
  echo "STM32 qualification: FAIL ($EXT_FAIL extended case(s))"
  exit 1
fi
