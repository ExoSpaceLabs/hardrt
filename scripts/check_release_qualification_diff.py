#!/usr/bin/env python3
"""Reject post-qualification changes that can affect HardRT target/build behavior."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import PurePosixPath


ALLOWED_EXACT = {
    "README.md",
    "RELEASE_NOTES.md",
    "scripts/check-docs.py",
    "scripts/extract_release_notes.py",
    "scripts/check_release_qualification_diff.py",
}
ALLOWED_PREFIXES = (
    ".github/workflows/",
    "docs/",
)


def changed_paths(base: str, head: str) -> list[str]:
    result = subprocess.run(
        ["git", "diff", "--name-only", f"{base}..{head}"],
        check=True,
        text=True,
        capture_output=True,
    )
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def allowed(path: str) -> bool:
    normalized = PurePosixPath(path).as_posix()
    return normalized in ALLOWED_EXACT or normalized.startswith(ALLOWED_PREFIXES)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("qualified_sha", help="physically qualified source commit")
    parser.add_argument("release_sha", nargs="?", default="HEAD", help="candidate release/tag commit")
    args = parser.parse_args()

    changed = changed_paths(args.qualified_sha, args.release_sha)
    rejected = [path for path in changed if not allowed(path)]

    print(f"qualified={args.qualified_sha}")
    print(f"release={args.release_sha}")
    print(f"changed_files={len(changed)}")
    for path in changed:
        marker = "ALLOWED" if allowed(path) else "REJECTED"
        print(f"{marker}: {path}")

    if rejected:
        print("Release candidate contains post-qualification target/build-affecting changes.")
        return 1

    print("Post-qualification diff is release-automation/documentation-only: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
