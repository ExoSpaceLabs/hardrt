#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPLACE=0

usage() {
  cat <<'USAGE'
Usage: scripts/promote_stm32_qualification.sh [--replace] RUN_DIR vX.Y.Z

Promotes one passing local STM32 qualification run into the repository's
single evidence package for a release. Development runs remain under the
ignored .qualification/ tree.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --replace) REPLACE=1; shift;;
    -h|--help) usage; exit 0;;
    *) break;;
  esac
done

[[ $# -eq 2 ]] || { usage >&2; exit 2; }
RUN_DIR="$(cd "$1" 2>/dev/null && pwd)" || { echo "Invalid run directory: $1" >&2; exit 2; }
VERSION="$2"
[[ "$VERSION" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "Release must look like v0.5.0" >&2; exit 2; }

REPORT="$RUN_DIR/qualification.md"
[[ -f "$REPORT" ]] || { echo "Missing qualification.md in $RUN_DIR" >&2; exit 2; }

grep -q -- '- Full matrix overall: \*\*PASS\*\*' "$REPORT" || { echo "Refusing to promote an incomplete or non-PASS full hardware matrix" >&2; exit 1; }
grep -q -- '- HardRT tracked source state: \*\*clean\*\*' "$REPORT" || { echo "Refusing to promote a dirty-source run" >&2; exit 1; }
grep -qE -- '- STM32CubeH7 SHA/state: `[^`]+` / `clean`' "$REPORT" || { echo "Refusing to promote a run with dirty/unrecorded STM32CubeH7" >&2; exit 1; }

REPORT_SHA="$(sed -n 's/^- HardRT SHA: `\([^`]*\)`$/\1/p' "$REPORT" | head -n1)"
HEAD_SHA="$(git -C "$ROOT_DIR" rev-parse HEAD)"
[[ -n "$REPORT_SHA" && "$REPORT_SHA" == "$HEAD_SHA" ]] || {
  echo "Run SHA ($REPORT_SHA) does not match current HEAD ($HEAD_SHA)." >&2
  echo "Promote only the exact release-candidate commit." >&2
  exit 1
}

DEST="$ROOT_DIR/validation/stm32/releases/$VERSION"
if [[ -e "$DEST" ]]; then
  if (( REPLACE == 0 )); then
    echo "Evidence already exists for $VERSION: $DEST" >&2
    echo "Use --replace only when deliberately superseding the same release candidate." >&2
    exit 1
  fi
  rm -rf -- "$DEST"
fi

mkdir -p "$(dirname "$DEST")"
cp -a "$RUN_DIR" "$DEST"
cat > "$DEST/RELEASE_EVIDENCE.txt" <<META
release=$VERSION
hardrt_sha=$HEAD_SHA
promoted_utc=$(date -u --iso-8601=seconds)
source_run=$(basename "$RUN_DIR")
META

echo "Promoted one STM32 qualification package for $VERSION:"
echo "  $DEST"
echo "Commit this directory with:"
echo "  git add validation/stm32/releases/$VERSION"
echo "  git commit -m 'test(hardrt): archive $VERSION STM32 qualification'"
