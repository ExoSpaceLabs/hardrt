# Build and Install

HardRT 0.5.1 is a C11 CMake project. C++17 and assembly are enabled only when the selected configuration needs them.

```cmake
project(hardrt VERSION 0.5.1 LANGUAGES C)
```

## Requirements

- CMake 3.16 or newer
- a C11 compiler
- a C++17 compiler only with `HARDRT_ENABLE_CPP=ON`
- a pthread-capable Linux environment for the hosted POSIX port
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

The POSIX port is a Linux hosted functional/scheduler-validation environment. HardRT application tasks execute in pthreads; the common HardRT core remains authoritative for task state and scheduling. Targeted POSIX signals are used to park/resume hosted tasks so CPU-bound task code can be preempted without first calling a HardRT API. This is not a Cortex-M timing model and it is excluded from hard-real-time timing claims.

A POSIX build resolves CMake's `Threads` package and publishes `Threads::Threads` through `HardRT::hardrt`. Source-tree and installed-package consumers therefore receive the required thread dependency transitively.

The hosted port currently reserves process-wide `SIGALRM` and `SIGUSR2` while HardRT is active. Applications using the hosted port must not install incompatible handlers or repurpose those signals concurrently.

The `stack_words` pointer and `n_words` count supplied to `hrt_create_task()` remain application-owned and participate in HardRT's lifetime and overlap validation. On POSIX that storage is not used as the native pthread execution stack; the host pthread owns a separate execution stack. Native Cortex-M task-stack semantics are unchanged.

## Tests

```bash
cmake -S . -B build-tests \
  -DHARDRT_PORT=posix \
  -DHARDRT_BUILD_TESTS=ON
cmake --build build-tests --target hardrt_tests -j
ctest --test-dir build-tests --output-on-failure
```

Runtime tests are created only for the POSIX port. The hosted suite includes a regression in which a low-priority task executes a CPU-bound infinite loop without entering HardRT while a higher-priority sleeping task must still wake and run. See [TESTS_POSIX.md](TESTS_POSIX.md).

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

UBSan is the sanitizer lane enabled by `HARDRT_SANITIZE` and exercised by the release CI. AddressSanitizer is not currently part of the release gate; no ASan compatibility claim is made for the pthread/signal hosted runtime until it has dedicated validation.

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
- `scripts/extract_release_notes.py`: extract the matching `X.Y.Z` section from `RELEASE_NOTES.md` for GitHub Release publication.
- `scripts/check_release_qualification_diff.py`: reject post-qualification changes outside the release-automation/documentation whitelist.

## Install

```bash
cmake --install build --prefix "$PWD/build/install"
```

The install contains `libhardrt.a`, public C headers, generated `hardrt_version.h` and `hardrt_port.h`, and CMake package files under `lib/cmake/HardRT`. When C++ is enabled, the wrapper headers and `HardRT::hardrtpp` are installed as well.

Kernel/port-private headers are not installed.

## Consume with CMake

C:

```cmake
find_package(HardRT 0.5 REQUIRED)
add_executable(app main.c)
target_link_libraries(app PRIVATE HardRT::hardrt)
```

C++ when the package was built with wrappers enabled:

```cmake
find_package(HardRT 0.5 REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE HardRT::hardrtpp)
```

POSIX consumers must not add a duplicate private pthread workaround. `HardRT::hardrt` exposes the thread dependency as part of the package contract.

The generated package version uses CMake `SameMinorVersion`. For pre-1.0 releases this deliberately keeps package resolution within the same minor line: a 0.5.x package may satisfy a compatible 0.5 request, but a 0.5.x package must not silently satisfy a `find_package(HardRT 0.4...)` request. Package target names remain stable, while source/behavior/ABI compatibility across pre-1.0 minor releases is governed separately by [COMPATIBILITY.md](COMPATIBILITY.md).

## Tag-driven release build

Pushing a non-v-prefixed `X.Y.Z` tag triggers `.github/workflows/release.yml`. Publication is intentionally separate from the Linux CI workflow.

The release workflow requires the tag to match the CMake project version and requires `main`, `develop`, and the tag SHA to be aligned. It then builds POSIX and Cortex-M install trees, validates the installed POSIX consumer, creates reproducible archives, generates `SHA256SUMS`, extracts the matching release-note section, retains the generated package set as an Actions artifact, and publishes the GitHub Release.

The generated software assets are:

```text
hardrt-posix-X.Y.Z.tar.gz
hardrt-cortexm-X.Y.Z.tar.gz
hardrt-bundle-X.Y.Z.tar.gz
SHA256SUMS
```

The physical STM32 qualification package is not generated by CI. It is retained from `scripts/stm32_manual_test_full.sh` and attached separately to the corresponding GitHub Release.

## v0.5.1 corrective-patch gate

Before publishing v0.5.1, CI must demonstrate at least:

- POSIX C and optional C++ builds succeed on the supported Linux matrix;
- the hosted runtime suite passes, including asynchronous CPU-bound preemption;
- strict-warning + UBSan validation passes;
- installed POSIX consumers resolve and link the transitive thread dependency through `find_package(HardRT)`;
- null and Cortex-M build contracts remain unchanged;
- Cortex-M and STM32H755 cross-builds remain green;
- bundled POSIX examples build and self-test successfully;
- documentation/API compile probes and Doxygen remain green.

The published v0.5.0 hardware evidence remains historical evidence and is not reinterpreted as v0.5.1 qualification. Under [QUALIFICATION.md](QUALIFICATION.md), v0.5.1 must pass a fresh **unfiltered** STM32H755 release-candidate run:

```bash
./scripts/stm32_manual_test_full.sh /path/to/STM32CubeH7 --clean-builds
```

The physical run requires board/OpenOCD probe PASS, 13/13 functional PASS, 38/38 benchmark PASS, and Overall PASS. The resulting commit is the hardware-qualified source SHA. If only release automation/documentation changes follow, they may advance the release/tag SHA without repeating hardware qualification only when `scripts/check_release_qualification_diff.py <qualified-sha> <release-sha>` passes and hosted/cross-build/documentation CI remains green. Any other post-qualification change requires a new full hardware run.
