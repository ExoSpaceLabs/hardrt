#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION=""
RUN_DIR=""
OUTPUT_DIR=""
FORCE=0

usage() {
  cat <<'USAGE'
Usage:
  scripts/package_stm32_qualification.sh X.Y.Z RUN_DIR [options]

Create one deterministic tar.xz archive plus SHA-256 file from a passing full
STM32 qualification run. The source evidence directory remains untracked.

Arguments:
  X.Y.Z       Release version using the repository's non-v-prefixed tag format.
  RUN_DIR     Qualification run directory containing qualification.md and raw/.

Options:
  --output-dir DIR  Output directory. Default:
                    validation/stm32/releases/X.Y.Z/
  --force           Replace an existing local archive/checksum.
  -h, --help        Show this help.

Output files:
  hardrt-stm32-qualification-X.Y.Z.tar.xz
  hardrt-stm32-qualification-X.Y.Z.tar.xz.sha256
USAGE
}

[[ $# -ge 2 ]] || { usage >&2; exit 2; }
VERSION="$1"
RUN_DIR="$2"
shift 2

while [[ $# -gt 0 ]]; do
  case "$1" in
    --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
    --force) FORCE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || {
  echo "Version must use X.Y.Z without a v prefix: $VERSION" >&2
  exit 2
}

need() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "Missing command: $1" >&2
    exit 2
  }
}
for cmd in git grep sed tar xz sha256sum mktemp cp head tr mv basename; do need "$cmd"; done

if ! tar --version 2>/dev/null | grep -q 'GNU tar'; then
  echo "GNU tar is required for deterministic qualification packaging" >&2
  exit 2
fi

RUN_DIR="$(cd "$RUN_DIR" 2>/dev/null && pwd)" || {
  echo "Qualification run directory does not exist: $RUN_DIR" >&2
  exit 2
}
REPORT="$RUN_DIR/qualification.md"
RAW_DIR="$RUN_DIR/raw"
[[ -f "$REPORT" ]] || { echo "Missing qualification report: $REPORT" >&2; exit 2; }
[[ -d "$RAW_DIR" ]] || { echo "Missing raw evidence directory: $RAW_DIR" >&2; exit 2; }

require_report_line() {
  local text="$1"
  grep -Fq -- "$text" "$REPORT" || {
    echo "Qualification report does not satisfy release packaging contract: $text" >&2
    exit 1
  }
}

require_report_line '- HardRT tracked source state: **clean**'
require_report_line '- Selected mode: **all tests (functional + benchmark)**'
require_report_line '- Board probe: **PASS**'
require_report_line '- Overall: **PASS**'
# Literal Markdown backticks are intentional here.
# shellcheck disable=SC2016
require_report_line '- Runner: `scripts/stm32_manual_test_full.sh`'

if grep -Fq '**ABORTED**' "$REPORT"; then
  echo "Qualification report records an aborted run" >&2
  exit 1
fi

FUNCTIONAL_TOTAL="$(sed -nE 's/^- Functional contracts: \*\*([0-9]+)\*\*.*/\1/p' "$REPORT" | head -n1)"
BENCHMARK_TOTAL="$(sed -nE 's/^- Hardware benchmarks: \*\*([0-9]+)\*\*.*/\1/p' "$REPORT" | head -n1)"
[[ "$FUNCTIONAL_TOTAL" =~ ^[1-9][0-9]*$ ]] || {
  echo "Could not resolve functional-contract total from $REPORT" >&2
  exit 1
}
[[ "$BENCHMARK_TOTAL" =~ ^[1-9][0-9]*$ ]] || {
  echo "Could not resolve benchmark total from $REPORT" >&2
  exit 1
}

require_report_line "- Functional contracts passed: **$FUNCTIONAL_TOTAL / $FUNCTIONAL_TOTAL**"
require_report_line '- Functional contracts failed: **0**'
require_report_line '- Functional contracts not run: **0**'
require_report_line "- Benchmarks passed: **$BENCHMARK_TOTAL / $BENCHMARK_TOTAL**"
require_report_line '- Benchmarks failed: **0**'
require_report_line '- Benchmarks not run: **0**'

# The sed program is deliberately single-quoted so its Markdown backticks and
# capture expression remain literal shell input.
# shellcheck disable=SC2016
QUALIFIED_SHA="$(sed -nE 's/^- HardRT SHA: `([0-9a-fA-F]{40})`.*/\1/p' "$REPORT" | head -n1 | tr 'A-F' 'a-f')"
[[ "$QUALIFIED_SHA" =~ ^[0-9a-f]{40}$ ]] || {
  echo "Could not resolve a 40-character HardRT SHA from $REPORT" >&2
  exit 1
}

git -C "$ROOT_DIR" cat-file -e "${QUALIFIED_SHA}^{commit}" 2>/dev/null || {
  echo "Qualified SHA is not available in this repository: $QUALIFIED_SHA" >&2
  exit 1
}

SOURCE_DATE_EPOCH="$(git -C "$ROOT_DIR" show -s --format=%ct "$QUALIFIED_SHA")"
[[ "$SOURCE_DATE_EPOCH" =~ ^[0-9]+$ ]] || {
  echo "Could not resolve SOURCE_DATE_EPOCH for $QUALIFIED_SHA" >&2
  exit 1
}

if [[ -z "$OUTPUT_DIR" ]]; then
  OUTPUT_DIR="$ROOT_DIR/validation/stm32/releases/$VERSION"
fi
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"

ARCHIVE_NAME="hardrt-stm32-qualification-${VERSION}.tar.xz"
CHECKSUM_NAME="${ARCHIVE_NAME}.sha256"
ARCHIVE="$OUTPUT_DIR/$ARCHIVE_NAME"
CHECKSUM="$OUTPUT_DIR/$CHECKSUM_NAME"

if (( FORCE == 0 )) && { [[ -e "$ARCHIVE" ]] || [[ -e "$CHECKSUM" ]]; }; then
  echo "Qualification package already exists:" >&2
  [[ -e "$ARCHIVE" ]] && echo "  $ARCHIVE" >&2
  [[ -e "$CHECKSUM" ]] && echo "  $CHECKSUM" >&2
  echo "Use --force only when intentionally rebuilding the same local package." >&2
  exit 1
fi

TMP_DIR="$(mktemp -d)"
cleanup() { rm -rf -- "$TMP_DIR"; }
trap cleanup EXIT INT TERM

PACKAGE_ROOT="hardrt-stm32-qualification-${VERSION}"
STAGE="$TMP_DIR/$PACKAGE_ROOT"
mkdir -p "$STAGE"
cp -a "$RUN_DIR/." "$STAGE/"

cat > "$STAGE/PACKAGE_METADATA.txt" <<EOF
format=hardrt-stm32-qualification-v1
release=$VERSION
qualified_sha=$QUALIFIED_SHA
source_run=$(basename "$RUN_DIR")
functional_contracts=$FUNCTIONAL_TOTAL
hardware_benchmarks=$BENCHMARK_TOTAL
report=qualification.md
EOF

TMP_ARCHIVE="$TMP_DIR/$ARCHIVE_NAME"
tar \
  --sort=name \
  --format=gnu \
  --mtime="@${SOURCE_DATE_EPOCH}" \
  --owner=0 \
  --group=0 \
  --numeric-owner \
  -C "$TMP_DIR" \
  -cf - "$PACKAGE_ROOT" \
  | xz -T1 -9e > "$TMP_ARCHIVE"

xz -t "$TMP_ARCHIVE"
tar -tJf "$TMP_ARCHIVE" | grep -Fqx "$PACKAGE_ROOT/qualification.md"
tar -tJf "$TMP_ARCHIVE" | grep -Fqx "$PACKAGE_ROOT/PACKAGE_METADATA.txt"

mv -f -- "$TMP_ARCHIVE" "$ARCHIVE"
(
  cd "$OUTPUT_DIR"
  sha256sum "$ARCHIVE_NAME" > "$CHECKSUM_NAME"
  sha256sum -c "$CHECKSUM_NAME"
)

printf 'QUALIFIED_SHA=%s\n' "$QUALIFIED_SHA"
printf 'FUNCTIONAL_CONTRACTS=%s\n' "$FUNCTIONAL_TOTAL"
printf 'HARDWARE_BENCHMARKS=%s\n' "$BENCHMARK_TOTAL"
printf 'ARCHIVE=%s\n' "$ARCHIVE"
printf 'CHECKSUM=%s\n' "$CHECKSUM"
printf 'Qualification package PASS: %s\n' "$ARCHIVE_NAME"
