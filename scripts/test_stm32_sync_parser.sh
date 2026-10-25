#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
AWK="$ROOT/scripts/parse_stm32_sync_benchmark.awk"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

{
  echo 'core_hz=64000000 samples=8 rows=14 profile=1 error=0'
  for (( i=0; i<14; ++i )); do
    case "$i" in
      0|1|2|3) bytes=0 ;;
      4|5) bytes=4 ;;
      6|7) bytes=16 ;;
      8|9) bytes=64 ;;
      10|11) bytes=256 ;;
      12|13) bytes=1024 ;;
    esac
    echo "BENCH id=$i bytes=$bytes count=8 api_min=100 api_avg=120 api_max=150 masked_min=30 masked_avg=50 masked_max=80"
  done
  echo 'RESULT: PASS'
} > "$TMP/valid.log"

parse() { awk -v target="$1" -v csv="$TMP/out.csv" -f "$AWK" "$2" >/dev/null 2>&1; }
parse 8 "$TMP/valid.log" || { echo "FAIL: valid benchmark report rejected" >&2; exit 1; }
[[ "$(wc -l < "$TMP/out.csv")" == 15 ]] || { echo "FAIL: expected 14 CSV data rows" >&2; exit 1; }

if parse 9 "$TMP/valid.log"; then echo "FAIL: wrong samples accepted" >&2; exit 1; fi
grep -v '^BENCH id=13 ' "$TMP/valid.log" > "$TMP/missing.log"
if parse 8 "$TMP/missing.log"; then echo "FAIL: missing metric accepted" >&2; exit 1; fi
sed 's/BENCH id=7 bytes=16/BENCH id=7 bytes=64/' "$TMP/valid.log" > "$TMP/bytes.log"
if parse 8 "$TMP/bytes.log"; then echo "FAIL: wrong byte size accepted" >&2; exit 1; fi
sed 's/BENCH id=6 bytes=16 count=8/BENCH id=5 bytes=16 count=8/' "$TMP/valid.log" > "$TMP/id.log"
if parse 8 "$TMP/id.log"; then echo "FAIL: duplicate/out-of-order metric accepted" >&2; exit 1; fi
sed 's/api_min=100 api_avg=120/api_min=130 api_avg=120/' "$TMP/valid.log" > "$TMP/range.log"
if parse 8 "$TMP/range.log"; then echo "FAIL: invalid min/avg accepted" >&2; exit 1; fi
sed 's/rows=14 profile=1/rows=14 profile=0/' "$TMP/valid.log" > "$TMP/profile.log"
if parse 8 "$TMP/profile.log"; then echo "FAIL: unprofiled kernel accepted" >&2; exit 1; fi
sed 's/RESULT: PASS/RESULT: FAIL/' "$TMP/valid.log" > "$TMP/failure.log"
if parse 8 "$TMP/failure.log"; then echo "FAIL: firmware FAIL accepted" >&2; exit 1; fi
echo "PASS: strict synchronization parser accepts complete evidence and rejects 7 malformed/partial cases"
