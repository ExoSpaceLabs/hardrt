#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CUBE_ROOT=""
SAMPLES=10000
OUT="$ROOT/validation/stm32/sync_benchmarks"
GDB_BIN=""
OPENOCD_BIN="${OPENOCD_BIN:-openocd}"
OPENOCD_SCRIPTS="${OPENOCD_SCRIPTS:-/usr/share/openocd/scripts}"
SERVER_PID=""
TIMEOUT=120
usage() {
  echo "Usage: $0 /path/to/STM32CubeH7 [--samples 1..100000] [--output-dir DIR]"
  echo "Separate diagnostic campaign. Never replaces the 38-image release benchmark matrix."
}
while (( $# )); do
  case "$1" in
    --samples) SAMPLES="$2"; shift 2 ;;
    --output-dir) OUT="$2"; shift 2 ;;
    --debug-timeout) TIMEOUT="$2"; shift 2 ;;
    --stm32h7-root) CUBE_ROOT="$2"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    --*) echo "Unknown argument: $1" >&2; exit 2 ;;
    *) [[ -z "$CUBE_ROOT" ]] || { echo "Extra argument: $1" >&2; exit 2; }; CUBE_ROOT="$1"; shift ;;
  esac
done
[[ -n "$CUBE_ROOT" && -d "$CUBE_ROOT/Drivers/CMSIS/Core/Include" ]] || { usage >&2; exit 2; }
[[ "$SAMPLES" =~ ^[1-9][0-9]*$ ]] && (( SAMPLES <= 100000 )) || { echo "Invalid sample count" >&2; exit 2; }
[[ "$TIMEOUT" =~ ^[1-9][0-9]*$ ]] || { echo "Invalid timeout" >&2; exit 2; }
for cmd in git cmake "$OPENOCD_BIN" arm-none-eabi-gcc timeout awk grep tee; do
  command -v "$cmd" >/dev/null || { echo "Missing command: $cmd" >&2; exit 2; }
done
if command -v gdb-multiarch >/dev/null; then GDB_BIN=gdb-multiarch
elif command -v arm-none-eabi-gdb >/dev/null; then GDB_BIN=arm-none-eabi-gdb
else echo "Missing gdb-multiarch / arm-none-eabi-gdb" >&2; exit 2
fi
export STM32CUBE_H7_ROOT="$(cd "$CUBE_ROOT" && pwd)"
cd "$ROOT"
SHA="$(git rev-parse HEAD)"
SHORT="$(git rev-parse --short=8 HEAD)"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
RUN="$OUT/${STAMP}_${SHORT}"
mkdir -p "$RUN/raw"
RAW="$RUN/raw"
cleanup() {
  if [[ -n "$SERVER_PID" ]]; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    SERVER_PID=""
  fi
}
trap cleanup EXIT INT TERM
echo "[BENCH] source=$SHA"
echo "[BENCH] samples=$SAMPLES"
echo "[BENCH] output=$RUN"
"$OPENOCD_BIN" -s "$OPENOCD_SCRIPTS" -f "$ROOT/scripts/openocd_h755.cfg" \
  -c "init; targets; shutdown" 2>&1 | tee "$RAW/probe.log"
echo "[BENCH] compiling and flashing diagnostic image"
"$ROOT/scripts/build-lib-stm32h7xx-sync-benchmark.sh" --samples "$SAMPLES" \
  2>&1 | tee "$RAW/build_flash.log"
ELF="$ROOT/examples/hardrt_h755_sync_benchmark/build-cortex_m/hardrt_h755_sync_benchmark.elf"
[[ -s "$ELF" ]] || { echo "ELF not produced" >&2; exit 1; }
"$OPENOCD_BIN" -s "$OPENOCD_SCRIPTS" -f "$ROOT/scripts/openocd_h755.cfg" \
  -c "init; reset halt" > "$RAW/openocd.log" 2>&1 &
SERVER_PID=$!
started=0
for (( i=0; i<100; i++ )); do
  if grep -q "Listening on port 3333 for gdb connections" "$RAW/openocd.log"; then started=1; break; fi
  kill -0 "$SERVER_PID" 2>/dev/null || break
  sleep 0.1
done
if (( ! started )); then cat "$RAW/openocd.log" >&2; exit 1; fi
set +e
timeout "$TIMEOUT" "$GDB_BIN" -q "$ELF" -batch -x "$ROOT/scripts/gdb/sync_benchmark.dbg" \
  2>&1 | tee "$RAW/gdb.log"
rc=${PIPESTATUS[0]}
set -e
cleanup
if (( rc != 0 )); then echo "GDB exited $rc" >&2; exit 1; fi
grep -qx 'RESULT: PASS' "$RAW/gdb.log" || { echo "Benchmark failed" >&2; exit 1; }
[[ "$(grep -c '^BENCH ' "$RAW/gdb.log")" == 14 ]] || { echo "Expected 14 benchmark rows" >&2; exit 1; }

CSV="$RUN/sync_dwt.csv"
awk '
BEGIN { print "id,bytes,count,api_min,api_avg,api_max,masked_min,masked_avg,masked_max" }
/^BENCH / {
  delete field
  for(i=2;i<=NF;i++){ split($i,z,"=");field[z[1]]=z[2] }
  printf "%s,%s,%s,%s,%s,%s,%s,%s,%s\n",field["id"],field["bytes"],field["count"],field["api_min"],field["api_avg"],field["api_max"],field["masked_min"],field["masked_avg"],field["masked_max"]
}' "$RAW/gdb.log" > "$CSV"

REPORT="$RUN/report.md"
{
  echo "# HardRT STM32H755 diagnostic synchronization timing"
  echo
  echo "- UTC: `$(date -u --iso-8601=seconds)`"
  echo "- HardRT SHA: `$SHA`"
  echo "- Tracked source: `$( [[ -z "$(git status --porcelain --untracked-files=no)" ]] && echo clean || echo DIRTY )`"
  echo "- STM32CubeH7 SHA: `$(git -C "$STM32CUBE_H7_ROOT" rev-parse HEAD 2>/dev/null || echo unknown)`"
  echo "- Toolchain: `$(arm-none-eabi-gcc --version | head -n1)`"
  echo "- Board: NUCLEO-H755ZI-Q, CM7, external tick (no active tick ISR)"
  echo "- Samples per operation: $SAMPLES (plus 32 discarded warm-ups)"
  echo "- Profile: HARDRT_CORTEXM_CRIT_PROFILE=ON (diagnostic library)"
  echo "- Raw CSV: `sync_dwt.csv`"
  echo "- Raw debugger/build/probe logs: `raw/`"
  echo "- Result: **PASS**"
  echo
  echo "The masked window begins after raising BASEPRI and ends before restoring"
  echo "it. It excludes boundary instructions and includes profiling overhead."
  echo "API cycles include call/return, BASEPRI entry/exit, and profiling overhead."
  echo "These are non-interfered observations, not production WCET or a"
  echo "numerical mutex blocking-time bound. Contended PI/timeout and IRQ"
  echo "interference characterization are separate outstanding #53 work."
} > "$REPORT"
echo "[BENCH] PASS, 14 metrics x $SAMPLES samples"
echo "[BENCH] report: $REPORT"
echo "[BENCH] CSV: $CSV"
