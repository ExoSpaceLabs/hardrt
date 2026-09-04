#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <cxx-compiler> <hardrt-source-dir>" >&2
  exit 2
fi

cxx="$1"
src="$2"
common=(
  -std=c++17
  -fsyntax-only
  -I"$src/cpp"
  -I"$src/inc"
)

echo "[cpp-contract] positive trivially-copyable queue payload"
"$cxx" "${common[@]}" "$src/tests/cpp_contracts/queue_ok.cpp"

expect_failure() {
  local source="$1"
  local marker="$2"
  local log
  log="$(mktemp)"
  if "$cxx" "${common[@]}" "$src/tests/cpp_contracts/$source" >"$log" 2>&1; then
    echo "expected compile failure but $source compiled successfully" >&2
    cat "$log" >&2
    rm -f "$log"
    exit 1
  fi
  if ! grep -Fq "$marker" "$log"; then
    echo "compile failed for $source, but not for the expected contract" >&2
    cat "$log" >&2
    rm -f "$log"
    exit 1
  fi
  rm -f "$log"
}

echo "[cpp-contract] QueueRef rejects non-trivially-copyable payload"
expect_failure queue_ref_nontrivial_fail.cpp \
  "QueueRef<T>: T must be trivially copyable because HardRT queues use memcpy"

echo "[cpp-contract] StaticQueue rejects non-trivially-copyable payload"
expect_failure static_queue_nontrivial_fail.cpp \
  "StaticQueue<T, Capacity>: T must be trivially copyable because HardRT queues use memcpy"

echo "[cpp-contract] StaticQueue rejects capacity outside uint16_t range"
expect_failure static_queue_capacity_fail.cpp \
  "StaticQueue<T, Capacity>: Capacity exceeds the uint16_t C queue capacity contract"

echo "[cpp-contract] PASS"
