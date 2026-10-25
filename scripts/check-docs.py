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

# Current-release capability and version assertions are evaluated against
# root VERSION, not hard-coded against the previous historical release.
version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
    fail(f"root VERSION is not semantic MAJOR.MINOR.PATCH: {version!r}")
cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
if 'file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" HARDRT_VERSION)' not in cmake:
    fail("CMake must read root VERSION when configuring the project")
if 'project(${LIB_NAME} VERSION ${HARDRT_VERSION} LANGUAGES C)' not in cmake:
    fail("CMake project version must derive from VERSION")

current_readme = (ROOT / "README.md").read_text(encoding="utf-8")
if f"**Current source version:** `{version}`" not in current_readme:
    fail("README current source version does not match root VERSION")
if "a final run is required on the corrected release-version candidate" in current_readme:
    fail("README incorrectly describes completed 0.6 hardware qualification as pending")
status = (ROOT / "docs/MODULE_STATUS.md").read_text(encoding="utf-8")
if f"HardRT **{version}**" not in status:
    fail("Module status does not describe the current VERSION")
if "require a fresh frozen candidate before publication" in status:
    fail("Module status incorrectly describes completed 0.6 qualification as pending")
intro = (ROOT / "docs/INTRODUCTION.md").read_text(encoding="utf-8")
if f"HardRT {version} provides:" not in intro:
    fail("Introduction version does not match root VERSION")
notes = (ROOT / "RELEASE_NOTES.md").read_text(encoding="utf-8")
if f"# HardRT v{version} Release Notes" not in notes:
    fail("No current VERSION release-note section")
for header, symbols in {
    "inc/hardrt.h": ("hrt_delay_until", "HRT_WAIT_TIMEOUT"),
    "inc/hardrt_sem.h": ("hrt_sem_take_until",),
    "inc/hardrt_mutex.h": ("hrt_mutex_lock_until", "HARDRT_MAX_MUTEXES_HELD"),
    "inc/hardrt_queue.h": ("hrt_queue_send_until", "hrt_queue_recv_until"),
    "inc/hardrt_event.h": ("hrt_event_wait_until",),
    "inc/hardrt_notify.h": ("hrt_task_notify_wait_until", "hrt_task_notify_take_until"),
}.items():
    text = (ROOT / header).read_text(encoding="utf-8")
    for symbol in symbols:
        if symbol not in text:
            fail(f"missing implemented v0.6 public API {symbol} in {header}")
for doc, words in {
    "README.md": ("absolute-tick IPC timeout engine", "priority inheritance", "52 benchmark metrics"),
    "docs/MUTEXES.md": ("hrt_mutex_lock_until", "transitive priority inheritance"),
    "docs/MODULE_STATUS.md": ("finite-wait", "transitive PI", "52/52"),
}.items():
    body = (ROOT / doc).read_text(encoding="utf-8")
    for word in words:
        if word not in body:
            fail(f"{doc} fails to document implemented 0.6 feature: {word}")
for required in ("VERSION", ".github/workflows/tag-release.yml", "scripts/test_release_contract.py"):
    if not (ROOT / required).is_file():
        fail(f"missing root version / automatic tagging release gate: {required}")


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
    ".github/workflows/release.yml",
    "scripts/run-all-examples.sh",
    "scripts/stm32_manual_test_full.sh",
    "scripts/package_stm32_qualification.sh",
    "scripts/finalize_release.sh",
    "scripts/build-stm32-examples-ci.sh",
    "scripts/build-lib-stm32h7xx-dwt-timing.sh",
    "scripts/extract_release_notes.py",
    "scripts/check_release_qualification_diff.py",
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
    "docs/QUALIFICATION.md",
    "docs/RELEASE_PROCESS.md",
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
    r"does\s+not\s+require\s+re-running\s+the\s+v0\.5\.0\s+physical\s+STM32\s+timing\s+campaign": "0.5.1 documentation contradicts the hardware qualification policy",
    r"hardrt-posix-X\.Y\.Z\.tar\.gz": "release documentation still uses an architecture-ambiguous POSIX package name",
    r"hardrt-bundle-X\.Y\.Z\.tar\.gz": "release documentation still describes the removed combined bundle",
}
for pattern, description in stale_patterns.items():
    if re.search(pattern, combined, flags=re.IGNORECASE):
        fail(description)

validation = (ROOT / "validation/stm32/README.md").read_text(encoding="utf-8")
if "52 benchmark metrics across 39 images" not in validation:
    fail("validation/stm32/README.md does not require the 52-metric / 39-image release benchmark matrix")
if "original 38 images" not in validation:
    fail("validation/stm32/README.md does not preserve the historical 38-image baseline")
if "do not commit generated qualification evidence" not in validation.lower():
    fail("validation/stm32/README.md does not forbid committing generated qualification evidence")
if "hardrt-stm32-qualification-X.Y.Z.tar.xz" not in validation:
    fail("validation/stm32/README.md does not define the compressed physical qualification asset")
if "scripts/package_stm32_qualification.sh" not in validation:
    fail("validation/stm32/README.md does not document qualification packaging")
if "scripts/finalize_release.sh" not in validation:
    fail("validation/stm32/README.md does not document release finalization")
if "validation/stm32/releases/X.Y.Z/" not in validation:
    fail("validation/stm32/README.md does not preserve the runner's local X.Y.Z evidence-directory convention")

release_validation = (ROOT / "validation/stm32/releases/README.md").read_text(encoding="utf-8")
if "gitignored" not in release_validation.lower():
    fail("release qualification retention guidance does not state that local evidence is gitignored")
if "validation/stm32/releases/0.5.0/" not in release_validation:
    fail("release qualification retention guidance does not document the local X.Y.Z directory convention")
if "hardrt-stm32-qualification-X.Y.Z.tar.xz" not in release_validation:
    fail("release qualification retention guidance does not define the tar.xz release evidence package")
if "scripts/finalize_release.sh" not in release_validation:
    fail("release qualification retention guidance does not document finalization")

runner = (ROOT / "scripts/stm32_manual_test_full.sh").read_text(encoding="utf-8")
if "BENCHMARK_TOTAL=52" not in runner or "FUNCTIONAL_TOTAL=16" not in runner:
    fail("physical qualification runner totals are not 16 functional + 52 benchmark metrics")
if "sync_benchmark_case" not in runner or "parse_stm32_sync_benchmark.awk" not in runner:
    fail("physical qualification runner does not execute the 14-metric DWT synchronization image")
package = (ROOT / "scripts/package_stm32_qualification.sh").read_text(encoding="utf-8")
if "EXPECTED_BENCHMARK" not in package or "sync_benchmark_metrics.csv" not in package:
    fail("release evidence packaging does not enforce the expanded benchmark matrix")
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
if "check_release_qualification_diff.py" not in qualification:
    fail("docs/QUALIFICATION.md does not define the post-qualification release-only diff guard")
if "hardware-qualified source SHA" not in qualification:
    fail("docs/QUALIFICATION.md does not distinguish hardware evidence SHA from a release-only follow-up commit")
for required in (
    "hardrt-posix-linux-amd64-X.Y.Z.tar.gz",
    "hardrt-posix-linux-arm64-X.Y.Z.tar.gz",
    "hardrt-stm32-qualification-X.Y.Z.tar.xz",
    "scripts/finalize_release.sh",
    "draft",
):
    if required not in qualification:
        fail(f"docs/QUALIFICATION.md is missing standardized release contract: {required}")

manual = (ROOT / "docs/STM32_MANUAL_TESTS.md").read_text(encoding="utf-8")
for required in (
    "validation/stm32/releases/X.Y.Z/",
    "scripts/package_stm32_qualification.sh",
    "hardrt-stm32-qualification-X.Y.Z.tar.xz",
    "scripts/finalize_release.sh",
):
    if required not in manual:
        fail(f"STM32 manual documentation is missing release-evidence contract: {required}")

release_process = (ROOT / "docs/RELEASE_PROCESS.md").read_text(encoding="utf-8")
for required in (
    "native amd64",
    "native arm64",
    "hardrt-posix-linux-amd64-X.Y.Z.tar.gz",
    "hardrt-posix-linux-arm64-X.Y.Z.tar.gz",
    "hardrt-stm32-qualification-X.Y.Z.tar.xz",
    "Branch cleanup is deliberately **outside** the finalizer",
    "draft",
):
    if required not in release_process:
        fail(f"release process is missing standardized contract: {required}")
if "hardrt-bundle" in release_process:
    fail("release process still references the removed combined bundle")

# Catch contradictory operator instructions that previously survived the
# generic Markdown/link and release helper smoke checks.
release_evidence_guide = (ROOT / "validation/stm32/releases/README.md").read_text(encoding="utf-8")
if "--cleanup-branches" in release_evidence_guide and "deprecated no-op" not in release_evidence_guide:
    fail("release evidence guide implies finalizer automatically removes branches")
if "promoted unchanged to `main`" in manual:
    fail("hardware guide incorrectly equates a qualified develop SHA with a required two-parent main promotion")
cross = (ROOT / "docs/CROSSCOMPILE.md").read_text(encoding="utf-8")
if "timing collection, and evidence packaging" in cross:
    fail("cross-compile guide incorrectly claims the STM32 runner packages release evidence")
runner = (ROOT / "scripts/stm32_manual_test_full.sh").read_text(encoding="utf-8")
if "manually copy/move this full run" in runner:
    fail("STM32 runner still directs operators to copy evidence instead of packaging it")
if "scripts/package_stm32_qualification.sh" not in runner:
    fail("STM32 runner does not identify the release evidence packaging helper")


signal_readme = (ROOT / "examples/hardrt_h755_dwt_timing/README.md").read_text(encoding="utf-8")
if "HARDRT_CFG_MAX_TASKS=N+1" in signal_readme:
    fail("timing README still documents the invalid one-waiter N+1 capacity rule")

events = (ROOT / "docs/EVENTS_NOTIFICATIONS.md").read_text(encoding="utf-8")
if "pending remains set while the decremented value is still non-zero" not in events:
    fail("task-notification take semantics do not document residual pending state")
if "pending notification whose value is zero" not in events:
    fail("task-notification documentation omits the zero-valued pending take edge case")

intro = (ROOT / "docs/INTRODUCTION.md").read_text(encoding="utf-8")
if f"HardRT {version} provides:" not in intro:
    fail("introduction does not describe the current VERSION feature contract")
if "maps HardRT application tasks to pthreads" not in intro:
    fail("introduction does not describe the corrected pthread-backed POSIX model")

cpp_doc = (ROOT / "docs/CPP.md").read_text(encoding="utf-8")
if "does not disable scheduler-policy preemption" not in cpp_doc:
    fail("C++ wrapper documentation does not distinguish zero timeslice from disabling preemption")

build_doc = (ROOT / "docs/BUILD.md").read_text(encoding="utf-8")
if "`stack_words` pointer and `n_words` count" not in build_doc:
    fail("build documentation does not use the actual hrt_create_task stack parameter names")
if "fresh **unfiltered** STM32H755 release-candidate run" not in build_doc:
    fail("build documentation does not preserve the v0.5.1 physical qualification gate")
for required in (
    ".github/workflows/release.yml",
    "hardrt-posix-linux-amd64-X.Y.Z.tar.gz",
    "hardrt-posix-linux-arm64-X.Y.Z.tar.gz",
    "scripts/package_stm32_qualification.sh",
    "scripts/finalize_release.sh",
):
    if required not in build_doc:
        fail(f"build documentation is missing standardized release contract: {required}")

release_notes = (ROOT / "RELEASE_NOTES.md").read_text(encoding="utf-8")
if "`stack_mem`" in release_notes:
    fail("release notes name a nonexistent hrt_create_task stack_mem parameter")
if "fresh full STM32H755 physical qualification run" not in release_notes:
    fail("0.5.1 release notes omit the required physical qualification gate")
if "scripts/check_release_qualification_diff.py" not in release_notes:
    fail("0.5.1 release notes omit the release-only post-qualification diff guard")
if "The candidate is **not published**" in release_notes:
    fail("current VERSION release notes still claim physical qualification is pending")
if "humanity" in release_notes.lower():
    fail("release notes contain conversational/editorial text")

documentation = (ROOT / "docs/DOCUMENTATION.md").read_text(encoding="utf-8")
if "configures/builds the HardRT version from root `VERSION`" not in documentation:
    fail("documentation-gate description must derive version from VERSION")

cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
if 'project(${LIB_NAME} VERSION ${HARDRT_VERSION} LANGUAGES C)' not in cmake:
    fail("CMake project version is not derived from VERSION")
if "COMPATIBILITY SameMinorVersion" not in cmake:
    fail("pre-1.0 CMake package compatibility is not restricted to the same minor line")

# Installed C/C++ consumer must target the current pre-1.0 minor line,
# which is intentionally incompatible with requests for the previous minor.
installed_consumer = (ROOT / "tests/installed_consumer/CMakeLists.txt").read_text(encoding="utf-8")
if f"find_package(HardRT {version} REQUIRED)" not in installed_consumer:
    fail("installed-package consumer does not target the current VERSION")

c_header = (ROOT / "inc/hardrt.h").read_text(encoding="utf-8")
if f'semantic version string, for example "{version}"' not in c_header:
    fail("C public header version example is stale")

cpp_header = (ROOT / "cpp/hardrtpp.hpp").read_text(encoding="utf-8")
if 'Version string (e.g., "0.4.0")' in cpp_header:
    fail("C++ public header still advertises the 0.4.0 version-string example")
if f'Version string (e.g., "{version}")' not in cpp_header:
    fail("C++ public header version example is stale")

release_workflow = (ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
for required in (
    "workflow_call:",
    "Verify tag, current main and qualified inputs",
    "ubuntu-24.04-arm",
    "hardrt-posix-linux-amd64-",
    "hardrt-posix-linux-arm64-",
    "scripts/extract_release_notes.py",
    "SHA256SUMS",
    "draft: true",
    "softprops/action-gh-release@v2",
):
    if required not in release_workflow:
        fail(f"release workflow is missing required contract: {required}")
if "hardrt-bundle" in release_workflow:
    fail("release workflow still produces the removed combined bundle")

linux_workflow = (ROOT / ".github/workflows/ci_posix.yml").read_text(encoding="utf-8")
if "ubuntu-24.04-arm" not in linux_workflow:
    fail("Linux CI does not execute the POSIX suite on native arm64")
if "softprops/action-gh-release" in linux_workflow:
    fail("Linux CI still publishes GitHub Release assets; release.yml must be the single publisher")
if "hardrt-bundle" in linux_workflow:
    fail("Linux CI still assembles the removed combined bundle")

qualification_diff = (ROOT / "scripts/check_release_qualification_diff.py").read_text(encoding="utf-8")
if "ALLOWED_EXACT" not in qualification_diff or "ALLOWED_PREFIXES" not in qualification_diff:
    fail("release qualification equivalence guard does not define a conservative allowlist")
if 'ALLOWED_EXACT = {"README.md", "RELEASE_NOTES.md"}' not in qualification_diff:
    fail("0.6 qualified-input guard must permit only current documentation files")
if 'ALLOWED_PREFIXES = ("docs/",)' not in qualification_diff:
    fail("0.6 qualified-input guard must reject arbitrary non-doc changes")
if 'HISTORICAL_051_SHAS' not in qualification_diff:
    fail("legacy 0.5.1 release requires immutable audited SHA-pair verification")
tagger = (ROOT / ".github/workflows/tag-release.yml").read_text(encoding="utf-8")
for required in ("workflow_run:", "Hardware-qualified SHA:", "Hardware qualification: PASS",
                 "check_release_qualification_diff.py", "uses: ./.github/workflows/release.yml"):
    if required not in tagger:
        fail(f"auto tag workflow is missing mandatory release gate: {required}")

if 'Post-release main maintenance: VERSION $tag already tagged' not in tagger:
    fail("auto tag workflow must skip already-tagged VERSION on ordinary main maintenance")
if 'New develop -> main release promotion requires a VERSION bump' not in tagger:
    fail("auto tag workflow must reject a second release promotion at the same VERSION")

if errors:
    print("Documentation gate FAILED:", file=sys.stderr)
    for error in errors:
        print(f"  - {error}", file=sys.stderr)
    sys.exit(1)

print(f"Documentation gate PASS: {len(MARKDOWN)} tracked Markdown files checked")
