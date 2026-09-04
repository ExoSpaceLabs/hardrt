#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_TYPE="${BUILD_TYPE:-Release}"
GENERATOR="${GENERATOR:-Ninja}"
JOBS="${JOBS:-$(nproc)}"
TOOLCHAIN="${TOOLCHAIN:-$ROOT_DIR/cmake/toolchains/arm-none-eabi.cmake}"
STM32CUBE_H7_ROOT="${STM32CUBE_H7_ROOT:?Set STM32CUBE_H7_ROOT to a valid STM32CubeH7 checkout}"

BASE_BUILD="${HARDRT_CORTEXM_BUILD:-$ROOT_DIR/build-cortex_m}"
BASE_INSTALL="${HARDRT_CORTEXM_INSTALL:-$ROOT_DIR/install-cortexm-ci}"
APP_BUILD_ROOT="${HARDRT_STM32_APP_BUILD_ROOT:-$ROOT_DIR/build-stm32-examples-ci}"

if [[ ! -f "$TOOLCHAIN" ]]; then echo "Missing toolchain file: $TOOLCHAIN" >&2; exit 1; fi
if [[ ! -d "$STM32CUBE_H7_ROOT/Drivers/CMSIS/Core/Include" ]] ||
   [[ ! -d "$STM32CUBE_H7_ROOT/Drivers/CMSIS/Device/ST/STM32H7xx/Include" ]] ||
   [[ ! -d "$STM32CUBE_H7_ROOT/Drivers/STM32H7xx_HAL_Driver/Inc" ]]; then
  echo "STM32CUBE_H7_ROOT is missing required CMSIS/HAL headers: $STM32CUBE_H7_ROOT" >&2; exit 1
fi

bash -n \
  "$ROOT_DIR/scripts/stm32_manual_test_full.sh" \
  "$ROOT_DIR/scripts/build-lib-stm32h7xx-demo.sh" \
  "$ROOT_DIR/scripts/build-lib-stm32h7xx-blinky.sh" \
  "$ROOT_DIR/scripts/build-lib-stm32h7xx-blinky-cpp.sh" \
  "$ROOT_DIR/scripts/build-lib-stm32h7xx-dwt-timing.sh" \
  "$ROOT_DIR/scripts/build-lib-stm32h7xx-tick-benchmark.sh" \
  "$ROOT_DIR/scripts/build-lib-stm32h7xx-preemption.sh" \
  "$ROOT_DIR/scripts/build-lib-stm32h7xx-fpu-validation.sh" \
  "$ROOT_DIR/scripts/build-lib-stm32h7xx-global-rr.sh" \
  "$ROOT_DIR/scripts/build-lib-stm32h7xx-ipc-validation.sh" \
  "$ROOT_DIR/scripts/build-lib-stm32h7xx-external-tick.sh"

configure_library() {
  local build_dir="$1" install_dir="$2"; shift 2
  rm -rf "$build_dir" "$install_dir"
  cmake -S "$ROOT_DIR" -B "$build_dir" -G "$GENERATOR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DHARDRT_PORT=cortex_m \
    -DHARDRT_BUILD_TESTS=OFF -DHARDRT_BUILD_EXAMPLES=OFF -DHARDRT_ENABLE_CPP=ON \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" "$@"
  cmake --build "$build_dir" --parallel "$JOBS"
  cmake --install "$build_dir" --prefix "$install_dir"
}

configure_base_library() {
  if [[ ! -f "$BASE_BUILD/libhardrt.a" ]]; then
    cmake -S "$ROOT_DIR" -B "$BASE_BUILD" -G "$GENERATOR" \
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DHARDRT_PORT=cortex_m \
      -DHARDRT_BUILD_TESTS=OFF -DHARDRT_BUILD_EXAMPLES=OFF -DHARDRT_ENABLE_CPP=ON \
      -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN"
  fi
  cmake --build "$BASE_BUILD" --parallel "$JOBS"
  rm -rf "$BASE_INSTALL"
  cmake --install "$BASE_BUILD" --prefix "$BASE_INSTALL"
}

build_app() {
  local name="$1" source_dir="$2" prefix="$3"; shift 3
  local build_dir="$APP_BUILD_ROOT/$name"; rm -rf "$build_dir"
  echo "[STM32 CI] Configuring $name"
  cmake -S "$source_dir" -B "$build_dir" -G "$GENERATOR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCMAKE_PREFIX_PATH="$prefix" -DSTM32CUBE_H7_ROOT="$STM32CUBE_H7_ROOT" "$@"
  cmake --build "$build_dir" --parallel "$JOBS"
  local elf; elf="$(find "$build_dir" -maxdepth 1 -type f -name '*.elf' -size +0c -print -quit)"
  [[ -n "$elf" ]] || { echo "No non-empty ELF produced for $name" >&2; exit 1; }
  echo "[STM32 CI] PASS $name -> $elf"
}

configure_timing_library() {
  local build_dir="$1" install_dir="$2" hook_header="$3"
  rm -rf "$build_dir" "$install_dir"
  cmake -S "$ROOT_DIR" -B "$build_dir" -G "$GENERATOR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DHARDRT_PORT=cortex_m \
    -DHARDRT_BUILD_TESTS=OFF -DHARDRT_BUILD_EXAMPLES=OFF -DHARDRT_ENABLE_CPP=OFF \
    -DHARDRT_TIMING_PROFILE=ipc -DHARDRT_TIMING_HOOK_HEADER="$hook_header" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN"
  cmake --build "$build_dir" --parallel "$JOBS"
  cmake --install "$build_dir" --prefix "$install_dir"
}

configure_base_library
rm -rf "$APP_BUILD_ROOT"; mkdir -p "$APP_BUILD_ROOT"
build_app hardrt_h755_blinky "$ROOT_DIR/examples/hardrt_h755_blinky" "$BASE_INSTALL"
build_app hardrt_h755_blinky_cpp "$ROOT_DIR/examples/hardrt_h755_blinky_cpp" "$BASE_INSTALL"
build_app hardrt_h755_demo "$ROOT_DIR/examples/hardrt_h755_demo" "$BASE_INSTALL"
build_app hardrt_h755_preemption_priority "$ROOT_DIR/examples/hardrt_h755_preemption" "$BASE_INSTALL" -DHARDRT_PREEMPT_CASE=priority
build_app hardrt_h755_preemption_priority_rr "$ROOT_DIR/examples/hardrt_h755_preemption" "$BASE_INSTALL" -DHARDRT_PREEMPT_CASE=priority_rr
build_app hardrt_h755_fpu_validation "$ROOT_DIR/examples/hardrt_h755_fpu_validation" "$BASE_INSTALL"
build_app hardrt_h755_global_rr "$ROOT_DIR/examples/hardrt_h755_global_rr" "$BASE_INSTALL"
build_app hardrt_h755_ipc_semaphore "$ROOT_DIR/examples/hardrt_h755_ipc_validation" "$BASE_INSTALL" -DHARDRT_IPC_CASE=semaphore
build_app hardrt_h755_ipc_queue "$ROOT_DIR/examples/hardrt_h755_ipc_validation" "$BASE_INSTALL" -DHARDRT_IPC_CASE=queue
build_app hardrt_h755_ipc_mutex "$ROOT_DIR/examples/hardrt_h755_ipc_validation" "$BASE_INSTALL" -DHARDRT_IPC_CASE=mutex
build_app hardrt_h755_external_tick "$ROOT_DIR/examples/hardrt_h755_external_tick" "$BASE_INSTALL"
build_app hardrt_h755_dwt_event_to_task "$ROOT_DIR/examples/hardrt_h755_dwt_timing" "$BASE_INSTALL" -DHARDRT_TIMING_CASE=event_to_task -DHARDRT_TIMING_TARGET_SAMPLES=8
build_app hardrt_h755_dwt_scheduler_decision "$ROOT_DIR/examples/hardrt_h755_dwt_timing" "$BASE_INSTALL" -DHARDRT_TIMING_CASE=scheduler_decision -DHARDRT_TIMING_TARGET_SAMPLES=8

# Compile every tick/sleeper workload branch using the normal eight-task
# configuration, then compile the most demanding simultaneous-expiry workload
# at the largest benchmarked capacity to catch RAM/link/configuration issues.
for scenario in none one_sleep all_sleep one_expiry simultaneous staggered; do
  build_app "hardrt_h755_tick_${scenario}_8" "$ROOT_DIR/examples/hardrt_h755_tick_benchmark" "$BASE_INSTALL" \
    -DHARDRT_TICK_BENCH_SCENARIO="$scenario" -DHARDRT_TICK_BENCH_TARGET_SAMPLES=8
done

TICK32_BUILD="$ROOT_DIR/build-cortex_m-tick32-ci"
TICK32_INSTALL="$ROOT_DIR/install-cortexm-tick32-ci"
configure_library "$TICK32_BUILD" "$TICK32_INSTALL" -DHARDRT_CFG_MAX_TASKS=32 -DHARDRT_ENABLE_CPP=OFF
build_app hardrt_h755_tick_simultaneous_32 "$ROOT_DIR/examples/hardrt_h755_tick_benchmark" "$TICK32_INSTALL" \
  -DHARDRT_TICK_BENCH_SCENARIO=simultaneous -DHARDRT_TICK_BENCH_TARGET_SAMPLES=8

IPC_BUILD="$ROOT_DIR/build-cortex_m-timing-ipc-ci"; IPC_INSTALL="$ROOT_DIR/install-cortexm-timing-ipc-ci"; IPC_HOOK_HEADER="$ROOT_DIR/examples/hardrt_h755_dwt_timing/inc/hardrt_timing_hooks.h"
configure_timing_library "$IPC_BUILD" "$IPC_INSTALL" "$IPC_HOOK_HEADER"
build_app hardrt_h755_dwt_sem_isr_ready "$ROOT_DIR/examples/hardrt_h755_dwt_timing" "$IPC_INSTALL" -DHARDRT_TIMING_CASE=sem_isr_ready -DHARDRT_TIMING_TARGET_SAMPLES=8

READY_BUILD="$ROOT_DIR/build-cortex_m-timing-ready-ci"; READY_INSTALL="$ROOT_DIR/install-cortexm-timing-ready-ci"; READY_HOOK_HEADER="$ROOT_DIR/examples/hardrt_h755_dwt_timing/inc/hardrt_timing_ready_hooks.h"
configure_timing_library "$READY_BUILD" "$READY_INSTALL" "$READY_HOOK_HEADER"
build_app hardrt_h755_dwt_ready_to_task "$ROOT_DIR/examples/hardrt_h755_dwt_timing" "$READY_INSTALL" -DHARDRT_TIMING_CASE=ready_to_task -DHARDRT_TIMING_TARGET_SAMPLES=8

echo "[STM32 CI] All STM32H755 examples cross-built successfully."
