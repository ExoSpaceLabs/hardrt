#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION=""
RUN_DIR=""
OUTPUT_DIR=""
LEGACY_CLEANUP_REQUESTED=0
FORCE_PACKAGE=0

usage() {
  cat <<'USAGE'
Usage:
  scripts/finalize_release.sh X.Y.Z RUN_DIR [options]

Finalize an already-tagged HardRT release without any hosting-service CLI.
The script validates the release tag/history and packages/verifies retained
STM32 qualification evidence locally. It never creates, deletes, or rewrites
remote branches.

Arguments:
  X.Y.Z       Existing non-v-prefixed release tag.
  RUN_DIR     Passing full STM32 qualification run directory. For historical
              0.5.1 only, a missing path uses the committed qualification
              record because the complete raw archive is no longer retained.

Options:
  --output-dir DIR      Local package output directory. Default:
                        validation/stm32/releases/X.Y.Z/
  --force-package       Rebuild an existing local qualification package.
  --cleanup-branches    Deprecated compatibility no-op. Branch cleanup is manual.
  --yes                 Deprecated compatibility no-op.
  -h, --help            Show this help.

Requirements:
  - standard command-line tools plus git;
  - an 'origin' remote containing main, develop, and the release tag;
  - origin/main must equal the release tag;
  - origin/develop may be ahead, but the release tag must remain its ancestor;
  - RUN_DIR must be a complete passing unfiltered STM32 qualification run;
  - historical 0.5.1 may use its committed qualification record when the
    original complete run directory is no longer available.

No hosting-service CLI, API, authentication, or release-asset publication is
performed here. Release hosting, if desired, is a separate step.
USAGE
}

[[ $# -ge 2 ]] || { usage >&2; exit 2; }
VERSION="$1"
RUN_DIR="$2"
shift 2

while [[ $# -gt 0 ]]; do
  case "$1" in
    --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
    --force-package) FORCE_PACKAGE=1; shift ;;
    --cleanup-branches) LEGACY_CLEANUP_REQUESTED=1; shift ;;
    --yes) shift ;;
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
    echo "Missing required command: $1" >&2
    exit 2
  }
}
for cmd in git grep sed sha256sum python3 head tr awk sort basename tar xz; do
  need "$cmd"
done

HISTORICAL_RECORD_ONLY=0
QUALIFIED_SHA=""

require_record_line() {
  local record="$1"
  local line="$2"
  grep -Fqx -- "$line" "$record" || {
    echo "Historical qualification record mismatch: $line" >&2
    return 1
  }
}

verify_historical_051_record() {
  local record="$ROOT_DIR/docs/QUALIFICATION_0_5_1.md"
  local staged_head=""

  [[ -f "$record" ]] || {
    echo "Missing historical 0.5.1 qualification record: $record" >&2
    return 1
  }

  require_record_line "$record" 'record_format=hardrt-historical-qualification-v1'
  require_record_line "$record" 'release=0.5.1'
  require_record_line "$record" 'run=20260907T220115Z_1802c763'
  require_record_line "$record" 'qualified_sha=1802c76392203b5e93eb73285c96ad0be30d4474'
  require_record_line "$record" 'release_sha=43dddffbfacdf2b1ed01b33940ac8f32a14d334d'
  require_record_line "$record" 'board=NUCLEO-H755ZI-Q'
  require_record_line "$record" 'core=CM7'
  require_record_line "$record" 'functional=13/13'
  require_record_line "$record" 'benchmarks=38/38'
  require_record_line "$record" 'overall=PASS'
  require_record_line "$record" 'full_raw_archive_retained=no'
  require_record_line "$record" 'audited_raw_logs=156'
  require_record_line "$record" 'partial_staging_raw_files=78'
  require_record_line "$record" 'partial_staging_has_qualification_report=no'
  require_record_line "$record" 'partial_staging_head=2b452f79d4429375d4c200e7ecb241ee02c1f439'

  [[ "$TAG_SHA" == "43dddffbfacdf2b1ed01b33940ac8f32a14d334d" ]] || {
    echo "Historical 0.5.1 release tag SHA no longer matches its recorded release SHA" >&2
    return 1
  }

  QUALIFIED_SHA="1802c76392203b5e93eb73285c96ad0be30d4474"

  if git ls-remote --exit-code --heads origin refs/heads/release/0.5.1 >/dev/null 2>&1; then
    staged_head="$(git ls-remote --heads origin refs/heads/release/0.5.1 | awk '{print $1}')"
    [[ "$staged_head" == "2b452f79d4429375d4c200e7ecb241ee02c1f439" ]] || {
      echo "Historical release/0.5.1 staging branch differs from the audited partial staging state:" >&2
      echo "  expected: 2b452f79d4429375d4c200e7ecb241ee02c1f439" >&2
      echo "  actual:   $staged_head" >&2
      return 1
    }
    echo "Historical partial staging branch identity PASS"
  fi

  echo "Historical 0.5.1 qualification record verification PASS"
  echo "  full raw archive retained: no"
  echo "  qualification at release: 13/13 functional, 38/38 benchmarks, Overall PASS"
  echo "  qualified SHA: $QUALIFIED_SHA"
}

cd "$ROOT_DIR"

git remote get-url origin >/dev/null 2>&1 || {
  echo "Missing git remote: origin" >&2
  exit 2
}

git fetch --force --prune origin \
  refs/heads/main:refs/remotes/origin/main \
  refs/heads/develop:refs/remotes/origin/develop \
  "refs/tags/$VERSION:refs/tags/$VERSION"

TAG_SHA="$(git rev-parse "refs/tags/${VERSION}^{commit}")"
MAIN_SHA="$(git rev-parse refs/remotes/origin/main)"
DEVELOP_SHA="$(git rev-parse refs/remotes/origin/develop)"

[[ "$TAG_SHA" == "$MAIN_SHA" ]] || {
  echo "Release tag does not equal origin/main:" >&2
  echo "  tag $VERSION: $TAG_SHA" >&2
  echo "  origin/main: $MAIN_SHA" >&2
  exit 1
}

git merge-base --is-ancestor "$TAG_SHA" "$DEVELOP_SHA" || {
  echo "origin/develop does not contain release tag $VERSION" >&2
  echo "  tag:     $TAG_SHA" >&2
  echo "  develop: $DEVELOP_SHA" >&2
  exit 1
}

PROJECT_VERSION="$(git show "$TAG_SHA:CMakeLists.txt" | sed -nE 's/.*VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' | head -n1)"
[[ "$PROJECT_VERSION" == "$VERSION" ]] || {
  echo "Tag version $VERSION does not match CMake project version $PROJECT_VERSION" >&2
  exit 1
}

if [[ ! -d "$RUN_DIR" ]]; then
  if [[ "$VERSION" == "0.5.1" ]]; then
    HISTORICAL_RECORD_ONLY=1
    verify_historical_051_record
  else
    echo "Qualification run directory does not exist: $RUN_DIR" >&2
    exit 2
  fi
fi

if (( HISTORICAL_RECORD_ONLY == 0 )); then
  if [[ -z "$OUTPUT_DIR" ]]; then
    OUTPUT_DIR="$ROOT_DIR/validation/stm32/releases/$VERSION"
  fi

  PACKAGE_ARGS=("$VERSION" "$RUN_DIR" --output-dir "$OUTPUT_DIR")
  (( FORCE_PACKAGE == 0 )) || PACKAGE_ARGS+=(--force)
  "$ROOT_DIR/scripts/package_stm32_qualification.sh" "${PACKAGE_ARGS[@]}"

  RUN_DIR_ABS="$(cd "$RUN_DIR" && pwd)"
  REPORT="$RUN_DIR_ABS/qualification.md"

  # The sed program is deliberately single-quoted so the Markdown backticks and
  # capture expression remain literal shell input.
  # shellcheck disable=SC2016
  QUALIFIED_SHA="$(sed -nE 's/^- HardRT SHA: `([0-9a-fA-F]{40})`.*/\1/p' "$REPORT" | head -n1 | tr 'A-F' 'a-f')"
  [[ "$QUALIFIED_SHA" =~ ^[0-9a-f]{40}$ ]] || {
    echo "Could not resolve qualified SHA from $REPORT" >&2
    exit 1
  }
fi

git merge-base --is-ancestor "$QUALIFIED_SHA" "$TAG_SHA" || {
  echo "Hardware-qualified SHA is not an ancestor of release tag $VERSION" >&2
  echo "  qualified: $QUALIFIED_SHA" >&2
  echo "  release:   $TAG_SHA" >&2
  exit 1
}

python3 "$ROOT_DIR/scripts/check_release_qualification_diff.py" "$QUALIFIED_SHA" "$TAG_SHA"

if (( HISTORICAL_RECORD_ONLY == 0 )); then
  OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"
  ARCHIVE_NAME="hardrt-stm32-qualification-${VERSION}.tar.xz"
  CHECKSUM_NAME="${ARCHIVE_NAME}.sha256"
  ARCHIVE="$OUTPUT_DIR/$ARCHIVE_NAME"
  CHECKSUM="$OUTPUT_DIR/$CHECKSUM_NAME"
  PACKAGE_ROOT="hardrt-stm32-qualification-${VERSION}"
  
  [[ -f "$ARCHIVE" && -f "$CHECKSUM" ]] || {
    echo "Qualification package helper did not produce expected files" >&2
    exit 1
  }
  
  xz -t "$ARCHIVE"
  tar -tJf "$ARCHIVE" | grep -Fqx "$PACKAGE_ROOT/qualification.md"
  tar -tJf "$ARCHIVE" | grep -Fqx "$PACKAGE_ROOT/PACKAGE_METADATA.txt"
  (
    cd "$OUTPUT_DIR"
    sha256sum -c "$CHECKSUM_NAME"
  )
  
  echo "Local physical qualification evidence verification PASS"
  printf '  release/tag:      %s @ %s\n' "$VERSION" "$TAG_SHA"
  printf '  qualified SHA:    %s\n' "$QUALIFIED_SHA"
  printf '  archive:          %s\n' "$ARCHIVE"
  printf '  checksum:         %s\n' "$CHECKSUM"
else
  echo "Historical 0.5.1 finalization proceeds from the committed qualification record."
  echo "No full physical-evidence archive is claimed or generated because the raw archive is not fully retained."
fi

if (( LEGACY_CLEANUP_REQUESTED != 0 )); then
  echo "Branch cleanup not performed: --cleanup-branches is deprecated and ignored."
  echo "Delete temporary branches manually after reviewing the remote branch list."
fi

echo "Release finalization PASS: $VERSION"
