#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION=""
RUN_DIR=""
OUTPUT_DIR=""
CLEANUP_BRANCHES=0
ASSUME_YES=0
FORCE_PACKAGE=0

usage() {
  cat <<'USAGE'
Usage:
  scripts/finalize_release.sh X.Y.Z RUN_DIR [options]

Finalize an already-tagged HardRT release without any hosting-service CLI.
The script validates the release tag/history, packages and verifies the retained
STM32 qualification evidence locally, and can optionally delete every remote
branch except main and develop.

Arguments:
  X.Y.Z       Existing non-v-prefixed release tag.
  RUN_DIR     Passing full STM32 qualification run directory.

Options:
  --output-dir DIR      Local package output directory. Default:
                        validation/stm32/releases/X.Y.Z/
  --force-package       Rebuild an existing local qualification package.
  --cleanup-branches    Delete every remote branch except main and develop after
                        local evidence verification succeeds.
  --yes                 Do not prompt before --cleanup-branches deletion.
  -h, --help            Show this help.

Requirements:
  - standard command-line tools plus git;
  - an 'origin' remote containing main, develop, and the release tag;
  - origin/main must equal the release tag;
  - origin/develop may be ahead, but the release tag must remain its ancestor;
  - RUN_DIR must be a complete passing unfiltered STM32 qualification run.

No GitHub/GitLab/service-specific CLI, API, authentication, or release-asset
publication is performed here. Release hosting, if desired, is a separate step.
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
    --cleanup-branches) CLEANUP_BRANCHES=1; shift ;;
    --yes) ASSUME_YES=1; shift ;;
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

git merge-base --is-ancestor "$QUALIFIED_SHA" "$TAG_SHA" || {
  echo "Hardware-qualified SHA is not an ancestor of release tag $VERSION" >&2
  echo "  qualified: $QUALIFIED_SHA" >&2
  echo "  release:   $TAG_SHA" >&2
  exit 1
}

python3 "$ROOT_DIR/scripts/check_release_qualification_diff.py" "$QUALIFIED_SHA" "$TAG_SHA"

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

mapfile -t EXTRA_BRANCHES < <(
  git ls-remote --heads origin \
    | awk '{sub("refs/heads/", "", $2); print $2}' \
    | grep -Ev '^(main|develop)$' \
    | sort || true
)

if (( CLEANUP_BRANCHES != 0 )); then
  if (("${#EXTRA_BRANCHES[@]}")); then
    echo
    echo "Remote branches scheduled for deletion:"
    printf '  %s\n' "${EXTRA_BRANCHES[@]}"
    echo
    echo "Policy after release finalization: retain only main and develop."

    if (( ASSUME_YES == 0 )); then
      read -r -p "Delete all listed remote branches? [y/N]: " answer
      case "${answer,,}" in
        y|yes) ;;
        *) echo "Branch cleanup cancelled" >&2; exit 1 ;;
      esac
    fi

    git push origin --delete "${EXTRA_BRANCHES[@]}"
    git fetch --prune origin
  else
    echo "Remote branch cleanup: nothing to delete"
  fi

  mapfile -t REMAINING_BRANCHES < <(
    git ls-remote --heads origin |
      awk '{sub("refs/heads/", "", $2); print $2}' |
      sort
  )

  if [[ "${REMAINING_BRANCHES[*]}" != "develop main" ]]; then
    echo "Unexpected remote branch set after cleanup:" >&2
    printf '  %s\n' "${REMAINING_BRANCHES[@]}" >&2
    exit 1
  fi

  echo "Remote branch cleanup PASS: main and develop only"
elif (("${#EXTRA_BRANCHES[@]}")); then
  echo
  echo "Temporary remote branches remain:"
  printf '  %s\n' "${EXTRA_BRANCHES[@]}"
  echo "Run again with --cleanup-branches after confirming local evidence is retained."
fi

echo "Release finalization PASS: $VERSION"
