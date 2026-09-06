#!/usr/bin/env python3
"""Fail CI on broken tracked Markdown and stale release/documentation contracts."""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
LINK_RE = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")

errors: list[str] = []


def fail(message: str) -> None:
    errors.append(message)


def tracked_markdown() -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "*.md"],
        cwd=ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    return [ROOT / rel for rel in sorted(line for line in result.stdout.splitlines() if line)]


MARKDOWN = tracked_markdown()

for source in MARKDOWN:
    text = source.read_text(encoding="utf-8")
    for match in LINK_RE.finditer(text):
        raw = match.group(1).strip()
        # Markdown permits an optional quoted title after the target. HardRT's
        # repository-local paths do not require spaces, so the first token is
        # the path we validate.
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
    "scripts/build-lib-stm32h7xx-dwt-timing.sh",
    "examples/two_tasks/CMakeLists.txt",
    "examples/event_notify/CMakeLists.txt",
    "examples/event_notify_cpp/CMakeLists.txt",
    "examples/hardrt_h755_dwt_timing/README.md",
    "tests/docs/api_c_smoke.c",
    "tests/docs/api_cpp_smoke.cpp",
    "tests/installed_consumer/CMakeLists.txt",
    "docs/API_C.md",
    "docs/CPP.md",
    "docs/EVENTS_NOTIFICATIONS.md",
    "docs/COMPATIBILITY.md",
    "docs/STM32_MANUAL_TESTS.md",
    "validation/stm32/README.md",
    "validation/stm32/releases/README.md",
]
for rel in required_paths:
    if not (ROOT / rel).exists():
        fail(f"missing documented/release path: {rel}")

combined = "\n".join(path.read_text(encoding="utf-8") for path in MARKDOWN)
stale_patterns = {
    r"find_package\(HardRT\s+0\.3": "stale HardRT 0.3 find_package requirement",
    r"stm32_signal_profile\.sh": "removed standalone signal profiler is still documented",
    r"promote_stm32_qualification\.sh": "removed/nonexistent qualification promotion script is still documented",
    r"Event flags and task notifications[^\n]*not implemented yet": "events/notifications still described as unimplemented",
    r"Event flags and task notifications[^\n]*remain planned": "events/notifications still described as planned",
    r"tag\s+`0\.5\.0`": "release documentation uses the obsolete non-v-prefixed 0.5.0 tag",
    r"qualified\s+`X\.Y\.Z`\s+tag": "release documentation uses the obsolete non-v-prefixed release-tag convention",
    r"current\s+`develop`\s+behavior": "release documentation is branch-relative instead of version-relative",
    r"on\s+the\s+`develop`\s+branch": "release documentation describes the public contract as develop-only",
    r"implemented\s+on\s+`develop`": "release documentation describes the public contract as develop-only",
    r"true\s+global\s+round-robin\s+on\s+`develop`": "scheduler documentation is branch-relative instead of version-relative",
    r"final\s+v0\.5\.0\s+release\s+candidate\s+must\s+repeat": "documentation still describes final v0.5 qualification as a pending repository state",
    r"release\s+candidate\s+must\s+therefore\s+be\s+qualified": "documentation still describes final v0.5 qualification as a pending repository state",
    r"Remaining\s+v0\.5\.0\s+release\s+gates": "roadmap still presents completed v0.5 engineering gates as pending",
    r"Humanity has already invented enough": "release documentation contains conversational/editorial text",
}
for pattern, description in stale_patterns.items():
    if re.search(pattern, combined, flags=re.IGNORECASE):
        fail(description)

validation = (ROOT / "validation/stm32/README.md").read_text(encoding="utf-8")
if "38 hardware benchmark images" not in validation:
    fail("validation/stm32/README.md does not describe the 38-image release benchmark matrix")
if "do not commit generated qualification evidence" not in validation.lower():
    fail("validation/stm32/README.md does not forbid committing generated qualification evidence")
if "GitHub Release asset" not in validation:
    fail("validation/stm32/README.md does not direct release evidence to a GitHub Release asset")
if "validation/stm32/releases/vX.Y.Z/" not in validation:
    fail("validation/stm32/README.md does not preserve the runner's local vX.Y.Z evidence-directory convention")
if "`vX.Y.Z` tag" not in validation:
    fail("validation/stm32/README.md does not document the v-prefixed Git release-tag convention")

release_validation = (ROOT / "validation/stm32/releases/README.md").read_text(encoding="utf-8")
if "gitignored" not in release_validation.lower():
    fail("release qualification retention guidance does not state that local evidence is gitignored")
if "validation/stm32/releases/v0.5.0/" not in release_validation:
    fail("release qualification retention guidance does not document the local v-prefixed directory convention")
if "tags are `v0.5.0`" not in release_validation:
    fail("release qualification retention guidance does not document the v-prefixed Git tag convention")

qualification = (ROOT / "docs/QUALIFICATION.md").read_text(encoding="utf-8")
if "tag `v0.5.0` on `main`" not in qualification:
    fail("docs/QUALIFICATION.md does not state the repository's exact v0.5.0 tag convention")
if "validation/stm32/releases/vX.Y.Z/" not in qualification:
    fail("docs/QUALIFICATION.md does not match the manual runner's local evidence-directory convention")

manual = (ROOT / "docs/STM32_MANUAL_TESTS.md").read_text(encoding="utf-8")
if "validation/stm32/releases/vX.Y.Z/" not in manual:
    fail("STM32 manual documentation does not match the runner's local evidence-directory convention")
if "repository `vX.Y.Z` release-tag convention" not in manual:
    fail("STM32 manual documentation does not state the v-prefixed Git release-tag convention")

signal_readme = (ROOT / "examples/hardrt_h755_dwt_timing/README.md").read_text(encoding="utf-8")
if "HARDRT_CFG_MAX_TASKS=N+1" in signal_readme:
    fail("timing README still documents the invalid one-waiter N+1 capacity rule")

events = (ROOT / "docs/EVENTS_NOTIFICATIONS.md").read_text(encoding="utf-8")
if "pending remains set while the decremented value is still non-zero" not in events:
    fail("task-notification take semantics do not document residual pending state")
if "pending notification whose value is zero" not in events:
    fail("task-notification documentation omits the zero-valued pending take edge case")

cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
if "project(${LIB_NAME} VERSION 0.5.0 LANGUAGES C)" not in cmake:
    fail("CMake project version is not 0.5.0")
if "COMPATIBILITY SameMinorVersion" not in cmake:
    fail("pre-1.0 CMake package compatibility is not restricted to the same minor line")

installed_consumer = (ROOT / "tests/installed_consumer/CMakeLists.txt").read_text(encoding="utf-8")
if "find_package(HardRT 0.5.0 REQUIRED)" not in installed_consumer:
    fail("installed-package consumer is not validating the v0.5.0 package contract")

cpp_header = (ROOT / "cpp/hardrtpp.hpp").read_text(encoding="utf-8")
if 'Version string (e.g., "0.4.0")' in cpp_header:
    fail("C++ public header still advertises the 0.4.0 version-string example")
if 'Version string (e.g., "0.5.0")' not in cpp_header:
    fail("C++ public header does not advertise the current 0.5.0 version-string example")

if errors:
    print("Documentation gate FAILED:", file=sys.stderr)
    for error in errors:
        print(f"  - {error}", file=sys.stderr)
    sys.exit(1)

print(f"Documentation gate PASS: {len(MARKDOWN)} tracked Markdown files checked")
