#!/usr/bin/env bash
# Check released DAS v0.1.0 interoperability without claiming physical HIL.
set -Eeuo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/thirdparty/DAS.lock"
CUBE=""
DAS_ROOT=""
BUILD_ROOT="$ROOT/build-das-interop"
usage() {
  echo "Usage: $0 /path/to/STM32CubeH7 [--das-root DIR] [--build-root DIR]"
}
while (( $# > 0 )); do
  case "$1" in
    --stm32h7-root) CUBE="$2"; shift 2 ;;
    --das-root) DAS_ROOT="$2"; shift 2 ;;
    --build-root) BUILD_ROOT="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    --*) echo "Unknown option $1" >&2; exit 2 ;;
    *) [[ -z "$CUBE" ]] || { echo "Multiple Cube roots" >&2; exit 2; }
       CUBE="$1"; shift ;;
  esac
done
[[ -n "$CUBE" ]] || { usage >&2; exit 2; }
CUBE="$(cd "$CUBE" && pwd)"
for f in "$CUBE/Drivers/CMSIS/Core/Include/core_cm7.h"   "$CUBE/Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h755xx.h"; do
  [[ -f "$f" ]] || { echo "Missing CMSIS header: $f" >&2; exit 2; }
done
for cmd in cmake git arm-none-eabi-gcc arm-none-eabi-nm; do
  command -v "$cmd" >/dev/null || { echo "Missing $cmd" >&2; exit 2; }
done
[[ "$DAS_TAG" == "v0.1.0" && "$DAS_SHA" =~ ^[0-9a-f]{40}$ ]] || {
  echo "Invalid DAS release pin" >&2; exit 2;
}
BUILD_ROOT="$(mkdir -p "$BUILD_ROOT" && cd "$BUILD_ROOT" && pwd)"
[[ "$BUILD_ROOT" != "/" && "$BUILD_ROOT" != "$ROOT" ]] || exit 2
if [[ -z "$DAS_ROOT" ]]; then
  DAS_ROOT="$BUILD_ROOT/thirdparty/das"
  if [[ ! -d "$DAS_ROOT/.git" ]]; then
    mkdir -p "$(dirname "$DAS_ROOT")"
    git clone --depth 1 --single-branch --branch "$DAS_TAG" "$DAS_REPOSITORY" "$DAS_ROOT"
  fi
fi
DAS_ROOT="$(cd "$DAS_ROOT" && pwd)"
tag_sha="$(git -C "$DAS_ROOT" rev-list -n 1 "$DAS_TAG")"
source_sha="$(git -C "$DAS_ROOT" rev-parse HEAD)"
[[ "$tag_sha" == "$DAS_SHA" && "$source_sha" == "$DAS_SHA" ]] || {
  echo "DAS pin mismatch: expected $DAS_TAG @ $DAS_SHA; got $source_sha" >&2
  exit 1
}
[[ -z "$(git -C "$DAS_ROOT" status --porcelain --untracked-files=no)" ]] || {
  echo "DAS tracked source is dirty" >&2; exit 1;
}
[[ "$(tr -d '[:space:]' < "$DAS_ROOT/VERSION")" == "0.1.0" ]] || exit 1
HRT_VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
echo "DAS reference : $DAS_TAG @ $DAS_SHA"
echo "HardRT source : $(git -C "$ROOT" rev-parse HEAD), VERSION=$HRT_VERSION"
echo "This is a cross-build/link test, NOT a physical STM32 qualification test."

toolchain="$DAS_ROOT/cmake/toolchains/arm-none-eabi.cmake"
cmake -S "$DAS_ROOT" -B "$BUILD_ROOT/das"   -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_BUILD_TYPE=Release   -DDAS_DEVICE=nucleo_h755zi_q -DDAS_CORE=cm7   -DSTM32_CUBE_H7_DIR="$CUBE"   -DDAS_BUILD_HARDWARE_TESTS=OFF -DDAS_BUILD_LINK_TESTS=OFF
cmake --build "$BUILD_ROOT/das" --parallel
cmake --install "$BUILD_ROOT/das" --prefix "$BUILD_ROOT/install/das"

cmake -S "$ROOT" -B "$BUILD_ROOT/hardrt"   -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_BUILD_TYPE=Release   -DHARDRT_PORT=cortex_m -DHARDRT_BUILD_TESTS=OFF -DHARDRT_BUILD_EXAMPLES=OFF
cmake --build "$BUILD_ROOT/hardrt" --parallel
cmake --install "$BUILD_ROOT/hardrt" --prefix "$BUILD_ROOT/install/hardrt"

cmake -S "$ROOT/tests/das_installed_consumer" -B "$BUILD_ROOT/consumer"   -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_BUILD_TYPE=Release   -DDAS_SOURCE_ROOT="$DAS_ROOT" -DHARDRT_EXPECTED_VERSION="$HRT_VERSION"   -DDAS_CORE=cm7   -DCMAKE_PREFIX_PATH="$BUILD_ROOT/install/hardrt;$BUILD_ROOT/install/das"
cmake --build "$BUILD_ROOT/consumer" --parallel
elf="$BUILD_ROOT/consumer/hardrt_das_release_consumer.elf"
[[ -s "$elf" ]] || { echo "Integration ELF missing" >&2; exit 1; }
for symbol in HardFault_Handler PendSV_Handler SysTick_Handler g_das_vector_table; do
  arm-none-eabi-nm "$elf" | awk -v name="$symbol"     '$3 == name && $2 ~ /^[TtRD]$/ { found=1 } END { exit(!found) }' || {
      echo "Missing $symbol in HardRT+DAS ELF" >&2; exit 1;
    }
done
echo "PASS: pinned DAS $DAS_TAG and current HardRT $HRT_VERSION CM7 consumer"
