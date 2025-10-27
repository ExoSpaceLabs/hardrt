# Build & Install

This page covers configuring, building, installing, and consuming HeaRTOS.

## Prerequisites
- A C compiler (GCC/Clang)
- CMake ≥ 3.16
- (Optional) A C++17 compiler if you enable the C++ wrapper

## Configure & build (terminal)
```bash
# clone
git clone https://github.com/<your-username>/heartos.git
cd heartos

# out-of-source build directory
mkdir build && cd build

# choose a port; `null` for a stub, `posix` for a hosted simulation
cmake -DHEARTOS_PORT=posix -DHEARTOS_BUILD_EXAMPLES=ON ..

# build the static library and examples
cmake --build . --target two_tasks -j

# run the example
./examples/two_tasks/two_tasks
```

Expected output (excerpt on POSIX):
```
HeaRTOS version: 0.2.0 (0x000200), port: posix (id=1)
[A] tick count [0]
[B] tock -----
...
```

> Tip: the `null` port is a stub — it doesn’t start a tick or switch contexts. `hrt_start()` returns immediately, so tasks do not run there.

## Building tests (POSIX)
The POSIX test suite builds as a single executable `heartos_tests`.
```bash
cmake -DHEARTOS_PORT=posix -DHEARTOS_BUILD_TESTS=ON ..
cmake --build . --target heartos_tests -j && ./heartos_tests
```
For details, see docs/TESTS_POSIX.md.

## CMake options
| Option | Default | Description |
|---|---|---|
| `HEARTOS_PORT` | `null` | Select build port: `null`, `posix`, or `cortex_m` |
| `HEARTOS_ENABLE_CPP` | `OFF` | Build C++17 header-only wrapper (`heartospp`) |
| `HEARTOS_BUILD_EXAMPLES` | `ON` | Build bundled demo projects |

Example:
```bash
cmake -DHEARTOS_PORT=posix -DHEARTOS_ENABLE_CPP=ON ..
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
- archive `libheartos.a`
- public headers from `inc/`
- generated header `heartos_version.h`
- CMake package files under `lib/cmake/HeaRTOS`

## Consume from another CMake project
```cmake
find_package(HeaRTOS REQUIRED)
add_executable(app main.c)
target_link_libraries(app PRIVATE HeaRTOS::heartos)
```
