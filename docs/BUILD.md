# Build and Install

HardRT 0.5.0 is a C11 CMake project. C++17 and assembly are enabled only when the selected configuration needs them.

```cmake
project(hardrt VERSION 0.5.0 LANGUAGES C)
```

## Requirements

- CMake 3.16 or newer
- a C11 compiler
- a C++17 compiler only with `HARDRT_ENABLE_CPP=ON`
- GNU Arm Embedded (`arm-none-eabi-gcc`) for the supplied Cortex-M toolchain
- `arm-none-eabi-g++` only for Cortex-M builds that enable the C++ wrapper

CI verifies that C-only null, POSIX, and Cortex-M builds do not require a C++ compiler.

## POSIX build

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

Enable the optional C++ wrappers with:

```text
-DHARDRT_ENABLE_CPP=ON
```

The POSIX port uses Linux/glibc `ucontext` and signals. It is a functional/scheduler environment, not a Cortex-M timing model.

## Tests

```bash
cmake -S . -B build-tests \
  -DHARDRT_PORT=posix \
  -DHARDRT_BUILD_TESTS=ON
cmake --build build-tests --target hardrt_tests -j
ctest --test-dir build-tests --output-on-failure
```

Runtime tests are created only for the POSIX port. See [TESTS_POSIX.md](TESTS_POSIX.md).

## Main CMake options

| Option | Default | Meaning |
|---|---:|---|
| `HARDRT_PORT` | `null` | `null`, `posix`, or `cortex_m` |
| `HARDRT_ENABLE_CPP` | `OFF` | Enable/install the header-only C++17 wrapper target |
| `HARDRT_BUILD_EXAMPLES` | `ON` | Build bundled examples |
| `HARDRT_BUILD_TESTS` | `ON` | Enable test configuration; runtime test executable is POSIX-only |
| `HARDRT_STRICT` | `OFF` | Strict POSIX warning set |
| `HARDRT_SANITIZE` | `OFF` | UndefinedBehaviorSanitizer for POSIX validation |
| `HARDRT_STALL_ON_ERROR` | `OFF` | Fatal-error stall where supported; forced OFF on POSIX |
| `HARDRT_DEBUG` | `OFF` | Enable guarded diagnostics |
| `HARDRT_CFG_MAX_TASKS` | `8` | Application-task slots; one additional private idle slot is reserved |
| `HARDRT_CFG_MAX_PRIO` | `4` | Priority classes, zero highest; valid range 1..12 |
| `HARDRT_TIMING_PROFILE` | `none` | Private timing instrumentation profile |
| `HARDRT_TIMING_HOOK_HEADER` | empty | Required only for an active timing profile |

The default task configuration produces:

```text
HARDRT_CFG_MAX_TASKS = 8 application slots
HARDRT_APP_MAX_TASKS = 8 creatable application tasks
HARDRT_MAX_TASKS     = 9 total TCB slots including private idle
HRT_IDLE_ID          = 8 (private/internal)
```

CMake rejects invalid ports, priority counts outside 1..12, task capacities outside 1..254, task capacity smaller than priority-class count, and invalid/incomplete timing-profile configurations.

## Runtime configuration

Build-time CMake options and runtime `hrt_config_t` are separate contracts. `hrt_init()` accepts initialization exactly once and validates:

- non-zero `tick_hz`;
- declared scheduler policy;
- `HRT_TICK_SYSTICK` or `HRT_TICK_EXTERNAL`;
- port-specific representability of the requested tick source/rate.

On Cortex-M SysTick, `core_hz == 0` delegates clock discovery to `hrt_port_get_core_hz()`; a non-zero value explicitly overrides that clock for reload calculation. External-tick configurations do not consume `core_hz`.

Invalid public configuration returns `HRT_ERR_INVALID_CONFIG`; a structurally valid request that the selected port cannot represent returns `HRT_ERR_PORT_INIT`. Failed initialization leaves the kernel UNINITIALIZED so corrected configuration can retry.

## Strict warnings and UBSan

```bash
cmake -S . -B build-strict \
  -DHARDRT_PORT=posix \
  -DHARDRT_BUILD_TESTS=ON \
  -DHARDRT_STRICT=ON \
  -DHARDRT_SANITIZE=ON
cmake --build build-strict -j
ctest --test-dir build-strict --output-on-failure
```

Strict warnings include:

```text
-Wall -Wextra -Wpedantic -Wconversion -Wcast-qual -Wshadow
```

UBSan uses:

```text
-fsanitize=undefined -fno-omit-frame-pointer
```

AddressSanitizer is deliberately excluded because the POSIX port uses `ucontext`.

## Null port

```bash
cmake -S . -B build-null \
  -DHARDRT_PORT=null \
  -DHARDRT_ENABLE_CPP=OFF
cmake --build build-null -j
```

The null port is a build/contract stub. It does not run tasks or a tick.

## Cortex-M library

```bash
cmake -S . -B build-cortex \
  -DHARDRT_PORT=cortex_m \
  -DHARDRT_ENABLE_CPP=OFF \
  -DHARDRT_BUILD_TESTS=OFF \
  -DHARDRT_BUILD_EXAMPLES=OFF \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi.cmake
cmake --build build-cortex -j
```

Add `-DHARDRT_ENABLE_CPP=ON` to expose the C++ target in the cross-build. The STM32H755 board examples have additional CMSIS/HAL dependencies and are exercised by `scripts/build-stm32-examples-ci.sh` and the physical qualification runner.

## Helper scripts

- `scripts/run-all-examples.sh`: configure/build/run POSIX-compatible examples with timeouts.
- `scripts/build-lib-posix.sh`: POSIX library/test helper.
- `scripts/build-stm32-examples-ci.sh`: cross-build the STM32H755 validation matrix.
- `scripts/stm32_manual_test_full.sh`: the single physical STM32H755 qualification entry point.

## Install

```bash
cmake --install build --prefix "$PWD/build/install"
```

The install contains `libhardrt.a`, public C headers, generated `hardrt_version.h` and `hardrt_port.h`, and CMake package files under `lib/cmake/HardRT`. When C++ is enabled, the wrapper headers and `HardRT::hardrtpp` are installed as well.

Kernel/port-private headers are not installed.

## Consume with CMake

C:

```cmake
find_package(HardRT 0.5.0 REQUIRED)
add_executable(app main.c)
target_link_libraries(app PRIVATE HardRT::hardrt)
```

C++ when the package was built with wrappers enabled:

```cmake
find_package(HardRT 0.5.0 REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE HardRT::hardrtpp)
```

The generated package version uses CMake `SameMajorVersion`, but HardRT is pre-1.0 and does not infer ABI stability from that setting. See [COMPATIBILITY.md](COMPATIBILITY.md).
