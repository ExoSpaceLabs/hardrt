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
ONLY_CASE=""
CLEANED_BUILD_DIRS=()

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

Runs the complete 13-case NUCLEO-H755ZI-Q hardware qualification matrix plus
all currently defined hardware timing diagnostics, including the scheduler /
PendSV switch breakdown.

Development evidence is written under validation/stm32/.
Timestamped development runs are gitignored; release evidence remains trackable under:
  validation/stm32/releases/vX.Y.Z/

Options:
  --stm32h7-root DIR      STM32CubeH7 checkout root.
  --output-dir DIR        Evidence root (default: validation/stm32).
  --timing-samples N      Samples per DWT case (default: 10000).
  --observe-seconds N     LED observation duration (default: 10).
  --debug-timeout N       GDB timeout per automated case (default: 90).
  --openocd-scripts DIR   OpenOCD scripts directory.
  --clean-builds          Remove known generated build/install directories before qualification.
  --no-clean-builds       Never offer pre-run generated-build cleanup.
  --only scheduler_decision
                          Filter the full runner to board probe + scheduler timing diagnostic.
                          The same diagnostic is always included by the default full run.
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
    --only) ONLY_CASE="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    --*) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    *)
      [[ -z "$STM32CUBE_H7_ROOT" ]] || { echo "Unexpected argument: $1" >&2; exit 2; }
      STM32CUBE_H7_ROOT="$1"; shift
      ;;
  esac
done

if [[ -n "$ONLY_CASE" && "$ONLY_CASE" != "scheduler_decision" ]]; then
  echo "Unsupported --only case: $ONLY_CASE" >&2
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
- Timing samples per case: \`$TIMING_SAMPLES\`
- LED observation duration: \`${OBSERVE_SECONDS}s\`
- Functional matrix size: **13 cases**
- Default diagnostics: **scheduler/PendSV switch breakdown**
- Selected mode: **${ONLY_CASE:-full matrix + diagnostics}**

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
set $a1=(unsigned)dbg_counterA
set $b1=(unsigned)dbg_counterB
set $e1=(unsigned)g_example_error
GDB
  if [[ "$exits" == yes ]]; then cat >> "$cmd" <<'GDB'
set $xa1=(unsigned)dbg_exit_counterA
set $xb1=(unsigned)dbg_exit_counterB
GDB
  fi
  cat >> "$cmd" <<'GDB'
monitor resume
shell sleep 3
monitor halt
set $a2=(unsigned)dbg_counterA
set $b2=(unsigned)dbg_counterB
set $e2=(unsigned)g_example_error
printf "counterA: %u -> %u\n",$a1,$a2
printf "counterB: %u -> %u\n",$b1,$b2
printf "g_example_error: %u -> %u\n",$e1,$e2
GDB
  if [[ "$exits" == yes ]]; then cat >> "$cmd" <<'GDB'
set $xa2=(unsigned)dbg_exit_counterA
set $xb2=(unsigned)dbg_exit_counterB
printf "exit_counterA: %u -> %u\n",$xa1,$xa2
printf "exit_counterB: %u -> %u\n",$xb1,$xb2
if $e2==0 && $a2>$a1 && $b2>$b1 && $xa2>$xa1 && $xb2>$xb1
  printf "RESULT: PASS\n"
else
  printf "RESULT: FAIL\n"
end
GDB
  else cat >> "$cmd" <<'GDB'
if $e2==0 && $a2>$a1 && $b2>$b1
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

ipc_case() {
  local case="$1" title="$2" status=PASS notes=""
  local prefix="ipc_$case"
  local elf="$ROOT_DIR/examples/hardrt_h755_ipc_validation/build-cortex_m/hardrt_h755_ipc_validation.elf"
  local glog="$RAW/${prefix}_gdb.log" criterion
  case "$case" in
    semaphore) criterion="Counting/saturation checks pass; real TIM2 ISR wakes a blocked higher-priority waiter with correct need_switch." ;;
    queue) criterion="FIFO/full/empty checks pass; ISR send->blocked receiver and ISR receive->blocked sender preserve payload and priority handoff." ;;
    mutex) criterion="Ownership, blocking, direct handoff and immediate higher-priority execution after unlock are correct." ;;
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

PROBE="$RAW/00_probe.log"
echo; echo "Checking ST-Link/OpenOCD target connection..."
if ! run_logged "$PROBE" openocd -s "$OPENOCD_SCRIPTS" -f "$ROOT_DIR/scripts/openocd_h755.cfg" -c "init; targets; shutdown"; then
  record "Board probe" FAIL "OpenOCD connects to STM32H755 before qualification." "raw/00_probe.log" "Probe failed; remaining tests skipped."
else
  record "Board probe" PASS "OpenOCD connects to STM32H755 before qualification." "raw/00_probe.log" ""
  if [[ "$ONLY_CASE" == "scheduler_decision" ]]; then
    timing_case scheduler_decision "DWT scheduler_decision timing"
  else
    visual_blinky "C blinky" "$ROOT_DIR/scripts/build-lib-stm32h7xx-blinky.sh" "$ROOT_DIR/examples/hardrt_h755_blinky/build-cortex_m/hardrt_h755_blinky.elf" "LD1/PB0 should be visibly about 2x faster than LD2/PE1 (250 ms vs 500 ms configured)." c_blinky
    visual_blinky "C++ blinky" "$ROOT_DIR/scripts/build-lib-stm32h7xx-blinky-cpp.sh" "$ROOT_DIR/examples/hardrt_h755_blinky_cpp/build-cortex_m/hardrt_h755_blinky_cpp.elf" "LD1/PB0 should be clearly faster than LD2/PE1 (100 ms vs 250 ms configured)." cpp_blinky
    counter_demo
    timing_case event_to_task "DWT event_to_task timing"
    timing_case sem_isr_ready "DWT sem_isr_ready timing"
    timing_case ready_to_task "DWT ready_to_task timing"
    timing_case scheduler_decision "DWT scheduler_decision timing"
    preemption_case priority "Fixed-priority hardware preemption"
    preemption_case priority_rr "PRIORITY_RR retained-quantum preemption"
    ipc_case semaphore "Semaphore hardware contract"
    ipc_case queue "Queue hardware contract"
    ipc_case mutex "Mutex hardware contract"
    external_tick_case
  fi
fi

PASS=0
FAIL=0
DIAG_PASS=0
DIAG_FAIL=0
if [[ "$ONLY_CASE" == "scheduler_decision" ]]; then
  for s in "${STATUSES[@]}"; do [[ "$s" == PASS ]] && ((PASS+=1)) || ((FAIL+=1)); done
  TOTAL_CASES=${#STATUSES[@]}
  NOT_RUN=0
else
  for i in "${!STATUSES[@]}"; do
    if [[ "${NAMES[$i]}" == "DWT scheduler_decision timing" ]]; then
      [[ "${STATUSES[$i]}" == PASS ]] && ((DIAG_PASS+=1)) || ((DIAG_FAIL+=1))
    else
      [[ "${STATUSES[$i]}" == PASS ]] && ((PASS+=1)) || ((FAIL+=1))
    fi
  done
  TOTAL_CASES=13
  NOT_RUN=$((TOTAL_CASES - PASS - FAIL)); (( NOT_RUN < 0 )) && NOT_RUN=0
fi
RUN_FAIL=$((FAIL + DIAG_FAIL))

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
  if [[ "$ONLY_CASE" == "scheduler_decision" ]]; then
    echo "- Selected checks passed: **$PASS / $TOTAL_CASES**"
    echo "- Selected checks failed: **$FAIL**"
  else
    echo "- Functional matrix passed: **$PASS / $TOTAL_CASES**"
    echo "- Functional matrix failed: **$FAIL**"
    echo "- Functional matrix not run: **$NOT_RUN**"
    echo "- Diagnostics passed: **$DIAG_PASS / 1**"
    echo "- Diagnostics failed: **$DIAG_FAIL**"
  fi
  if (( RUN_FAIL == 0 && NOT_RUN == 0 )); then echo "- Overall: **PASS**"
  elif (( RUN_FAIL > 0 )); then echo "- Overall: **FAIL**"
  else echo "- Overall: **PARTIAL**"; fi
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
if [[ "$ONLY_CASE" == "scheduler_decision" ]]; then
  printf 'Selected   : %d/%d PASS, %d FAIL\n' "$PASS" "$TOTAL_CASES" "$FAIL"
else
  printf 'Functional : %d/%d PASS, %d FAIL, %d NOT RUN\n' "$PASS" "$TOTAL_CASES" "$FAIL" "$NOT_RUN"
  printf 'Diagnostic : %d/1 PASS, %d FAIL\n' "$DIAG_PASS" "$DIAG_FAIL"
fi
echo
printf '%-44s %s\n' "CASE" "RESULT"
printf '%-44s %s\n' "--------------------------------------------" "------"
for i in "${!NAMES[@]}"; do printf '%-44s %s\n' "${NAMES[$i]}" "${STATUSES[$i]}"; done
if (( NOT_RUN > 0 )); then printf '%-44s %s\n' "remaining matrix cases" "$NOT_RUN NOT RUN"; fi

echo
echo "Timing (cycles, min / avg / max):"
for i in "${!NAMES[@]}"; do
  case "${NAMES[$i]}" in
    "DWT "*" timing")
      min="$(sed -n 's/.*min=\([0-9][0-9]*\) cycles.*/\1/p' <<< "${NOTES[$i]}")"
      avg="$(sed -n 's/.*avg=\([0-9][0-9]*\) cycles.*/\1/p' <<< "${NOTES[$i]}")"
      max="$(sed -n 's/.*max=\([0-9][0-9]*\) cycles.*/\1/p' <<< "${NOTES[$i]}")"
      label="${NAMES[$i]#DWT }"; label="${label% timing}"
      [[ -n "$min" && -n "$avg" && -n "$max" ]] && printf '  %-24s %s / %s / %s\n' "$label" "$min" "$avg" "$max"
      ;;
  esac
done

print_switch_breakdown

echo
for i in "${!NAMES[@]}"; do
  if [[ "${NAMES[$i]}" == "PRIORITY_RR retained-quantum preemption" ]]; then
    rr="$(grep -o 'RR remaining: expected=[0-9]* observed=[0-9]*' <<< "${NOTES[$i]}" || true)"
    seq="$(grep -o 'sequence slots: \[[^]]*\]' <<< "${NOTES[$i]}" || true)"
    [[ -n "$rr" ]] && echo "PRIORITY_RR: $rr"
    [[ -n "$seq" ]] && echo "PRIORITY_RR: $seq"
  fi
done

if (( RUN_FAIL == 0 && NOT_RUN == 0 )); then echo "Overall    : PASS"
elif (( RUN_FAIL > 0 )); then echo "Overall    : FAIL"
else echo "Overall    : PARTIAL"; fi

echo "Report     : $REPORT"
echo "Raw logs   : $RAW"
if [[ -z "$ONLY_CASE" ]]; then
  echo "Release evidence: manually copy/move this run to validation/stm32/releases/vX.Y.Z/."
else
  echo "Filtered diagnostic run: not complete release qualification evidence."
fi
echo "============================================================"

(( RUN_FAIL == 0 ))