#!/usr/bin/env python3
"""Extract one version section from RELEASE_NOTES.md for GitHub Releases."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


HEADING_RE = re.compile(r"^# HardRT v(?P<version>\d+\.\d+\.\d+) Release Notes\s*$")


def extract_release_notes(text: str, version: str) -> str:
    lines = text.splitlines()
    start = None

    for index, line in enumerate(lines):
        match = HEADING_RE.match(line)
        if match and match.group("version") == version:
            start = index
            break

    if start is None:
        raise ValueError(f"release notes for {version} were not found")

    end = len(lines)
    for index in range(start + 1, len(lines)):
        if lines[index].strip() == "---":
            end = index
            break
        if HEADING_RE.match(lines[index]):
            end = index
            break

    section = "\n".join(lines[start:end]).strip()
    if not section:
        raise ValueError(f"release notes for {version} are empty")
    return section + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("version", help="release version without a v prefix")
    parser.add_argument("release_notes", type=Path, help="path to RELEASE_NOTES.md")
    args = parser.parse_args()

    if not re.fullmatch(r"\d+\.\d+\.\d+", args.version):
        parser.error("version must be X.Y.Z without a v prefix")

    try:
        section = extract_release_notes(args.release_notes.read_text(encoding="utf-8"), args.version)
    except ValueError as exc:
        parser.error(str(exc))

    print(section, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
