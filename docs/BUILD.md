# Build and Install

This page describes the build behavior on the current `develop` branch. The CMake project version remains `0.4.0` until the v0.5.0 release metadata is finalized; the build contracts below describe the implemented v0.5-line behavior.

## Prerequisites

HardRT starts as a C-only CMake project:

```cmake
project(hardrt VERSION 0.4.0 LANGUAGES C)
```

Additional languages are enabled only when the selected build needs them:

- C11 is always required;
- C++17 is required only with `HARDRT_ENABLE_CPP=ON`;
- ASM is enabled only for `HARDRT_PORT=cortex_m`;
- CMake 3.16 or newer is required.

For Cortex-M builds, the supplied GNU Arm Embedded toolchain requires `arm-none-eabi-gcc`. `arm-none-eabi-g++` is required only when `HARDRT_ENABLE_CPP=ON`. The Cortex-M assembly sources are compiled through GCC with assembler preprocessing enabled.

CI explicitly verifies that C-only null, POSIX, and Cortex-M builds do not require a C++ compiler.

## Configure and build the POSIX port

C-only:

```bash
git clone https://github.com/ExoSpaceLabs/hardrt.git
cd hardrt

cmake -S . -B build \
  -DHARDRT_PORT=posix \
  -DHARDRT_ENABLE_CPP=OFF \
  -DHARDRT_BUILD_EXAMPLES=ON
cmake --build build -j
./build/examples/two_tasks/two_tasks
```

With the optional C++ wrappers:

```bash
cmake -S . -B build-cpp \
  -DHARDRT_PORT=posix \
  -DHARDRT_ENABLE_CPP=ON
cmake --build build-cpp -j
```

Representative output begins with the selected version and port. Task interleaving depends on task configuration and scheduling points and should not be treated as a fixed output transcript.

The POSIX port uses Linux/glibc `ucontext` plus signals. It is intended for logic and scheduler testing rather than timing validation.

## Build and run tests

```bash
cmake -S . -B build-tests \
  -DHARDRT_PORT=posix \
  -DHARDRT_BUILD_TESTS=ON
cmake --build build-tests --target hardrt_tests -j
ctest --test-dir build-tests --output-on-failure
```

The runtime test target is created only for the POSIX port. With another port selected, CMake reports that the runtime test target is skipped.

See [TESTS_POSIX.md](TESTS_POSIX.md).

## CMake options

| Option | Default | Effective behavior |
|---|---:|---|
| `HARDRT_PORT` | `null` | Selects `null`, `posix`, or `cortex_m`; any other value is a configure error. |
| `HARDRT_ENABLE_CPP` | `OFF` | Enables CXX, adds the header-only `hardrtpp` interface target, and installs the C++ wrapper. CXX is not enabled when this option is OFF. |
| `HARDRT_BUILD_EXAMPLES` | `ON` | Adds bundled example targets. C++ examples are added only when the wrapper is enabled. |
| `HARDRT_BUILD_TESTS` | `ON` | Includes the test configuration. A runnable `hardrt_tests` target is created only for `posix`. |
| `HARDRT_STRICT` | `OFF` | Adds strict warning flags for POSIX builds. |
| `HARDRT_SANITIZE` | `OFF` | Enables UndefinedBehaviorSanitizer for the POSIX test configuration. AddressSanitizer is deliberately not enabled because of `ucontext`. |
| `HARDRT_STALL_ON_ERROR` | `OFF` | Enables fatal-error stalling where supported. POSIX does not support this mode: requesting ON is force-normalized to effective OFF and the public compile definition is `HARDRT_STALL_ON_ERROR=0`. |
| `HARDRT_DEBUG` | `OFF` | Publishes `HARDRT_DEBUG=0` or `1` and enables guarded diagnostic checks/variables. |
| `HARDRT_CFG_MAX_TASKS` | `8` | Number of application task slots. The kernel allocates one additional private idle slot. |
| `HARDRT_CFG_MAX_PRIO` | `4` | Number of priority classes, with priority zero highest. Valid range is 1 through 12. |
| `HARDRT_TIMING_PROFILE` | `none` | Private timing instrumentation profile. Production/default `none` emits no timing-hook code. |
| `HARDRT_TIMING_HOOK_HEADER` | empty | Required only by an active timing profile such as `ipc`. |

CMake status output reports the effective values after port-specific normalization rather than merely echoing the originally requested cache values.

## Configuration validation

CMake rejects configurations where:

- `HARDRT_PORT` is not `null`, `posix`, or `cortex_m`;
- `HARDRT_CFG_MAX_PRIO` is outside `[1, 12]`;
- `HARDRT_CFG_MAX_TASKS < 1`;
- `HARDRT_CFG_MAX_TASKS > 254`;
- `HARDRT_CFG_MAX_TASKS < HARDRT_CFG_MAX_PRIO`;
- an active timing profile is unknown or lacks its required hook header.

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

`HARDRT_STRICT` enables:

```text
-Wall -Wextra -Wpedantic -Wconversion -Wcast-qual -Wshadow
```

`HARDRT_SANITIZE` enables:

```text
-fsanitize=undefined -fno-omit-frame-pointer
```

It does not enable AddressSanitizer.

## Null port

```bash
cmake -S . -B build-null \
  -DHARDRT_PORT=null \
  -DHARDRT_ENABLE_CPP=OFF
cmake --build build-null -j
```

The null port provides build-time stubs and does not require CXX or ASM. It does not start a tick or transfer into task contexts.

## Cortex-M library build

C-only Cortex-M library:

```bash
cmake -S . -B build-cortex \
  -DHARDRT_PORT=cortex_m \
  -DHARDRT_ENABLE_CPP=OFF \
  -DHARDRT_BUILD_TESTS=OFF \
  -DHARDRT_BUILD_EXAMPLES=OFF \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi.cmake
cmake --build build-cortex -j
```

This requires `arm-none-eabi-gcc` but not `arm-none-eabi-g++`.

To expose the C++ wrapper target in the same cross-build, add:

```text
-DHARDRT_ENABLE_CPP=ON
```

That configuration additionally requires `arm-none-eabi-g++`.

Use the supplied toolchain or an application-specific toolchain that provides the expected CPU and linker settings. The library port sources include ARM assembly and reference linker-provided RAM boundary symbols.

The STM32H755 examples have board/vendor dependencies and are validated separately by the repository's cross-build and hardware qualification flows.

## Helper scripts

- `scripts/run-all-examples.sh` configures, builds, and runs POSIX-compatible examples under timeouts.
- `scripts/build-lib-posix.sh` configures a POSIX build, runs tests, and launches its example path.
- `scripts/build-stm32-examples-ci.sh` cross-builds the STM32H755 example matrix used in CI.

Inspect scripts before using them in a different build layout; they encode the repository's current target names and assumptions.

## Install

```bash
cmake --install build --prefix "$PWD/build/install"
```

The install contains:

- `libhardrt.a`;
- public headers from `inc/`, excluding kernel-private headers;
- generated `hardrt_version.h` and `hardrt_port.h`;
- CMake package files under `lib/cmake/HardRT`;
- `hardrtpp.hpp` and `HardRT::hardrtpp` when `HARDRT_ENABLE_CPP=ON`.

CI builds downstream C and C++ consumers against the installed package so the exported API is checked independently of in-tree include paths.

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
