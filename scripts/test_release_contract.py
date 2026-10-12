#!/usr/bin/env python3
"""Regression gate for hardware-qualified tree equivalence (#103).

Uses a synthetic disposable Git repository. No physical H755 results are
fabricated or implied.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

GUARD = Path(__file__).with_name("check_release_qualification_diff.py").resolve()


class ReleaseInputEquivalenceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix="hardrt-release-guard-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.git("init", "-q")
        self.git("config", "user.name", "Release Guard Test")
        self.git("config", "user.email", "guard@example.invalid")
        for name in ("src/core/core.c", "inc/hardrt.h", "CMakeLists.txt",
                     "VERSION", ".github/workflows/release.yml",
                     "tests/test_api.c", "README.md", "docs/SCHEDULING.md"):
            self.write(name, "baseline\n")
        self.git("add", ".")
        self.git("commit", "-qm", "qualified baseline")
        self.qualified = self.git("rev-parse", "HEAD").stdout.strip()

    def git(self, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(["git", *args], cwd=self.root,
                              check=True, text=True, capture_output=True)

    def write(self, path: str, text: str) -> None:
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")

    def evaluate(self, paths: dict[str, str], expected_ok: bool) -> None:
        for path, contents in paths.items():
            self.write(path, contents)
        self.git("add", "-A")
        self.git("commit", "--allow-empty", "-qm", "promoted release merge content")
        tagged = self.git("rev-parse", "HEAD").stdout.strip()
        self.assertNotEqual(self.qualified, tagged)
        result = subprocess.run(
            [sys.executable, str(GUARD), self.qualified, tagged],
            cwd=self.root, text=True, capture_output=True,
        )
        self.assertEqual(result.returncode == 0, expected_ok,
                         msg=result.stdout + result.stderr)

    def test_different_sha_same_tree_passes(self) -> None:
        self.evaluate({}, True)

    def test_documentation_only_passes(self) -> None:
        self.evaluate({"README.md": "revised\n",
                       "docs/SCHEDULING.md": "revised\n"}, True)

    def test_kernel_source_change_fails(self) -> None:
        self.evaluate({"src/core/core.c": "changed\n"}, False)

    def test_public_header_change_fails(self) -> None:
        self.evaluate({"inc/hardrt.h": "changed\n"}, False)

    def test_cmake_change_fails(self) -> None:
        self.evaluate({"CMakeLists.txt": "changed\n"}, False)

    def test_version_change_fails(self) -> None:
        self.evaluate({"VERSION": "0.7.0\n"}, False)

    def test_workflow_change_fails(self) -> None:
        self.evaluate({".github/workflows/release.yml": "changed\n"}, False)

    def test_hardware_test_change_fails(self) -> None:
        self.evaluate({"tests/test_api.c": "changed\n"}, False)

    def test_unclassified_new_file_fails(self) -> None:
        self.evaluate({"extras/new-input.conf": "new\n"}, False)

    def test_linker_startup_change_fails(self) -> None:
        self.evaluate({"examples/hardrt_h755_blinky/linker/flash.ld": "new\n"}, False)


if __name__ == "__main__":
    unittest.main(verbosity=2)
