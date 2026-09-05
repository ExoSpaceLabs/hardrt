#!/usr/bin/env python3
"""Fail CI on broken repository-local docs links and known stale release references."""
from __future__ import annotations

import re
import sys
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
MARKDOWN = [ROOT / "README.md", ROOT / "RELEASE_NOTES.md", *sorted((ROOT / "docs").glob("*.md"))]
LINK_RE = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")

errors: list[str] = []


def fail(message: str) -> None:
    errors.append(message)


for source in MARKDOWN:
    text = source.read_text(encoding="utf-8")
    for match in LINK_RE.finditer(text):
        raw = match.group(1).strip()
        # Markdown permits an optional quoted title after the target. None of
        # HardRT's local paths require spaces, so the first token is the target.
        target = raw.split()[0].strip("<>") if raw else ""
        if not target or target.startswith(("http://", "https://", "mailto:", "#")):
            continue
        target = unquote(target.split("#", 1)[0].split("?", 1)[0])
        if not target:
            continue
        resolved = (source.parent / target).resolve()
        try:
            resolved.relative_to(ROOT.resolve())
        except ValueError:
            fail(f"{source.relative_to(ROOT)}: link escapes repository: {raw}")
            continue
        if not resolved.exists():
            fail(f"{source.relative_to(ROOT)}: missing link target: {target}")

required_paths = [
    "scripts/run-all-examples.sh",
    "scripts/stm32_manual_test_full.sh",
    "scripts/build-stm32-examples-ci.sh",
    "examples/two_tasks/CMakeLists.txt",
    "examples/event_notify/CMakeLists.txt",
    "examples/event_notify_cpp/CMakeLists.txt",
    "tests/docs/api_c_smoke.c",
    "tests/docs/api_cpp_smoke.cpp",
    "docs/API_C.md",
    "docs/CPP.md",
    "docs/EVENTS_NOTIFICATIONS.md",
    "docs/COMPATIBILITY.md",
]
for rel in required_paths:
    if not (ROOT / rel).exists():
        fail(f"missing documented/release path: {rel}")

combined = "\n".join(path.read_text(encoding="utf-8") for path in MARKDOWN)
stale_patterns = {
    r"find_package\(HardRT\s+0\.3": "stale HardRT 0.3 find_package requirement",
    r"stm32_signal_profile\.sh": "removed standalone signal profiler is still documented",
    r"Event flags and task notifications[^\n]*not implemented yet": "events/notifications still described as unimplemented",
    r"Event flags and task notifications[^\n]*remain planned": "events/notifications still described as planned",
}
for pattern, description in stale_patterns.items():
    if re.search(pattern, combined, flags=re.IGNORECASE):
        fail(description)

cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
if "project(${LIB_NAME} VERSION 0.5.0 LANGUAGES C)" not in cmake:
    fail("CMake project version is not 0.5.0")

if errors:
    print("Documentation gate FAILED:", file=sys.stderr)
    for error in errors:
        print(f"  - {error}", file=sys.stderr)
    sys.exit(1)

print(f"Documentation gate PASS: {len(MARKDOWN)} Markdown files checked")
