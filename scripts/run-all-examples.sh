#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${HARDRT_EXAMPLE_BUILD_DIR:-$ROOT_DIR/build-examples-posix}"
RUN_TIME="${HARDRT_EXAMPLE_TIMEOUT:-3s}"
LOG_DIR="$BUILD_DIR/self-test-logs"

fail() {
    echo "[FAIL] $*" >&2
    exit 1
}

require_count() {
    local log_file="$1"
    local pattern="$2"
    local minimum="$3"
    local description="$4"
    local count

    count=$(grep -cE -- "$pattern" "$log_file" || true)
    if (( count < minimum )); then
        echo "[FAIL] $description: expected >= $minimum, observed $count" >&2
        echo "----- captured output -----" >&2
        cat "$log_file" >&2
        echo "---------------------------" >&2
        exit 1
    fi
    printf '    [OK] %-34s %d/%d\n' "$description" "$count" "$minimum"
}

run_example() {
    local example="$1"
    shift

    local exe="$BUILD_DIR/examples/$example/$example"
    local log_file="$LOG_DIR/$example.log"

    [[ -x "$exe" ]] || fail "expected executable not found: $exe"

    echo "--> Running $example (bounded by $RUN_TIME)"
    set +e
    timeout --signal=TERM --kill-after=1s "$RUN_TIME" \
        stdbuf -oL -eL "$exe" >"$log_file" 2>&1
    local rc=$?
    set -e

    if [[ $rc -ne 0 && $rc -ne 124 ]]; then
        echo "[FAIL] $example exited unexpectedly with code $rc" >&2
        cat "$log_file" >&2
        exit 1
    fi

    if grep -Eiq -- 'HardRT init failed|create .* failed|Failed to start tick thread|Segmentation fault|Aborted|Assertion.*failed' "$log_file"; then
        echo "[FAIL] $example reported an initialization/runtime failure" >&2
        cat "$log_file" >&2
        exit 1
    fi

    while (( $# > 0 )); do
        [[ $# -ge 3 ]] || fail "internal harness error for $example: marker arguments must be triples"
        require_count "$log_file" "$1" "$2" "$3"
        shift 3
    done

    if [[ $rc -eq 124 ]]; then
        echo "    [OK] progress criteria met before bounded timeout"
    else
        echo "    [OK] exited successfully after meeting progress criteria"
    fi
}

echo "=== Building POSIX C/C++ examples ==="
rm -rf "$BUILD_DIR"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DHARDRT_PORT=posix \
    -DHARDRT_ENABLE_CPP=ON \
    -DHARDRT_BUILD_EXAMPLES=ON \
    -DHARDRT_BUILD_TESTS=OFF \
    -DHARDRT_STRICT=ON
cmake --build "$BUILD_DIR" --parallel
mkdir -p "$LOG_DIR"

CXX_COMPILER="$(awk -F= '/^CMAKE_CXX_COMPILER:FILEPATH=/{print $2; exit}' "$BUILD_DIR/CMakeCache.txt")"
[[ -n "$CXX_COMPILER" ]] || fail "could not resolve CMAKE_CXX_COMPILER from $BUILD_DIR/CMakeCache.txt"

echo
echo "=== Validating C++ compile-time contracts ==="
bash "$ROOT_DIR/tests/cpp_contracts/check.sh" "$CXX_COMPILER" "$ROOT_DIR"

echo
echo "=== Executing POSIX examples with progress checks ==="
run_example two_tasks \
    '^\[A\] tick count' 2 'task A iterations' \
    '^\[B\] tock' 2 'task B iterations'

run_example queue_posix \
    '^\[producer\] sent' 3 'producer sends' \
    '^\[consumer\] got' 2 'consumer receives'

run_example sem_basic \
    '^\[A\] got sem' 2 'task A semaphore takes' \
    '^\[B\] (got sem|waiting)' 2 'task B semaphore activity'

run_example mutex_basic \
    '^\[A\] locked mutex' 2 'task A mutex locks' \
    '^\[B\] ' 2 'task B mutex activity'

run_example sem_counting \
    '^\[P\] burst give x3' 2 'producer token bursts' \
    '^\[C\] took token' 2 'consumer token takes'

run_example two_tasks_external \
    '^\[A\] External tick' 2 'external-tick task A iterations' \
    '^\[B\] External tock' 2 'external-tick task B iterations'

run_example two_tasks_cpp \
    '^\[A\] tick count' 2 'C++ task A iterations' \
    '^\[B\] tock' 2 'C++ task B iterations'

run_example queue_posix_cpp \
    '^\[producer\] sent' 3 'C++ producer sends' \
    '^\[consumer\] got' 2 'C++ consumer receives'

run_example sem_basic_cpp \
    '^\[A\] got sem' 2 'C++ task A semaphore takes' \
    '^\[B\] (got sem|waiting)' 2 'C++ task B semaphore activity'

run_example mutex_basic_cpp \
    '^\[A\] locked mutex' 2 'C++ task A mutex locks' \
    '^\[B\] ' 2 'C++ task B mutex activity'

run_example sem_counting_cpp \
    '^\[P\] burst give x3' 2 'C++ producer token bursts' \
    '^\[C\] took token' 2 'C++ consumer token takes'

echo
echo "=== All POSIX examples compiled and demonstrated required task progress ==="
