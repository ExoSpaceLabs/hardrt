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
    r"tag\s+`v0\.5\.0`": "release documentation uses a v-prefixed 0.5.0 tag instead of the established 0.5.0 convention",
    r"qualified\s+`vX\.Y\.Z`\s+tag": "release documentation uses a v-prefixed release-tag convention",
    r"current\s+`develop`\s+behavior": "release documentation is branch-relative instead of version-relative",
    r"on\s+the\s+`develop`\s+branch": "release documentation describes the public contract as develop-only",
    r"implemented\s+on\s+`develop`": "release documentation describes the public contract as develop-only",
    r"true\s+global\s+round-robin\s+on\s+`develop`": "scheduler documentation is branch-relative instead of version-relative",
    r"final\s+v0\.5\.0\s+release\s+candidate\s+must\s+repeat": "documentation still describes final v0.5 qualification as a pending repository state",
    r"release\s+candidate\s+must\s+therefore\s+be\s+qualified": "documentation still describes final v0.5 qualification as a pending repository state",
    r"Remaining\s+v0\.5\.0\s+release\s+gates": "roadmap still presents completed v0.5 engineering gates as pending",
    r"Humanity has already invented enough": "release documentation contains conversational/editorial text",
    r"slice\s*==\s*0[^\n.]*creates\s+a\s+cooperative\s+task": "zero timeslice is still documented as globally cooperative",
    r"transfers\s+task\s+context\s+only\s+when\s+the\s+running\s+task\s+reaches\s+a\s+HardRT\s+scheduling\s+point": "POSIX documentation still describes the removed cooperative execution model",
    r"does\s+not\s+require\s+re-running\s+the\s+v0\.5\.0\s+physical\s+STM32\s+timing\s+campaign": "0.5.1 documentation contradicts the exact-SHA hardware qualification policy",
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
if "validation/stm32/releases/X.Y.Z/" not in validation:
    fail("validation/stm32/README.md does not preserve the runner's local X.Y.Z evidence-directory convention")
if "`X.Y.Z` tag" not in validation:
    fail("validation/stm32/README.md does not document the non-v-prefixed Git release-tag convention")

release_validation = (ROOT / "validation/stm32/releases/README.md").read_text(encoding="utf-8")
if "gitignored" not in release_validation.lower():
    fail("release qualification retention guidance does not state that local evidence is gitignored")
if "validation/stm32/releases/0.5.0/" not in release_validation:
    fail("release qualification retention guidance does not document the local X.Y.Z directory convention")
if "tags are `0.5.0`" not in release_validation:
    fail("release qualification retention guidance does not document the non-v-prefixed Git tag convention")

qualification = (ROOT / "docs/QUALIFICATION.md").read_text(encoding="utf-8")
if "tag `0.5.0` was created on `main`" not in qualification:
    fail("docs/QUALIFICATION.md does not preserve the repository's historical 0.5.0 tag convention")
if "## v0.5.1 corrective release evidence contract" not in qualification:
    fail("docs/QUALIFICATION.md does not define the 0.5.1 corrective-release evidence contract")
if "tag `0.5.1`" not in qualification:
    fail("docs/QUALIFICATION.md does not state the exact 0.5.1 release-tag convention")
if "default unfiltered STM32 qualification command" not in qualification:
    fail("docs/QUALIFICATION.md does not require a full unfiltered 0.5.1 hardware run")
if "validation/stm32/releases/X.Y.Z/" not in qualification:
    fail("docs/QUALIFICATION.md does not match the manual runner's local evidence-directory convention")

manual = (ROOT / "docs/STM32_MANUAL_TESTS.md").read_text(encoding="utf-8")
if "validation/stm32/releases/X.Y.Z/" not in manual:
    fail("STM32 manual documentation does not match the runner's local evidence-directory convention")
if "repository `X.Y.Z` release-tag convention" not in manual:
    fail("STM32 manual documentation does not state the non-v-prefixed Git release-tag convention")

signal_readme = (ROOT / "examples/hardrt_h755_dwt_timing/README.md").read_text(encoding="utf-8")
if "HARDRT_CFG_MAX_TASKS=N+1" in signal_readme:
    fail("timing README still documents the invalid one-waiter N+1 capacity rule")

events = (ROOT / "docs/EVENTS_NOTIFICATIONS.md").read_text(encoding="utf-8")
if "pending remains set while the decremented value is still non-zero" not in events:
    fail("task-notification take semantics do not document residual pending state")
if "pending notification whose value is zero" not in events:
    fail("task-notification documentation omits the zero-valued pending take edge case")

intro = (ROOT / "docs/INTRODUCTION.md").read_text(encoding="utf-8")
if "HardRT 0.5.1 provides:" not in intro:
    fail("introduction does not describe the current 0.5.1 feature contract")
if "maps HardRT application tasks to pthreads" not in intro:
    fail("introduction does not describe the corrected pthread-backed POSIX model")

cpp_doc = (ROOT / "docs/CPP.md").read_text(encoding="utf-8")
if "does not disable scheduler-policy preemption" not in cpp_doc:
    fail("C++ wrapper documentation does not distinguish zero timeslice from disabling preemption")

build_doc = (ROOT / "docs/BUILD.md").read_text(encoding="utf-8")
if "`stack_words` pointer and `n_words` count" not in build_doc:
    fail("build documentation does not use the actual hrt_create_task stack parameter names")
if "fresh **unfiltered** STM32H755 release-candidate run" not in build_doc:
    fail("build documentation does not require fresh full physical qualification for 0.5.1")

release_notes = (ROOT / "RELEASE_NOTES.md").read_text(encoding="utf-8")
if "`stack_mem`" in release_notes:
    fail("release notes name a nonexistent hrt_create_task stack_mem parameter")
if "fresh full STM32H755 physical qualification run" not in release_notes:
    fail("0.5.1 release notes omit the required exact-SHA physical qualification gate")
if "humanity" in release_notes.lower():
    fail("release notes contain conversational/editorial text")

documentation = (ROOT / "docs/DOCUMENTATION.md").read_text(encoding="utf-8")
if "configures/builds HardRT 0.5.1" not in documentation:
    fail("documentation-gate description is not aligned to HardRT 0.5.1")

cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
if "project(${LIB_NAME} VERSION 0.5.1 LANGUAGES C)" not in cmake:
    fail("CMake project version is not 0.5.1")
if "COMPATIBILITY SameMinorVersion" not in cmake:
    fail("pre-1.0 CMake package compatibility is not restricted to the same minor line")

# Keep this consumer pinned to 0.5.0: a 0.5.1 SameMinorVersion package must
# remain consumable by applications that requested the original 0.5 contract.
installed_consumer = (ROOT / "tests/installed_consumer/CMakeLists.txt").read_text(encoding="utf-8")
if "find_package(HardRT 0.5.0 REQUIRED)" not in installed_consumer:
    fail("installed-package consumer is not validating 0.5.0 -> 0.5.1 patch compatibility")

c_header = (ROOT / "inc/hardrt.h").read_text(encoding="utf-8")
if 'semantic version string, for example "0.5.1"' not in c_header:
    fail("C public header does not advertise the current 0.5.1 version-string example")

cpp_header = (ROOT / "cpp/hardrtpp.hpp").read_text(encoding="utf-8")
if 'Version string (e.g., "0.4.0")' in cpp_header:
    fail("C++ public header still advertises the 0.4.0 version-string example")
if 'Version string (e.g., "0.5.1")' not in cpp_header:
    fail("C++ public header does not advertise the current 0.5.1 version-string example")

if errors:
    print("Documentation gate FAILED:", file=sys.stderr)
    for error in errors:
        print(f"  - {error}", file=sys.stderr)
    sys.exit(1)

print(f"Documentation gate PASS: {len(MARKDOWN)} tracked Markdown files checked")
