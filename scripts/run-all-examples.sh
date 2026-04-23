#!/usr/bin/env bash
set -euo pipefail

# This script builds all POSIX examples and runs them individually for a few seconds.
# It excludes examples that target STM32 (Cortex-M).

BUILD_DIR="build-examples-posix"
RUN_TIME="2s"

echo "=== Configuring and Building POSIX Examples ==="
rm -rf "$BUILD_DIR"
mkdir "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure for POSIX with examples and C++ enabled
cmake -DHARDRT_PORT=posix \
      -DHARDRT_ENABLE_CPP=ON \
      -DHARDRT_BUILD_EXAMPLES=ON \
      -DHARDRT_BUILD_TESTS=OFF \
      -DHARDRT_STRICT=ON \
      ..

# Build all targets
cmake --build . -j"$(nproc)"

echo ""
echo "=== Running POSIX Examples (each for $RUN_TIME) ==="

# Find all executables in the examples directory within the build folder
# We exclude directory names and focus on the actual executable files.
# Examples in this project are typically in subdirectories of 'examples/'.

# List of known POSIX examples from root CMakeLists.txt
EXAMPLES=(
    "two_tasks"
    "queue_posix"
    "sem_basic"
    "mutex_basic"
    "sem_counting"
    "two_tasks_external"
    "two_tasks_cpp"
    "queue_posix_cpp"
    "sem_basic_cpp"
    "mutex_basic_cpp"
    "sem_counting_cpp"
)

for ex in "${EXAMPLES[@]}"; do
    # Find the executable path. It should be in examples/<name>/<name>
    EX_PATH="examples/$ex/$ex"
    
    if [ -f "$EX_PATH" ]; then
        echo "--> Running $ex..."
        set +e
        timeout "$RUN_TIME" "./$EX_PATH"
        EXIT_CODE=$?
        set -e
        
        # timeout returns 124 if it killed the process, which is expected
        if [ $EXIT_CODE -eq 124 ] || [ $EXIT_CODE -eq 0 ]; then
            echo "    [OK] $ex finished/timed out as expected."
        else
            echo "    [ERROR] $ex exited with code $EXIT_CODE"
            exit 1
        fi
    else
        echo "    [SKIP] Executable for $ex not found at $EX_PATH"
    fi
    echo ""
done

echo "=== All POSIX examples verified successfully ==="
