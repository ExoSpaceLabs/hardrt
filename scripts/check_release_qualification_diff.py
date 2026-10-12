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


# Historical 0.5.1 is immutable and predates the narrowed 0.6 equivalence
# policy. The old allowlist is applicable only to these two audited SHAs.
HISTORICAL_051_SHAS = (
    "1802c76392203b5e93eb73285c96ad0be30d4474",
    "43dddffbfacdf2b1ed01b33940ac8f32a14d334d",
)
HISTORICAL_051_EXACT = ALLOWED_EXACT | {
    "scripts/check-docs.py", "scripts/extract_release_notes.py",
    "scripts/check_release_qualification_diff.py",
    "scripts/package_stm32_qualification.sh", "scripts/finalize_release.sh",
}
HISTORICAL_051_PREFIXES = ("docs/", ".github/workflows/")


def changed_paths(base: str, head: str) -> list[str]:
    result = subprocess.run(
        ["git", "diff", "--name-only", "--no-renames", f"{base}..{head}", "--"],
        check=True, text=True, capture_output=True,
    )
    return [line.strip() for line in result.stdout.splitlines() if line]


def allowed(path: str, historical_051: bool = False) -> bool:
    normalized = PurePosixPath(path).as_posix()
    exact = HISTORICAL_051_EXACT if historical_051 else ALLOWED_EXACT
    prefixes = HISTORICAL_051_PREFIXES if historical_051 else ALLOWED_PREFIXES
    return normalized in exact or any(
        normalized.startswith(prefix) for prefix in prefixes
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("qualified_sha", help="physically qualified develop commit")
    parser.add_argument("release_sha", nargs="?", default="HEAD", help="tagged main merge commit")
    parser.add_argument("--historical-0.5.1", action="store_true", dest="historical_051",
                        help="verify only the two frozen 0.5.1 SHAs using the archived policy")
    args = parser.parse_args()

    # Resolve both revisions to make the audit output stable and unambiguous.
    qualified = subprocess.check_output(
        ["git", "rev-parse", "--verify", f"{args.qualified_sha}^{{commit}}"], text=True
    ).strip()
    release = subprocess.check_output(
        ["git", "rev-parse", "--verify", f"{args.release_sha}^{{commit}}"], text=True
    ).strip()
    if args.historical_051 and (qualified, release) != HISTORICAL_051_SHAS:
        print("FAIL: 0.5.1 historical policy requires its original audited SHAs")
        return 1
    changed = changed_paths(qualified, release)
    rejected = [path for path in changed if not allowed(path, args.historical_051)]

    print(f"qualified_sha={qualified}")
    print(f"release_sha={release}")
    print(f"changed_files={len(changed)}")
    for path in changed:
        print(f"{'ALLOWED' if allowed(path, args.historical_051) else 'REJECTED'}: {path}")

    if rejected:
        print("FAIL: unqualified build/test/source inputs differ. Requalify before release.")
        return 1

    if args.historical_051:
        print("PASS: immutable historical 0.5.1 qualified source policy")
    else:
        print("PASS: qualified build inputs are equivalent (commit SHAs may differ).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
