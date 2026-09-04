# Build and Install

This page describes the CMake behavior present in HardRT v0.4.0 on `develop`.

## Prerequisites

The root project currently declares `C`, `CXX`, and `ASM` unconditionally:

```cmake
project(hardrt VERSION 0.4.0 LANGUAGES C CXX ASM)
```

A normal configure therefore requires:

- a C compiler with C11 support;
- a C++ compiler, even when `HARDRT_ENABLE_CPP=OFF`;
- an assembler supported by the selected CMake generator/toolchain;
- CMake 3.16 or newer.

For Cortex-M builds, the supplied toolchain expects the GNU Arm Embedded tools. The exact compiler requirements are defined by `cmake/toolchains/arm-none-eabi.cmake`.

## Configure and build the POSIX port

```bash
git clone https://github.com/ExoSpaceLabs/hardrt.git
cd hardrt

cmake -S . -B build \
  -DHARDRT_PORT=posix \
  -DHARDRT_BUILD_EXAMPLES=ON
cmake --build build -j
./build/examples/two_tasks/two_tasks
```

Representative output begins with the selected version and port. Task interleaving is dependent on task configuration and scheduling points and should not be treated as a fixed output transcript.

The POSIX port requires Linux/glibc `ucontext`. It is intended for logic and scheduler testing rather than timing validation.

## Build and run tests

```bash
cmake -S . -B build-tests \
  -DHARDRT_PORT=posix \
  -DHARDRT_BUILD_TESTS=ON
cmake --build build-tests --target hardrt_tests -j
ctest --test-dir build-tests --output-on-failure
```

The test target is created only for the POSIX port. With another port selected, CMake reports that the runtime test target is skipped.

See [TESTS_POSIX.md](TESTS_POSIX.md).

## CMake options

| Option | Default | Current behavior |
|---|---:|---|
| `HARDRT_PORT` | `null` | Selects `null`, `posix`, or `cortex_m`. |
| `HARDRT_ENABLE_CPP` | `OFF` | Adds and installs the header-only `hardrtpp` interface target. The root configure still requires a C++ compiler because CXX is always enabled. |
| `HARDRT_BUILD_EXAMPLES` | `ON` | Adds bundled example targets. C++ examples are added only when the wrapper is enabled. |
| `HARDRT_BUILD_TESTS` | `ON` | Includes the test CMake file. A runnable `hardrt_tests` target is created only for `posix`. |
| `HARDRT_STRICT` | `OFF` | Adds strict warning flags globally for POSIX builds. |
| `HARDRT_SANITIZE` | `OFF` | Enables UndefinedBehaviorSanitizer for the POSIX test configuration. AddressSanitizer is deliberately not enabled because of `ucontext`. |
| `HARDRT_STALL_ON_ERROR` | `OFF` | Publishes `HARDRT_STALL_ON_ERROR` as a compile definition. Keep this OFF for POSIX. The current POSIX warning path assigns a differently named CMake variable and does not reliably override an ON value. |
| `HARDRT_DEBUG` | `OFF` | Publishes `HARDRT_DEBUG=0` or `1` and enables debug variables/checks in guarded code. |
| `HARDRT_CFG_MAX_TASKS` | `8` | Number of application task slots. The kernel allocates one additional private idle slot. Public waiter storage is sized only for application tasks. |
| `HARDRT_CFG_MAX_PRIO` | `4` | Number of priority queues, with priority zero highest. Valid range is 1 through 12. |

## Configuration validation

CMake currently rejects configurations where:

- `HARDRT_CFG_MAX_PRIO` is outside `[1, 12]`;
- `HARDRT_CFG_MAX_TASKS < 1`;
- `HARDRT_CFG_MAX_TASKS > 254`;
- `HARDRT_CFG_MAX_TASKS < HARDRT_CFG_MAX_PRIO`.

For a CMake build with the defaults:

```text
HARDRT_CFG_MAX_TASKS = 8 application slots
HARDRT_APP_MAX_TASKS = 8 creatable application tasks
HARDRT_MAX_TASKS     = 9 total TCB slots, including one private idle slot
HRT_IDLE_ID          = 8 (private/internal)
```

`HARDRT_MAX_TASKS` remains the legacy total-slot compatibility macro. Application code that needs the number of creatable tasks should use `HARDRT_APP_MAX_TASKS`. Direct/non-CMake builds that define only the legacy `HARDRT_MAX_TASKS` value infer application capacity as `HARDRT_MAX_TASKS - 1`; with neither macro supplied, the fallback is 8 application tasks plus one idle slot.

## Strict warnings and sanitizers

```bash
cmake -S . -B build-strict \
  -DHARDRT_PORT=posix \
  -DHARDRT_BUILD_TESTS=ON \
  -DHARDRT_STRICT=ON \
  -DHARDRT_SANITIZE=ON
cmake --build build-strict -j
ctest --test-dir build-strict --output-on-failure
```

`HARDRT_STRICT` currently enables:

```text
-Wall -Wextra -Wpedantic -Wconversion -Wcast-qual -Wshadow
```

`HARDRT_SANITIZE` currently enables:

```text
-fsanitize=undefined -fno-omit-frame-pointer
```

It does not enable AddressSanitizer.

## C++ wrapper build

```bash
cmake -S . -B build-cpp \
  -DHARDRT_PORT=posix \
  -DHARDRT_ENABLE_CPP=ON
cmake --build build-cpp -j
```

The C++ wrapper is an interface target and does not compile a separate wrapper library.

## Null port

```bash
cmake -S . -B build-null -DHARDRT_PORT=null
cmake --build build-null -j
```

The null port provides build-time stubs. It does not start a tick or transfer into task contexts. `hrt_start()` returns.

## Cortex-M library build

Use the supplied toolchain or an application-specific toolchain that provides the expected CPU and linker settings. The library port sources include ARM assembly and reference linker-provided RAM boundary symbols.

The Cortex-M examples may have additional board/vendor dependencies. In particular, the timing example is not a generic host-build target.

## Helper scripts

- `scripts/run-all-examples.sh` configures, builds, and runs POSIX-compatible examples under timeouts.
- `scripts/build-lib-posix.sh` configures a POSIX build, runs tests, and launches its example path.

Inspect scripts before using them in a different build layout; they encode the repository's current target names and assumptions.

## Install

```bash
cmake --install build --prefix "$PWD/build/install"
```

The install contains:

- `libhardrt.a`;
- public headers from `inc/`, excluding `hardrt_port_int.h`;
- generated `hardrt_version.h` and `hardrt_port.h`;
- CMake package files under `lib/cmake/HardRT`;
- `hardrtpp.hpp` when `HARDRT_ENABLE_CPP=ON`.

## Consume from another CMake project

C application:

```cmake
find_package(HardRT 0.4.0 REQUIRED)
add_executable(app main.c)
target_link_libraries(app PRIVATE HardRT::hardrt)
```

C++ wrapper application, when the installed package includes it:

```cmake
find_package(HardRT 0.4.0 REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE HardRT::hardrtpp)
```

Package compatibility is generated with CMake's `SameMajorVersion` policy. Since the project major version is zero, consumers should still review release notes for source, ABI, and behavioral changes between minor releases.