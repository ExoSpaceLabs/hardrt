#!/usr/bin/env bash
set -euo pipefail

# Fresh build directory
rm -rf build
mkdir build && cd build

# Configure for POSIX with examples and tests enabled
cmake -DHEARTOS_PORT=posix -DHEARTOS_BUILD_EXAMPLES=ON -DHEARTOS_BUILD_TESTS=ON ..

# Build tests and example
cmake --build . -j"${NPROC:-$(nproc)}"

# Run the test suite first; if it fails, exit without running the example
# Note: Use either -V or --output-on-failure, not both, to avoid duplicate output on failures.
ctest -V

# If tests passed, run the example
./examples/two_tasks/two_tasks
