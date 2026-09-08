#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION=""
RUN_DIR=""
REPO=""
OUTPUT_DIR=""
CLEANUP_BRANCHES=0
ASSUME_YES=0
REPLACE_ASSETS=0
FORCE_PACKAGE=0

usage() {
  cat <<'USAGE'
Usage:
  scripts/finalize_release.sh X.Y.Z RUN_DIR [options]

Finalize an already-tagged HardRT release by validating the CI-produced software
assets, packaging and publishing the retained STM32 qualification evidence,
verifying both checksum sets, publishing a draft GitHub Release when applicable,
and optionally deleting all remote branches except main and develop.

Arguments:
  X.Y.Z       Existing non-v-prefixed release tag.
  RUN_DIR     Passing full STM32 qualification run directory.

Options:
  --repo OWNER/REPO     GitHub repository. Default: resolved by `gh repo view`.
  --output-dir DIR      Local package output directory. Default:
                        validation/stm32/releases/X.Y.Z/
  --force-package       Rebuild an existing local qualification package.
  --replace-assets      Replace same-named qualification assets on the release.
                        Without this flag, identical existing assets are accepted
                        and different existing assets fail safely.
  --cleanup-branches    Delete every remote branch except main and develop after
                        release publication and verification succeeds.
  --yes                 Do not prompt before --cleanup-branches deletion.
  -h, --help            Show this help.

The script requires the release tag to equal origin/main. origin/develop may be
at the release tag or ahead of it, but the release tag must remain its ancestor.
The tag itself is never moved or rewritten.
USAGE
}

[[ $# -ge 2 ]] || { usage >&2; exit 2; }
VERSION="$1"
RUN_DIR="$2"
shift 2

while [[ $# -gt 0 ]]; do
  case "$1" in
    --repo) REPO="$2"; shift 2 ;;
    --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
    --force-package) FORCE_PACKAGE=1; shift ;;
    --replace-assets) REPLACE_ASSETS=1; shift ;;
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
    echo "Missing command: $1" >&2
    exit 2
  }
}
for cmd in git gh grep sed sha256sum python3 head tr awk sort cmp mktemp basename; do need "$cmd"; done

cd "$ROOT_DIR"

gh auth status >/dev/null 2>&1 || {
  echo "GitHub CLI is not authenticated. Run: gh auth login" >&2
  exit 2
}

if [[ -z "$REPO" ]]; then
  REPO="$(gh repo view --json nameWithOwner --jq '.nameWithOwner')"
fi
[[ "$REPO" == */* ]] || { echo "Could not resolve OWNER/REPO" >&2; exit 2; }

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

RELEASE_TAG="$(gh release view "$VERSION" --repo "$REPO" --json tagName --jq '.tagName' 2>/dev/null)" || {
  echo "GitHub Release does not exist for tag $VERSION" >&2
  exit 1
}
RELEASE_DRAFT="$(gh release view "$VERSION" --repo "$REPO" --json isDraft --jq '.isDraft')"
RELEASE_IMMUTABLE="$(gh release view "$VERSION" --repo "$REPO" --json isImmutable --jq '.isImmutable')"
[[ "$RELEASE_TAG" == "$VERSION" ]] || { echo "GitHub Release tag mismatch: $RELEASE_TAG" >&2; exit 1; }

TMP_DIR="$(mktemp -d)"
cleanup() { rm -rf -- "$TMP_DIR"; }
trap cleanup EXIT INT TERM

asset_exists() {
  local name="$1"
  gh release view "$VERSION" --repo "$REPO" --json assets --jq '.assets[].name' | grep -Fqx "$name"
}

SOFTWARE_ASSETS=(
  "hardrt-posix-${VERSION}.tar.gz"
  "hardrt-cortexm-${VERSION}.tar.gz"
  "hardrt-bundle-${VERSION}.tar.gz"
  "SHA256SUMS"
)
for asset in "${SOFTWARE_ASSETS[@]}"; do
  asset_exists "$asset" || {
    echo "Release workflow software asset is missing: $asset" >&2
    exit 1
  }
done

SOFTWARE_VERIFY_DIR="$TMP_DIR/software"
mkdir -p "$SOFTWARE_VERIFY_DIR"
gh release download "$VERSION" --repo "$REPO" \
  --pattern "hardrt-posix-${VERSION}.tar.gz" \
  --pattern "hardrt-cortexm-${VERSION}.tar.gz" \
  --pattern "hardrt-bundle-${VERSION}.tar.gz" \
  --pattern "SHA256SUMS" \
  --dir "$SOFTWARE_VERIFY_DIR"
(
  cd "$SOFTWARE_VERIFY_DIR"
  sha256sum -c SHA256SUMS
)
echo "Release software asset verification PASS"

if [[ -z "$OUTPUT_DIR" ]]; then
  OUTPUT_DIR="$ROOT_DIR/validation/stm32/releases/$VERSION"
fi
PACKAGE_ARGS=("$VERSION" "$RUN_DIR" --output-dir "$OUTPUT_DIR")
(( FORCE_PACKAGE == 0 )) || PACKAGE_ARGS+=(--force)
"$ROOT_DIR/scripts/package_stm32_qualification.sh" "${PACKAGE_ARGS[@]}"

RUN_DIR_ABS="$(cd "$RUN_DIR" && pwd)"
REPORT="$RUN_DIR_ABS/qualification.md"
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
[[ -f "$ARCHIVE" && -f "$CHECKSUM" ]] || {
  echo "Qualification package helper did not produce expected files" >&2
  exit 1
}

ensure_asset() {
  local path="$1"
  local name
  name="$(basename "$path")"

  if asset_exists "$name"; then
    if (( REPLACE_ASSETS != 0 )); then
      [[ "$RELEASE_IMMUTABLE" != "true" ]] || {
        echo "Release is immutable; published asset cannot be replaced: $name" >&2
        exit 1
      }
      echo "Replacing release asset: $name"
      gh release upload "$VERSION" "$path" --repo "$REPO" --clobber
      return
    fi

    local existing_dir="$TMP_DIR/existing-$name"
    mkdir -p "$existing_dir"
    gh release download "$VERSION" --repo "$REPO" --pattern "$name" --dir "$existing_dir"
    if cmp -s "$path" "$existing_dir/$name"; then
      echo "Release asset already matches local file: $name"
      return
    fi

    echo "Release asset exists but differs from local file: $name" >&2
    echo "Use --replace-assets only after explicitly deciding to replace published evidence." >&2
    exit 1
  fi

  [[ "$RELEASE_IMMUTABLE" != "true" ]] || {
    echo "Release is immutable and qualification asset is missing: $name" >&2
    exit 1
  }
  echo "Uploading release asset: $name"
  gh release upload "$VERSION" "$path" --repo "$REPO"
}

ensure_asset "$ARCHIVE"
ensure_asset "$CHECKSUM"

VERIFY_DIR="$TMP_DIR/qualification"
mkdir -p "$VERIFY_DIR"
gh release download "$VERSION" --repo "$REPO" \
  --pattern "$ARCHIVE_NAME" \
  --pattern "$CHECKSUM_NAME" \
  --dir "$VERIFY_DIR"
cmp -s "$CHECKSUM" "$VERIFY_DIR/$CHECKSUM_NAME" || {
  echo "Published checksum asset differs from local checksum" >&2
  exit 1
}
(
  cd "$VERIFY_DIR"
  sha256sum -c "$CHECKSUM_NAME"
)
echo "Physical qualification asset verification PASS"

if [[ "$RELEASE_DRAFT" == "true" ]]; then
  gh release edit "$VERSION" --repo "$REPO" --draft=false
  echo "GitHub Release published: $VERSION"
fi
FINAL_DRAFT="$(gh release view "$VERSION" --repo "$REPO" --json isDraft --jq '.isDraft')"
[[ "$FINAL_DRAFT" == "false" ]] || {
  echo "GitHub Release is still a draft after finalization" >&2
  exit 1
}

printf 'Release publication PASS\n'
printf '  repository:    %s\n' "$REPO"
printf '  release/tag:   %s @ %s\n' "$VERSION" "$TAG_SHA"
printf '  qualified SHA: %s\n' "$QUALIFIED_SHA"
printf '  archive:       %s\n' "$ARCHIVE_NAME"
printf '  checksum:      %s\n' "$CHECKSUM_NAME"

mapfile -t EXTRA_BRANCHES < <(
  git ls-remote --heads origin \
    | awk '{sub("refs/heads/", "", $2); print $2}' \
    | grep -Ev '^(main|develop)$' \
    | sort || true
)

if (( CLEANUP_BRANCHES != 0 )); then
  if ((${#EXTRA_BRANCHES[@]})); then
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
    git ls-remote --heads origin | awk '{sub("refs/heads/", "", $2); print $2}' | sort
  )
  if [[ "${REMAINING_BRANCHES[*]}" != "develop main" ]]; then
    echo "Unexpected remote branch set after cleanup:" >&2
    printf '  %s\n' "${REMAINING_BRANCHES[@]}" >&2
    exit 1
  fi
  echo "Remote branch cleanup PASS: main and develop only"
elif ((${#EXTRA_BRANCHES[@]})); then
  echo
  echo "Release is published, but remote temporary branches remain:"
  printf '  %s\n' "${EXTRA_BRANCHES[@]}"
  echo "Run again with --cleanup-branches after confirming all branch work is complete."
fi

echo "Release finalization PASS: $VERSION"
