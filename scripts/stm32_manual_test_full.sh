#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STM32CUBE_H7_ROOT=""
OUTPUT_BASE="$ROOT_DIR/validation/stm32"
TIMING_SAMPLES=10000
OBSERVE_SECONDS=10
DEBUG_TIMEOUT=90
OPENOCD_SCRIPTS="/usr/share/openocd/scripts"
GDB_BIN=""
OPENOCD_PID=""
FINALIZED=0
CLEAN_BUILDS_MODE="ask"
ONLY_MODE=""
CLEANED_BUILD_DIRS=()

FUNCTIONAL_TOTAL=13
BENCHMARK_TOTAL=38

NAMES=()
STATUSES=()
CRITERIA=()
EVIDENCE=()
NOTES=()

usage() {
  cat <<'USAGE'
Usage:
  scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 [options]
  scripts/stm32_manual_test_full.sh --stm32h7-root /path/to/STM32CubeH7 [options]

Modes:
  no --only            Run ALL available tests: every functional hardware validation AND every hardware benchmark.
  --only functional    Run only functional hardware validation.
  --only benchmark     Run only every hardware benchmark, one image at a time.

The board probe is a prerequisite and is always run.

Development evidence is written under validation/stm32/.
Timestamped development runs are gitignored; release evidence remains trackable under:
  validation/stm32/releases/vX.Y.Z/

Options:
  --stm32h7-root DIR      STM32CubeH7 checkout root.
  --output-dir DIR        Evidence root (default: validation/stm32).
  --timing-samples N      Samples per benchmark image (default: 10000).
  --observe-seconds N     LED observation duration (default: 10).
  --debug-timeout N       GDB timeout per automated case (default: 90).
  --openocd-scripts DIR   OpenOCD scripts directory.
  --clean-builds          Remove known generated build/install directories before qualification.
  --no-clean-builds       Never offer pre-run generated-build cleanup.
  --only MODE             MODE is functional or benchmark. Omit to run all available tests.
  -h, --help              Show help.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --stm32h7-root) STM32CUBE_H7_ROOT="$2"; shift 2 ;;
    --output-dir) OUTPUT_BASE="$2"; shift 2 ;;
    --timing-samples) TIMING_SAMPLES="$2"; shift 2 ;;
    --observe-seconds) OBSERVE_SECONDS="$2"; shift 2 ;;
    --debug-timeout) DEBUG_TIMEOUT="$2"; shift 2 ;;
    --openocd-scripts) OPENOCD_SCRIPTS="$2"; shift 2 ;;
    --clean-builds) CLEAN_BUILDS_MODE="yes"; shift ;;
    --no-clean-builds) CLEAN_BUILDS_MODE="no"; shift ;;
    --only) ONLY_MODE="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    --*) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    *)
      [[ -z "$STM32CUBE_H7_ROOT" ]] || { echo "Unexpected argument: $1" >&2; exit 2; }
      STM32CUBE_H7_ROOT="$1"; shift
      ;;
  esac
done

if [[ -n "$ONLY_MODE" && "$ONLY_MODE" != "functional" && "$ONLY_MODE" != "benchmark" ]]; then
  echo "Unsupported --only mode: $ONLY_MODE (expected functional or benchmark)" >&2
  exit 2
fi

[[ -n "$STM32CUBE_H7_ROOT" ]] || { usage >&2; exit 2; }
STM32CUBE_H7_ROOT="$(cd "$STM32CUBE_H7_ROOT" 2>/dev/null && pwd)" || { echo "Invalid STM32CubeH7 root" >&2; exit 2; }
export STM32CUBE_H7_ROOT

for n in "$TIMING_SAMPLES" "$OBSERVE_SECONDS" "$DEBUG_TIMEOUT"; do
  [[ "$n" =~ ^[0-9]+$ ]] && (( n > 0 )) || { echo "Numeric options must be > 0" >&2; exit 2; }
done

need() { command -v "$1" >/dev/null 2>&1 || { echo "Missing command: $1" >&2; exit 2; }; }
for c in git cmake make openocd arm-none-eabi-gcc timeout tee grep sed awk; do need "$c"; done
if command -v gdb-multiarch >/dev/null 2>&1; then GDB_BIN=gdb-multiarch
elif command -v arm-none-eabi-gdb >/dev/null 2>&1; then GDB_BIN=arm-none-eabi-gdb
else echo "Install gdb-multiarch or arm-none-eabi-gdb" >&2; exit 2
fi

for f in \
  "$STM32CUBE_H7_ROOT/Drivers/CMSIS/Core/Include/core_cm7.h" \
  "$STM32CUBE_H7_ROOT/Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h755xx.h" \
  "$STM32CUBE_H7_ROOT/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal.h"; do
  [[ -f "$f" ]] || { echo "Incomplete STM32CubeH7 checkout: missing $f" >&2; exit 2; }
done

cd "$ROOT_DIR"

ask_default() { local v; read -r -p "$1 [$2]: " v; printf '%s' "${v:-$2}"; }
ask_optional() { local v; read -r -p "$1: " v; printf '%s' "$v"; }
yes_no() {
  local a
  while true; do
    read -r -p "$1 [y/n]: " a
    case "${a,,}" in y|yes) return 0;; n|no) return 1;; *) echo "Please answer y or n.";; esac
  done
}

collect_generated_dirs() {
  local p
  local -a found=()
  shopt -s nullglob
  for p in \
    "$ROOT_DIR/build" "$ROOT_DIR"/build-* \
    "$ROOT_DIR/install" "$ROOT_DIR"/install-* \
    "$ROOT_DIR"/cmake-build-* \
    "$ROOT_DIR/examples"/hardrt_h755_*/build-cortex_m \
    "$ROOT_DIR/examples"/hardrt_h755_*/cmake-build-*; do
    [[ -d "$p" ]] && found+=("$p")
  done
  shopt -u nullglob
  ((${#found[@]})) && printf '%s\n' "${found[@]}"
}

clean_generated_dirs() {
  local p
  local -a dirs=()
  mapfile -t dirs < <(collect_generated_dirs)
  ((${#dirs[@]})) || return 0
  for p in "${dirs[@]}"; do
    echo "[CLEAN] ${p#$ROOT_DIR/}"
    rm -rf -- "$p"
    CLEANED_BUILD_DIRS+=("${p#$ROOT_DIR/}")
  done
}

INITIAL_STATUS="$(git status --porcelain --untracked-files=normal)"
mapfile -t GENERATED_DIRS < <(collect_generated_dirs)
if ((${#GENERATED_DIRS[@]})); then
  do_clean=0
  case "$CLEAN_BUILDS_MODE" in
    yes) do_clean=1 ;;
    no) do_clean=0 ;;
    ask)
      if [[ -n "$INITIAL_STATUS" ]]; then
        echo "Generated build/install directories are present:"
        printf '  %s\n' "${GENERATED_DIRS[@]#$ROOT_DIR/}"
        yes_no "Remove these generated directories before qualification" && do_clean=1
      fi
      ;;
  esac
  (( do_clean == 0 )) || clean_generated_dirs
fi

SHA="$(git rev-parse HEAD)"
SHORT_SHA="$(git rev-parse --short=8 HEAD)"
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
TRACKED_STATUS="$(git status --porcelain --untracked-files=no)"
UNTRACKED_STATUS="$(git ls-files --others --exclude-standard)"
SOURCE_WORKTREE=clean; [[ -n "$TRACKED_STATUS" ]] && SOURCE_WORKTREE=DIRTY
UNTRACKED_STATE=none; [[ -n "$UNTRACKED_STATUS" ]] && UNTRACKED_STATE=present

STAMP="$(date -u +'%Y%m%dT%H%M%SZ')"
RUN_DIR="$OUTPUT_BASE/${STAMP}_${SHORT_SHA}"
RAW="$RUN_DIR/raw"
REPORT="$RUN_DIR/qualification.md"
mkdir -p "$RAW"

cleanup_openocd() {
  if [[ -n "${OPENOCD_PID:-}" ]] && kill -0 "$OPENOCD_PID" >/dev/null 2>&1; then
    kill "$OPENOCD_PID" >/dev/null 2>&1 || true
    wait "$OPENOCD_PID" >/dev/null 2>&1 || true
  fi
  OPENOCD_PID=""
}

on_exit() {
  local rc=$?
  cleanup_openocd
  if (( FINALIZED == 0 )) && [[ -f "$REPORT" ]]; then
    printf '\n## Run status\n\n**ABORTED** with runner exit code %d.\n' "$rc" >> "$REPORT"
  fi
}
trap on_exit EXIT INT TERM

TESTER="$(ask_default "Tester" "${USER:-unknown}")"
BOARD="$(ask_default "Board" "NUCLEO-H755ZI-Q")"
REVISION="$(ask_optional "MCU/board revision (optional)")"
if [[ "$SOURCE_WORKTREE" == DIRTY ]]; then
  echo "WARNING: tracked source files differ from HEAD:"; printf '%s\n' "$TRACKED_STATUS"
  yes_no "Continue and record a tracked-source DIRTY qualification" || exit 1
fi
if [[ "$UNTRACKED_STATE" == present ]]; then
  echo "Note: untracked workspace files are recorded but do not mark tracked source dirty."
fi

first_line() { "$@" 2>&1 | head -n1 || true; }
CUBE_SHA=unknown; CUBE_STATE=not-a-git-checkout
if git -C "$STM32CUBE_H7_ROOT" rev-parse HEAD >/dev/null 2>&1; then
  CUBE_SHA="$(git -C "$STM32CUBE_H7_ROOT" rev-parse HEAD)"
  CUBE_STATE=clean
  [[ -n "$(git -C "$STM32CUBE_H7_ROOT" status --porcelain --untracked-files=no 2>/dev/null || true)" ]] && CUBE_STATE=DIRTY
fi

cat > "$REPORT" <<EOF
# HardRT STM32H755 Hardware Qualification Report

- Run ID: \`${STAMP}_${SHORT_SHA}\`
- UTC start: \`$(date -u --iso-8601=seconds)\`
- Local start: \`$(date --iso-8601=seconds)\`
- Tester: \`$TESTER\`
- Board: \`$BOARD\`
- MCU/board revision: \`${REVISION:-not recorded}\`
- Core under test: \`CM7\`
- HardRT branch: \`$BRANCH\`
- HardRT SHA: \`$SHA\`
- HardRT tracked source state: **$SOURCE_WORKTREE**
- HardRT untracked workspace files: **$UNTRACKED_STATE**
- STM32CubeH7 root: \`$STM32CUBE_H7_ROOT\`
- STM32CubeH7 SHA/state: \`$CUBE_SHA\` / \`$CUBE_STATE\`
- ARM GCC: \`$(first_line arm-none-eabi-gcc --version)\`
- GDB: \`$(first_line "$GDB_BIN" --version)\`
- OpenOCD: \`$(first_line openocd --version)\`
- CMake: \`$(first_line cmake --version)\`
- Host: \`$(uname -a)\`
- Timing samples per benchmark image: \`$TIMING_SAMPLES\`
- LED observation duration: \`${OBSERVE_SECONDS}s\`
- Functional contracts: **$FUNCTIONAL_TOTAL**
- Hardware benchmarks: **$BENCHMARK_TOTAL**
- Selected mode: **${ONLY_MODE:-all tests (functional + benchmark)}**

The board probe is reported separately from functional and benchmark counts.
Each benchmark is rebuilt and flashed as a separate image. The build script enables
only the timing profile/hooks required by that benchmark; ordinary HardRT builds
remain uninstrumented. The benchmark count includes the complete v0.5 event and
notification signal-profiling matrix.

OpenOCD/GDB sessions are managed by this runner; no additional terminal windows are required.

EOF

if ((${#CLEANED_BUILD_DIRS[@]})); then
  { echo "## Pre-run generated build cleanup"; echo; printf -- '- `%s`\n' "${CLEANED_BUILD_DIRS[@]}"; echo; } >> "$REPORT"
fi
if [[ -n "$TRACKED_STATUS" ]]; then printf '## Tracked source changes\n\n```text\n%s\n```\n\n' "$TRACKED_STATUS" >> "$REPORT"; fi
if [[ -n "$UNTRACKED_STATUS" ]]; then printf '## Untracked workspace files\n\n```text\n%s\n```\n\n' "$UNTRACKED_STATUS" >> "$REPORT"; fi

record() { NAMES+=("$1"); STATUSES+=("$2"); CRITERIA+=("$3"); EVIDENCE+=("$4"); NOTES+=("$5"); }

run_logged() {
  local log="$1"; shift
  { printf '$'; printf ' %q' "$@"; printf '\n'; } | tee -a "$log"
  local rc
  set +e; "$@" 2>&1 | tee -a "$log"; rc=${PIPESTATUS[0]}; set -e
  return "$rc"
}

start_openocd() {
  local log="$1"; cleanup_openocd
  openocd -s "$OPENOCD_SCRIPTS" -f "$ROOT_DIR/scripts/openocd_h755.cfg" -c "init; reset halt" >"$log" 2>&1 &
  OPENOCD_PID=$!
  for ((i=0; i<100; ++i)); do
    grep -q "Listening on port 3333 for gdb connections" "$log" 2>/dev/null && return 0
    kill -0 "$OPENOCD_PID" >/dev/null 2>&1 || { cat "$log" >&2; OPENOCD_PID=""; return 1; }
    sleep 0.1
  done
  echo "OpenOCD GDB server timeout" >&2; cleanup_openocd; return 1
}

run_gdb() {
  local elf="$1" script="$2" prefix="$3"
  local olog="$RAW/${prefix}_openocd.log" glog="$RAW/${prefix}_gdb.log"
  [[ -s "$elf" ]] || return 1
  start_openocd "$olog" || return 1
  local rc
  set +e; timeout "${DEBUG_TIMEOUT}s" "$GDB_BIN" -q "$elf" -batch -x "$script" 2>&1 | tee "$glog"; rc=${PIPESTATUS[0]}; set -e
  cleanup_openocd
  return "$rc"
}

reset_run() { run_logged "$1" openocd -s "$OPENOCD_SCRIPTS" -f "$ROOT_DIR/scripts/openocd_h755.cfg" -c "init; reset run; shutdown"; }

progress_probe() {
  local elf="$1" prefix="$2" exits="$3"
  local cmd="$RAW/${prefix}_progress.gdb" olog="$RAW/${prefix}_openocd.log" glog="$RAW/${prefix}_gdb.log"
  cat > "$cmd" <<'GDB'
set confirm off
set pagination off
set mem inaccessible-by-default off
target extended-remote :3333
monitor arm semihosting disable
monitor reset run
shell sleep 2
monitor halt
set $counter_a_before=(unsigned)dbg_counterA
set $counter_b_before=(unsigned)dbg_counterB
set $error_before=(unsigned)g_example_error
GDB
  if [[ "$exits" == yes ]]; then cat >> "$cmd" <<'GDB'
set $exit_a_before=(unsigned)dbg_exit_counterA
set $exit_b_before=(unsigned)dbg_exit_counterB
GDB
  fi
  cat >> "$cmd" <<'GDB'
monitor resume
shell sleep 3
monitor halt
set $counter_a_after=(unsigned)dbg_counterA
set $counter_b_after=(unsigned)dbg_counterB
set $error_after=(unsigned)g_example_error
printf "counterA: %u -> %u\n",$counter_a_before,$counter_a_after
printf "counterB: %u -> %u\n",$counter_b_before,$counter_b_after
printf "g_example_error: %u -> %u\n",$error_before,$error_after
GDB
  if [[ "$exits" == yes ]]; then cat >> "$cmd" <<'GDB'
set $exit_a_after=(unsigned)dbg_exit_counterA
set $exit_b_after=(unsigned)dbg_exit_counterB
printf "exit_counterA: %u -> %u\n",$exit_a_before,$exit_a_after
printf "exit_counterB: %u -> %u\n",$exit_b_before,$exit_b_after
if $error_after==0 && $counter_a_after>$counter_a_before && $counter_b_after>$counter_b_before && $exit_a_after>$exit_a_before && $exit_b_after>$exit_b_before
  printf "RESULT: PASS\n"
else
  printf "RESULT: FAIL\n"
end
GDB
  else cat >> "$cmd" <<'GDB'
if $error_after==0 && $counter_a_after>$counter_a_before && $counter_b_after>$counter_b_before
  printf "RESULT: PASS\n"
else
  printf "RESULT: FAIL\n"
end
GDB
  fi
  cat >> "$cmd" <<'GDB'
monitor resume
detach
quit
GDB
  [[ -s "$elf" ]] || return 1
  start_openocd "$olog" || return 1
  local rc
  set +e; timeout "${DEBUG_TIMEOUT}s" "$GDB_BIN" -q "$elf" -batch -x "$cmd" 2>&1 | tee "$glog"; rc=${PIPESTATUS[0]}; set -e
  cleanup_openocd
  (( rc == 0 )) && grep -q '^RESULT: PASS$' "$glog"
}

visual_blinky() {
  local title="$1" script="$2" elf="$3" expected="$4" prefix="$5"
  local status=PASS notes="" blog="$RAW/${prefix}_build_flash.log"
  local criterion="Build/flash succeeds; g_example_error stays 0; both task counters increase; both LEDs visibly toggle and the configured relative rate difference is clearly observable for at least ${OBSERVE_SECONDS}s."
  echo; echo "========== $title =========="
  if ! run_logged "$blog" "$script"; then status=FAIL; notes="Build/flash failed."
  elif ! progress_probe "$elf" "$prefix" no; then status=FAIL; notes="Automated counter/error probe failed."
  elif ! reset_run "$RAW/${prefix}_reset_run.log"; then status=FAIL; notes="Could not run target for visual observation."
  else
    echo "$expected"
    echo "Human acceptance is qualitative: both LEDs toggle and the relative speed is obvious."
    echo "Observe for ${OBSERVE_SECONDS}s..."; sleep "$OBSERVE_SECONDS"
    if ! yes_no "Did both LEDs toggle with the expected relative speed"; then status=FAIL; notes="User rejected visual criterion. $(ask_optional "Optional observation note")"; fi
  fi
  record "$title" "$status" "$criterion" "raw/${prefix}_*.log" "$notes"
}

counter_demo() {
  local prefix=counter_demo status=PASS notes=""
  local elf="$ROOT_DIR/examples/hardrt_h755_demo/build-cortex_m/hardrt_cm7_demo.elf"
  local criterion="Build/flash succeeds; g_example_error stays 0; dbg_counterA/B and dbg_exit_counterA/B all increase."
  echo; echo "========== Scheduler counter demo =========="
  if ! run_logged "$RAW/${prefix}_build_flash.log" "$ROOT_DIR/scripts/build-lib-stm32h7xx-demo.sh"; then status=FAIL; notes="Build/flash failed."
  elif ! progress_probe "$elf" "$prefix" yes; then status=FAIL; notes="Automated four-counter probe failed."
  fi
  record "Scheduler counter demo" "$status" "$criterion" "raw/${prefix}_*.log" "$notes"
}

timing_case() {
  local case="$1" title="$2" prefix="timing_$1" status=PASS notes=""
  local elf="$ROOT_DIR/examples/hardrt_h755_dwt_timing/build-cortex_m/hardrt_cm7_dwt_timing.elf"
  local glog="$RAW/${prefix}_gdb.log"
  local criterion="Build/flash succeeds; target sample count is reached; no kernel/example error; min <= avg <= max."
  echo; echo "========== $title =========="
  if ! run_logged "$RAW/${prefix}_build_flash.log" "$ROOT_DIR/scripts/build-lib-stm32h7xx-dwt-timing.sh" --case "$case" --samples "$TIMING_SAMPLES"; then
    status=FAIL; notes="Build/flash failed."
  elif ! run_gdb "$elf" "$ROOT_DIR/scripts/gdb/timing.dbg" "$prefix"; then
    status=FAIL; notes="Timing GDB run failed or timed out."
  else
    local count target min avg max
    count="$(sed -n 's/^count=\([0-9][0-9]*\)$/\1/p' "$glog" | tail -n1)"
    target="$(sed -n 's/^event_hz=[0-9][0-9]* target_samples=\([0-9][0-9]*\)$/\1/p' "$glog" | tail -n1)"
    read -r min avg max < <(sed -n 's/^min=\([0-9][0-9]*\) cycles, avg=\([0-9][0-9]*\) cycles (sum\/count=[0-9][0-9]*), max=\([0-9][0-9]*\) cycles$/\1 \2 \3/p' "$glog" | tail -n1) || true
    if grep -qE 'hrt_error hit|timing example failure' "$glog" || [[ -z "$count" || -z "$target" || -z "${min:-}" || -z "${avg:-}" || -z "${max:-}" ]] || (( count != TIMING_SAMPLES || target != TIMING_SAMPLES || min > avg || avg > max )); then
      status=FAIL; notes="Automated timing acceptance failed."
    else
      notes="count=$count, min=$min cycles, avg=$avg cycles, max=$max cycles."
    fi
  fi
  record "$title" "$status" "$criterion" "raw/${prefix}_*.log" "$notes"
}

signal_timing_case() {
  local case="$1" waiters="$2"
  local suffix=""
  [[ "$case" == event_scan_* ]] && suffix="_waiters${waiters}"
  local label="${case}${suffix}"
  local title="DWT ${label} timing"
  local prefix="timing_${label}" status=PASS notes=""
  local elf="$ROOT_DIR/examples/hardrt_h755_dwt_timing/build-cortex_m/hardrt_cm7_dwt_timing.elf"
  local glog="$RAW/${prefix}_gdb.log"
  local criterion="Production event/notification path reaches the target sample count with no kernel/example error; min <= avg <= max; event-scan waiter load and wake fan-out match the requested scenario."

  echo; echo "========== $title =========="
  if ! run_logged "$RAW/${prefix}_build_flash.log" \
       "$ROOT_DIR/scripts/build-lib-stm32h7xx-dwt-timing.sh" \
       --case "$case" --waiters "$waiters" --samples "$TIMING_SAMPLES"; then
    status=FAIL; notes="Build/flash failed."
  elif ! run_gdb "$elf" "$ROOT_DIR/scripts/gdb/timing.dbg" "$prefix"; then
    status=FAIL; notes="Timing GDB run failed or timed out."
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
      status=FAIL; notes="Automated signal timing acceptance failed."
    elif [[ "$case" == event_scan_* ]] && (( signal_waiters != waiters )); then
      status=FAIL; notes="Firmware waiter count did not match requested event scan load."
    else
      notes="waiters=$signal_waiters, expected_wakes/sample=$expected, observed_wakes=$observed, count=$count, min=$min cycles, avg=$avg cycles, max=$max cycles."
    fi
  fi
  record "$title" "$status" "$criterion" "raw/${prefix}_*.log" "$notes"
}

tick_benchmark_case() {
  local scenario="$1" tasks="$2"
  local title="DWT tick_${scenario}_tasks${tasks} timing"
  local prefix="tick_${scenario}_tasks${tasks}" status=PASS notes=""
  local elf="$ROOT_DIR/examples/hardrt_h755_tick_benchmark/build-cortex_m/hardrt_h755_tick_benchmark.elf"
  local glog="$RAW/${prefix}_gdb.log"
  local criterion="Production hrt_tick_from_isr() completes for the configured task capacity/workload; target sample count is reached; min <= avg <= max; expected wake activity matches the scenario."
  echo; echo "========== $title =========="
  if ! run_logged "$RAW/${prefix}_build_flash.log" "$ROOT_DIR/scripts/build-lib-stm32h7xx-tick-benchmark.sh" --scenario "$scenario" --tasks "$tasks" --samples "$TIMING_SAMPLES"; then
    status=FAIL; notes="Build/flash failed."
  elif ! run_gdb "$elf" "$ROOT_DIR/scripts/gdb/tick_benchmark.dbg" "$prefix"; then
    status=FAIL; notes="Tick benchmark GDB run failed or timed out."
  else
    local count target capacity workers wakes min avg max
    count="$(sed -n 's/^count=\([0-9][0-9]*\)$/\1/p' "$glog" | tail -n1)"
    target="$(sed -n 's/^event_hz=[0-9][0-9]* target_samples=\([0-9][0-9]*\) final_tick=[0-9][0-9]*$/\1/p' "$glog" | tail -n1)"
    capacity="$(sed -n 's/^configured_app_tasks=\([0-9][0-9]*\) worker_tasks=[0-9][0-9]*$/\1/p' "$glog" | tail -n1)"
    workers="$(sed -n 's/^configured_app_tasks=[0-9][0-9]* worker_tasks=\([0-9][0-9]*\)$/\1/p' "$glog" | tail -n1)"
    wakes="$(sed -n 's/^worker_wakes=\([0-9][0-9]*\) error=[0-9][0-9]*$/\1/p' "$glog" | tail -n1)"
    read -r min avg max < <(sed -n 's/^min=\([0-9][0-9]*\) cycles, avg=\([0-9][0-9]*\) cycles (sum\/count=[0-9][0-9]*), max=\([0-9][0-9]*\) cycles$/\1 \2 \3/p' "$glog" | tail -n1) || true
    if ! grep -q '^RESULT: PASS$' "$glog" || [[ -z "$count" || -z "$target" || -z "$capacity" || -z "$workers" || -z "$wakes" || -z "${min:-}" || -z "${avg:-}" || -z "${max:-}" ]] || (( count != TIMING_SAMPLES || target != TIMING_SAMPLES || capacity != tasks || min > avg || avg > max )); then
      status=FAIL; notes="Automated tick benchmark acceptance failed."
    else
      notes="scenario=$scenario, app_tasks=$capacity, workers=$workers, wakes=$wakes, count=$count, min=$min cycles, avg=$avg cycles, max=$max cycles."
    fi
  fi
  record "$title" "$status" "$criterion" "raw/${prefix}_*.log" "$notes"
}

preemption_case() {
  local case="$1" title="$2" prefix="preemption_$1" status=PASS notes=""
  local elf="$ROOT_DIR/examples/hardrt_h755_preemption/build-cortex_m/hardrt_h755_preemption.elf"
  local glog="$RAW/${prefix}_gdb.log" criterion
  if [[ "$case" == priority ]]; then criterion="ISR wake dispatches the higher-priority task before interrupted lower-priority Thread mode continues."
  else criterion="Trace is low-A -> high -> low-A -> low-B and low-A retains unused RR quantum."; fi
  echo; echo "========== $title =========="
  if ! run_logged "$RAW/${prefix}_build_flash.log" "$ROOT_DIR/scripts/build-lib-stm32h7xx-preemption.sh" --case "$case"; then status=FAIL; notes="Build/flash failed."
  elif ! run_gdb "$elf" "$ROOT_DIR/scripts/gdb/preemption.dbg" "$prefix"; then status=FAIL; notes="Preemption GDB run failed or timed out."
  elif ! grep -q '^RESULT: PASS$' "$glog"; then status=FAIL; notes="Firmware validator reported failure."
  else notes="$(grep -E '^(case=|irq_count=|ticks:|RR remaining:|sequence slots:|RESULT:)' "$glog" | tr '\n' ' ' | sed 's/[[:space:]]*$//')"; fi
  record "$title" "$status" "$criterion" "raw/${prefix}_*.log" "$notes"
}

global_rr_case() {
  local prefix=global_rr status=PASS notes=""
  local title="Global RR mixed-priority hardware contract"
  local elf="$ROOT_DIR/examples/hardrt_h755_global_rr/build-cortex_m/hardrt_h755_global_rr.elf"
  local glog="$RAW/${prefix}_gdb.log"
  local criterion="Under HRT_SCHED_RR, mixed-priority READY tasks follow one global FIFO; a real TIM2 ISR wake reports need_switch=0; the interrupted task continues and the woken high-priority task joins the global tail behind already READY work."
  echo; echo "========== $title =========="
  if ! run_logged "$RAW/${prefix}_build_flash.log" "$ROOT_DIR/scripts/build-lib-stm32h7xx-global-rr.sh"; then status=FAIL; notes="Build/flash failed."
  elif ! run_gdb "$elf" "$ROOT_DIR/scripts/gdb/global_rr.dbg" "$prefix"; then status=FAIL; notes="Global RR GDB run failed or timed out."
  elif ! grep -q '^RESULT: PASS$' "$glog"; then status=FAIL; notes="Firmware validator reported failure."
  else notes="$(grep -E '^(pass=|irq_count=|runs:|sequence slots:|RESULT:)' "$glog" | tr '\n' ' ' | sed 's/[[:space:]]*$//')"; fi
  record "$title" "$status" "$criterion" "raw/${prefix}_*.log" "$notes"
}

ipc_case() {
  local case="$1" title="$2" status=PASS notes=""
  local prefix="ipc_$case"
  local elf="$ROOT_DIR/examples/hardrt_h755_ipc_validation/build-cortex_m/hardrt_h755_ipc_validation.elf"
  local glog="$RAW/${prefix}_gdb.log" criterion
  case "$case" in
    semaphore) criterion="Counting/saturation checks pass; real TIM2 ISR wakes a blocked higher-priority waiter with correct need_switch." ;;
    queue) criterion="FIFO/full/empty checks pass; ISR send->blocked receiver and ISR receive->blocked sender preserve payload and priority handoff." ;;
    mutex) criterion="Ownership, blocking, direct handoff and immediate higher-priority execution after unlock are correct." ;;
    event) criterion="Wait-all completes from incremental task/ISR sets; clear-on-exit and retained wait-any behavior are correct; the real TIM2 ISR reports scheduler-aware need_switch and preempts when required." ;;
    notification) criterion="Pending notifications survive unrelated IPC blocking; overwrite/no-overwrite/set-bits, real TIM2 ISR wake/need_switch, increment and counting-take semantics are correct." ;;
  esac
  echo; echo "========== $title =========="
  if ! run_logged "$RAW/${prefix}_build_flash.log" "$ROOT_DIR/scripts/build-lib-stm32h7xx-ipc-validation.sh" --case "$case"; then status=FAIL; notes="Build/flash failed."
  elif ! run_gdb "$elf" "$ROOT_DIR/scripts/gdb/ipc_validation.dbg" "$prefix"; then status=FAIL; notes="IPC GDB run failed or timed out."
  elif ! grep -q '^RESULT: PASS$' "$glog"; then status=FAIL; notes="Firmware validator reported failure."
  else notes="$(grep -E '^(case=|irq_count=|need_switch|observed|sequence slots:|RESULT:)' "$glog" | tr '\n' ' ' | sed 's/[[:space:]]*$//')"; fi
  record "$title" "$status" "$criterion" "raw/${prefix}_*.log" "$notes"
}

external_tick_case() {
  local prefix=external_tick status=PASS notes=""
  local title="External tick hardware contract"
  local elf="$ROOT_DIR/examples/hardrt_h755_external_tick/build-cortex_m/hardrt_h755_external_tick.elf"
  local glog="$RAW/${prefix}_gdb.log"
  local criterion="SysTick stays disabled; periodic TIM2 ISR drives hrt_tick_from_isr(); sleeping high-priority work wakes at the requested tick count and preempts lower-priority work."
  echo; echo "========== $title =========="
  if ! run_logged "$RAW/${prefix}_build_flash.log" "$ROOT_DIR/scripts/build-lib-stm32h7xx-external-tick.sh"; then status=FAIL; notes="Build/flash failed."
  elif ! run_gdb "$elf" "$ROOT_DIR/scripts/gdb/external_tick_validation.dbg" "$prefix"; then status=FAIL; notes="External-tick GDB run failed or timed out."
  elif ! grep -q '^RESULT: PASS$' "$glog"; then status=FAIL; notes="Firmware validator reported failure."
  else notes="$(grep -E '^(case=|irq_count=|tick|wake|sequence slots:|RESULT:)' "$glog" | tr '\n' ' ' | sed 's/[[:space:]]*$//')"; fi
  record "$title" "$status" "$criterion" "raw/${prefix}_*.log" "$notes"
}

basepri_case() {
  local prefix=basepri_validation status=PASS notes=""
  local title="BASEPRI critical-section hardware contract"
  local elf="$ROOT_DIR/examples/hardrt_h755_basepri_validation/build-cortex_m/hardrt_h755_basepri_validation.elf"
  local glog="$RAW/${prefix}_gdb.log"
  local criterion="Unmasked entry installs the HardRT BASEPRI ceiling; weaker masks are tightened; stricter pre-existing masks are preserved; nested sections restore only on outer exit; final BASEPRI is zero."
  echo; echo "========== $title =========="
  if ! run_logged "$RAW/${prefix}_build_flash.log" "$ROOT_DIR/scripts/build-lib-stm32h7xx-basepri-validation.sh"; then status=FAIL; notes="Build/flash failed."
  elif ! run_gdb "$elf" "$ROOT_DIR/scripts/gdb/basepri_validation.dbg" "$prefix"; then status=FAIL; notes="BASEPRI GDB run failed or timed out."
  elif ! grep -q '^RESULT: PASS$' "$glog"; then status=FAIL; notes="Firmware validator reported failure."
  else notes="$(grep -E '^(pass=|zero:|weaker:|stricter:|final_basepri=|RESULT:)' "$glog" | tr '\n' ' ' | sed 's/[[:space:]]*$//')"; fi
  record "$title" "$status" "$criterion" "raw/${prefix}_*.log" "$notes"
}

append_switch_breakdown_report() {
  local glog="$RAW/timing_scheduler_decision_gdb.log"
  [[ -f "$glog" ]] || return 0
  {
    echo
    echo "## Scheduler / PendSV switch breakdown"
    echo
    echo '```text'
    sed -n '/^\[SWITCH BREAKDOWN\]/,/^NOTE: derived values/p' "$glog"
    echo '```'
  } >> "$REPORT"
}

print_switch_breakdown() {
  local glog="$RAW/timing_scheduler_decision_gdb.log"
  [[ -f "$glog" ]] || return 0
  echo
  echo "Scheduler / PendSV switch breakdown:"
  sed -n '/^\[SWITCH BREAKDOWN\]/,/^NOTE: derived values/p' "$glog" | sed '/^\[SWITCH BREAKDOWN\]$/d'
}

run_functional_matrix() {
  visual_blinky "C blinky" "$ROOT_DIR/scripts/build-lib-stm32h7xx-blinky.sh" "$ROOT_DIR/examples/hardrt_h755_blinky/build-cortex_m/hardrt_h755_blinky.elf" "LD1/PB0 should be visibly about 2x faster than LD2/PE1 (250 ms vs 500 ms configured)." c_blinky
  visual_blinky "C++ blinky" "$ROOT_DIR/scripts/build-lib-stm32h7xx-blinky-cpp.sh" "$ROOT_DIR/examples/hardrt_h755_blinky_cpp/build-cortex_m/hardrt_h755_blinky_cpp.elf" "LD1/PB0 should be clearly faster than LD2/PE1 (100 ms vs 250 ms configured)." cpp_blinky
  counter_demo
  preemption_case priority "Fixed-priority hardware preemption"
  global_rr_case
  preemption_case priority_rr "PRIORITY_RR retained-quantum preemption"
  ipc_case semaphore "Semaphore hardware contract"
  ipc_case queue "Queue hardware contract"
  ipc_case mutex "Mutex hardware contract"
  ipc_case event "Event flags hardware contract"
  ipc_case notification "Task notification hardware contract"
  external_tick_case
  basepri_case
}

run_benchmarks() {
  # Every benchmark gets its own build and flash. The timing helper selects
  # HARDRT_TIMING_PROFILE/hooks per case; tick and v0.5 signal timing use
  # application-side DWT around production code with profile=none.
  timing_case event_to_task "DWT event_to_task timing"
  timing_case sem_isr_ready "DWT sem_isr_ready timing"
  timing_case ready_to_task "DWT ready_to_task timing"
  timing_case scheduler_decision "DWT scheduler_decision timing"

  signal_timing_case event_isr_to_task 1
  signal_timing_case notify_isr_to_task 1
  local waiters
  for waiters in 1 8 16 32; do
    signal_timing_case event_scan_none "$waiters"
    signal_timing_case event_scan_one "$waiters"
    signal_timing_case event_scan_all "$waiters"
  done
  signal_timing_case notify_isr_no_wake 1
  signal_timing_case notify_isr_wake 1

  local tasks scenario
  for tasks in 8 16 32; do
    for scenario in none one_sleep all_sleep one_expiry simultaneous staggered; do
      tick_benchmark_case "$scenario" "$tasks"
    done
  done
}

run_all_tests() {
  run_functional_matrix
  run_benchmarks
}

PROBE="$RAW/00_probe.log"
echo; echo "Checking ST-Link/OpenOCD target connection..."
if ! run_logged "$PROBE" openocd -s "$OPENOCD_SCRIPTS" -f "$ROOT_DIR/scripts/openocd_h755.cfg" -c "init; targets; shutdown"; then
  record "Board probe" FAIL "OpenOCD connects to STM32H755 before qualification." "raw/00_probe.log" "Probe failed; remaining tests skipped."
else
  record "Board probe" PASS "OpenOCD connects to STM32H755 before qualification." "raw/00_probe.log" ""
  case "$ONLY_MODE" in
    functional) run_functional_matrix ;;
    benchmark) run_benchmarks ;;
    "") run_all_tests ;;
  esac
fi

PROBE_PASS=0
PROBE_FAIL=0
FUNCTIONAL_PASS=0
FUNCTIONAL_FAIL=0
BENCHMARK_PASS=0
BENCHMARK_FAIL=0

for i in "${!STATUSES[@]}"; do
  name="${NAMES[$i]}"
  status="${STATUSES[$i]}"
  if [[ "$name" == "Board probe" ]]; then
    [[ "$status" == PASS ]] && ((PROBE_PASS+=1)) || ((PROBE_FAIL+=1))
  elif [[ "$name" == "DWT "*" timing" ]]; then
    [[ "$status" == PASS ]] && ((BENCHMARK_PASS+=1)) || ((BENCHMARK_FAIL+=1))
  else
    [[ "$status" == PASS ]] && ((FUNCTIONAL_PASS+=1)) || ((FUNCTIONAL_FAIL+=1))
  fi
done

if [[ "$ONLY_MODE" == "benchmark" ]]; then
  FUNCTIONAL_NOT_RUN=$FUNCTIONAL_TOTAL
else
  FUNCTIONAL_NOT_RUN=$((FUNCTIONAL_TOTAL - FUNCTIONAL_PASS - FUNCTIONAL_FAIL))
  (( FUNCTIONAL_NOT_RUN < 0 )) && FUNCTIONAL_NOT_RUN=0
fi

if [[ "$ONLY_MODE" == "functional" ]]; then
  BENCHMARK_NOT_RUN=$BENCHMARK_TOTAL
else
  BENCHMARK_NOT_RUN=$((BENCHMARK_TOTAL - BENCHMARK_PASS - BENCHMARK_FAIL))
  (( BENCHMARK_NOT_RUN < 0 )) && BENCHMARK_NOT_RUN=0
fi

RUN_FAIL=$((PROBE_FAIL + FUNCTIONAL_FAIL + BENCHMARK_FAIL))

{
  echo "## Test results"; echo
  echo "| Test | Result | PASS criterion | Evidence | Notes |"
  echo "|---|:---:|---|---|---|"
  for i in "${!NAMES[@]}"; do
    n="${NAMES[$i]//|/\\|}"; c="${CRITERIA[$i]//|/\\|}"; e="${EVIDENCE[$i]//|/\\|}"; note="${NOTES[$i]//|/\\|}"
    printf '| %s | **%s** | %s | `%s` | %s |\n' "$n" "${STATUSES[$i]}" "$c" "$e" "${note:- }"
  done
} >> "$REPORT"

append_switch_breakdown_report

{
  echo; echo "## Qualification verdict"; echo
  echo "- Board probe: **$([[ $PROBE_FAIL -eq 0 && $PROBE_PASS -eq 1 ]] && echo PASS || echo FAIL)**"
  echo "- Functional contracts passed: **$FUNCTIONAL_PASS / $FUNCTIONAL_TOTAL**"
  echo "- Functional contracts failed: **$FUNCTIONAL_FAIL**"
  echo "- Functional contracts not run: **$FUNCTIONAL_NOT_RUN**"
  echo "- Benchmarks passed: **$BENCHMARK_PASS / $BENCHMARK_TOTAL**"
  echo "- Benchmarks failed: **$BENCHMARK_FAIL**"
  echo "- Benchmarks not run: **$BENCHMARK_NOT_RUN**"

  case "$ONLY_MODE" in
    functional)
      if (( RUN_FAIL == 0 && FUNCTIONAL_NOT_RUN == 0 )); then echo "- Overall: **PASS**"
      else echo "- Overall: **FAIL**"; fi
      ;;
    benchmark)
      if (( RUN_FAIL == 0 && BENCHMARK_NOT_RUN == 0 )); then echo "- Overall: **PASS**"
      else echo "- Overall: **FAIL**"; fi
      ;;
    "")
      if (( RUN_FAIL == 0 && FUNCTIONAL_NOT_RUN == 0 && BENCHMARK_NOT_RUN == 0 )); then echo "- Overall: **PASS**"
      else echo "- Overall: **FAIL**"; fi
      ;;
  esac
  echo; echo "## Tester notes"; echo
} >> "$REPORT"

FINAL_NOTES="$(ask_optional "Final qualification notes (optional)")"
[[ -n "$FINAL_NOTES" ]] && printf '%s\n' "$FINAL_NOTES" >> "$REPORT" || echo "None." >> "$REPORT"
printf '\n## Completion\n\n- UTC end: `%s`\n- Local end: `%s`\n- Runner: `scripts/stm32_manual_test_full.sh`\n' "$(date -u --iso-8601=seconds)" "$(date --iso-8601=seconds)" >> "$REPORT"
FINALIZED=1

echo
echo "============================================================"
echo "HardRT STM32 hardware qualification summary"
echo "============================================================"
echo "HardRT SHA : $SHA"
echo "Cube SHA   : $CUBE_SHA ($CUBE_STATE)"
printf 'Probe      : %s\n' "$([[ $PROBE_FAIL -eq 0 && $PROBE_PASS -eq 1 ]] && echo PASS || echo FAIL)"
printf 'Functional : %d/%d PASS, %d FAIL, %d NOT RUN\n' "$FUNCTIONAL_PASS" "$FUNCTIONAL_TOTAL" "$FUNCTIONAL_FAIL" "$FUNCTIONAL_NOT_RUN"
printf 'Benchmark  : %d/%d PASS, %d FAIL, %d NOT RUN\n' "$BENCHMARK_PASS" "$BENCHMARK_TOTAL" "$BENCHMARK_FAIL" "$BENCHMARK_NOT_RUN"

echo
printf '%-44s %s\n' "CASE" "RESULT"
printf '%-44s %s\n' "--------------------------------------------" "------"
for i in "${!NAMES[@]}"; do printf '%-44s %s\n' "${NAMES[$i]}" "${STATUSES[$i]}"; done

echo
echo "Benchmark timing (cycles, min / avg / max):"
for i in "${!NAMES[@]}"; do
  case "${NAMES[$i]}" in
    "DWT "*" timing")
      min="$(sed -n 's/.*min=\([0-9][0-9]*\) cycles.*/\1/p' <<< "${NOTES[$i]}")"
      avg="$(sed -n 's/.*avg=\([0-9][0-9]*\) cycles.*/\1/p' <<< "${NOTES[$i]}")"
      max="$(sed -n 's/.*max=\([0-9][0-9]*\) cycles.*/\1/p' <<< "${NOTES[$i]}")"
      label="${NAMES[$i]#DWT }"; label="${label% timing}"
      [[ -n "$min" && -n "$avg" && -n "$max" ]] && printf '  %-32s %s / %s / %s\n' "$label" "$min" "$avg" "$max"
      ;;
  esac
done

print_switch_breakdown

echo
for i in "${!NAMES[@]}"; do
  if [[ "${NAMES[$i]}" == "Global RR mixed-priority hardware contract" ]]; then
    irq="$(grep -o 'irq_count=[0-9]* need_switch=-*[0-9]*' <<< "${NOTES[$i]}" || true)"
    runs="$(grep -o 'runs: A=[0-9]* B=[0-9]* high=[0-9]*' <<< "${NOTES[$i]}" || true)"
    seq="$(grep -o 'sequence slots: \[[^]]*\]' <<< "${NOTES[$i]}" || true)"
    [[ -n "$irq" ]] && echo "RR: $irq"
    [[ -n "$runs" ]] && echo "RR: $runs"
    [[ -n "$seq" ]] && echo "RR: $seq"
  fi
  if [[ "${NAMES[$i]}" == "PRIORITY_RR retained-quantum preemption" ]]; then
    rr="$(grep -o 'RR remaining: expected=[0-9]* observed=[0-9]*' <<< "${NOTES[$i]}" || true)"
    seq="$(grep -o 'sequence slots: \[[^]]*\]' <<< "${NOTES[$i]}" || true)"
    [[ -n "$rr" ]] && echo "PRIORITY_RR: $rr"
    [[ -n "$seq" ]] && echo "PRIORITY_RR: $seq"
  fi
  if [[ "${NAMES[$i]}" == "BASEPRI critical-section hardware contract" ]]; then
    stricter="$(grep -o 'stricter: before=0x[0-9a-fA-F]* inside=0x[0-9a-fA-F]* nested=0x[0-9a-fA-F]* after_inner=0x[0-9a-fA-F]* after_outer=0x[0-9a-fA-F]*' <<< "${NOTES[$i]}" || true)"
    final="$(grep -o 'final_basepri=0x[0-9a-fA-F]*' <<< "${NOTES[$i]}" || true)"
    [[ -n "$stricter" ]] && echo "BASEPRI: $stricter"
    [[ -n "$final" ]] && echo "BASEPRI: $final"
  fi
done

case "$ONLY_MODE" in
  functional)
    (( RUN_FAIL == 0 && FUNCTIONAL_NOT_RUN == 0 )) && OVERALL=PASS || OVERALL=FAIL
    ;;
  benchmark)
    (( RUN_FAIL == 0 && BENCHMARK_NOT_RUN == 0 )) && OVERALL=PASS || OVERALL=FAIL
    ;;
  "")
    (( RUN_FAIL == 0 && FUNCTIONAL_NOT_RUN == 0 && BENCHMARK_NOT_RUN == 0 )) && OVERALL=PASS || OVERALL=FAIL
    ;;
esac

echo "Overall    : $OVERALL"
echo "Report     : $REPORT"
echo "Raw logs   : $RAW"
if [[ -z "$ONLY_MODE" ]]; then
  echo "Release evidence: manually copy/move this full run to validation/stm32/releases/vX.Y.Z/."
else
  echo "Filtered run: useful evidence, but not complete release qualification evidence."
fi
echo "============================================================"

(( RUN_FAIL == 0 ))
