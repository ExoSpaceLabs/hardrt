# Build & Install

This page covers configuring, building, installing, and consuming HardRT.

## Prerequisites
- A C compiler (GCC/Clang)
- CMake ≥ 3.16
- (Optional) A C++17 compiler if you enable the C++ wrapper

## Configure & build (terminal)
```bash
# clone
git clone https://github.com/<your-username>/hardrt.git
cd hardrt

# out-of-source build directory
mkdir build && cd build

# choose a port; `null` for a stub, `posix` for a hosted simulation
cmake -DHARDRT_PORT=posix -DHARDRT_BUILD_EXAMPLES=ON ..

# build the static library and examples
cmake --build . --target two_tasks -j

# run the example
./examples/two_tasks/two_tasks
```

Expected output (excerpt on POSIX):
```
HardRT version: 0.3.0 (0x000300), port: posix (id=1)
[A] tick count [0]
[B] tock -----
...
```

> Tip: the `null` port is a stub — it doesn’t start a tick or switch contexts. `hrt_start()` returns immediately, so tasks do not run there.

## Building tests (POSIX)
The POSIX test suite builds as a single executable `hardrt_tests`.
```bash
cmake -DHARDRT_PORT=posix -DHARDRT_BUILD_TESTS=ON ..
cmake --build . --target hardrt_tests -j && ./hardrt_tests
```
For details, see docs/TESTS_POSIX.md.

## CMake options
| Option                  | Default | Description                                                                             |
|-------------------------|---------|-----------------------------------------------------------------------------------------|
| `HARDRT_PORT`           | `null`  | Select build port: `null`, `posix`, or `cortex_m`                                       |
| `HARDRT_ENABLE_CPP`     | `OFF`   | Build C++17 header-only wrapper (`hardrtpp`)                                            |
| `HARDRT_BUILD_EXAMPLES` | `ON`    | Build bundled demo projects                                                             |
| `HARDRT_STRICT`         | `OFF`   | Enable strict warnings on POSIX builds                                                  |
| `HARDRT_SANITIZE`       | `OFF`   | Enable ASan/UBSan on POSIX tests                                                        |
| `HARDRT_STALL_ON_ERROR` | `OFF`   | Stalls in an infinite loop if an error occurs.                                          |
| `HARDRT_DEBUG`          | `OFF`   | Enables debug settings of debug variables (see [debub bariables](#Debug-variables).     |
| `HARDRT_CFG_MAX_TASKS`  | `8`     | Maximum number of concurrent tasks supported by the kernel (maps to `HARDRT_MAX_TASKS`) |
| `HARDRT_CFG_MAX_PRIO`   | `4`     | Number of scheduler priority classes (0..N-1; maps to `HARDRT_MAX_PRIO`)                |

Constraints and notes:
- Priority levels have a physical cap of 12 in this release (`HRT_PRIO0..HRT_PRIO11`).
- There is no hard cap on the number of tasks in the source; practical limits depend on your system/memory.
- CMake validates at configuration time: `HARDRT_CFG_MAX_PRIO` must be in [1, 12] and `HARDRT_CFG_MAX_TASKS >= HARDRT_CFG_MAX_PRIO` (and `>= 1`).
- `HARDRTT_STALL_ON_ERROR` is hard disabled for `posix` port as it breaks ctests.

Examples:
```bash
# POSIX tests with 12 priority levels and 12 tasks
cmake -DHARDRT_PORT=posix -DHARDRT_BUILD_TESTS=ON \
      -DHARDRT_CFG_MAX_PRIO=12 -DHARDRT_CFG_MAX_TASKS=12 ..
cmake --build . --target hardrt_tests -j && ./hardrt_tests

# Defaults (4 priorities, 8 tasks) with C++ wrapper
cmake -DHARDRT_PORT=posix -DHARDRT_ENABLE_CPP=ON ..
```

## Install (optional)
Install the library, headers, and CMake package files to a prefix:
```bash
# from your build directory
cmake --install . --prefix "$PWD/install"
# or system-wide
sudo cmake --install .
```
This installs:
- archive `libhardrt.a`
- public headers from `inc/`
- generated header `hardrt_version.h`
- CMake package files under `lib/cmake/HardRT`

## Consume from another CMake project
```cmake
find_package(HardRT REQUIRED)
add_executable(app main.c)
target_link_libraries(app PRIVATE HardRT::hardrt)
```

## Debug variables

...