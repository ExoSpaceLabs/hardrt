#!/usr/bin/env python3
"""Prove hardware-qualified build-input equivalence across distinct Git commits.

A reviewed develop -> main merge changes commit SHA, but must not introduce
unqualified target/build/test inputs. Only non-build documentation drift is allowed.
"""

from __future__ import annotations

import argparse
import subprocess
from pathlib import PurePosixPath

# Deliberately conservative. Workflows, build/release helpers, version inputs,
# tests and anything unknown are NOT automatically exempt.
ALLOWED_EXACT = {"README.md", "RELEASE_NOTES.md"}
ALLOWED_PREFIXES = ("docs/",)


def changed_paths(base: str, head: str) -> list[str]:
    result = subprocess.run(
        ["git", "diff", "--name-only", "--no-renames", f"{base}..{head}", "--"],
        check=True, text=True, capture_output=True,
    )
    return [line.strip() for line in result.stdout.splitlines() if line]


def allowed(path: str) -> bool:
    normalized = PurePosixPath(path).as_posix()
    return normalized in ALLOWED_EXACT or any(
        normalized.startswith(prefix) for prefix in ALLOWED_PREFIXES
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("qualified_sha", help="physically qualified develop commit")
    parser.add_argument("release_sha", nargs="?", default="HEAD", help="tagged main merge commit")
    args = parser.parse_args()

    # Resolve both revisions to make the audit output stable and unambiguous.
    qualified = subprocess.check_output(
        ["git", "rev-parse", "--verify", f"{args.qualified_sha}^{{commit}}"], text=True
    ).strip()
    release = subprocess.check_output(
        ["git", "rev-parse", "--verify", f"{args.release_sha}^{{commit}}"], text=True
    ).strip()
    changed = changed_paths(qualified, release)
    rejected = [path for path in changed if not allowed(path)]

    print(f"qualified_sha={qualified}")
    print(f"release_sha={release}")
    print(f"changed_files={len(changed)}")
    for path in changed:
        print(f"{'ALLOWED' if allowed(path) else 'REJECTED'}: {path}")

    if rejected:
        print("FAIL: unqualified build/test/source inputs differ. Requalify before release.")
        return 1

    print("PASS: qualified build inputs are equivalent (commit SHAs may differ).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
