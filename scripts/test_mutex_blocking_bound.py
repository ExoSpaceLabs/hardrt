#!/usr/bin/env python3
"""Synthetic analysis fixtures, not real STM32 timing qualification."""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import unittest

SCRIPT = Path(__file__).with_name("mutex_blocking_bound.py")
spec = importlib.util.spec_from_file_location("mutex_blocking_bound", SCRIPT)
assert spec is not None and spec.loader is not None
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class BlockingBoundTests(unittest.TestCase):
    def test_seven_ahead(self):
        self.assertEqual(module.blocking_bound("HRT_SCHED_PRIORITY_RR", 8, 7, 200, 40), 1680)

    def test_no_predecessor(self):
        self.assertEqual(module.blocking_bound("HRT_SCHED_PRIORITY", 8, 0, 200, 40), 0)

    def test_upper_capacity(self):
        self.assertEqual(module.blocking_bound("HRT_SCHED_PRIORITY", 254, 253, 1, 1), 506)

    def test_global_rr_not_claimed(self):
        with self.assertRaisesRegex(ValueError, "global RR"):
            module.blocking_bound("HRT_SCHED_RR", 8, 7, 200, 40)

    def test_capacity_is_not_total_tcb_count(self):
        with self.assertRaisesRegex(ValueError, "FIFO predecessors"):
            module.blocking_bound("HRT_SCHED_PRIORITY_RR", 8, 8, 200, 40)

    def test_negative_inputs(self):
        with self.assertRaises(ValueError):
            module.blocking_bound("HRT_SCHED_PRIORITY", 8, -1, 200, 40)
        with self.assertRaises(ValueError):
            module.blocking_bound("HRT_SCHED_PRIORITY", 8, 1, -200, 40)

    def test_missing_finite_hold_bound_rejected(self):
        with self.assertRaises(ValueError):
            module.blocking_bound("HRT_SCHED_PRIORITY", 8, 2, 0, 40)

    def test_missing_dispatch_bound_rejected(self):
        with self.assertRaises(ValueError):
            module.blocking_bound("HRT_SCHED_PRIORITY", 8, 2, 200, 0)

    def test_unknown_policy_fails_closed(self):
        with self.assertRaises(ValueError):
            module.blocking_bound("unknown", 8, 2, 200, 40)

    def test_cli_json_marked_conditional(self):
        p = subprocess.run(
            [sys.executable, str(SCRIPT), "--policy", "HRT_SCHED_PRIORITY_RR",
             "--task-capacity", "8", "--fifo-predecessors", "7",
             "--hold-us", "200", "--handoff-us", "40", "--json"],
            check=True, text=True, capture_output=True,
        )
        output = json.loads(p.stdout)
        self.assertEqual(output["conditional_blocking_bound_us"], 1680)
        self.assertEqual(output["status"], "CONDITIONAL_NOT_HARDWARE_QUALIFIED")

    def test_cli_missing_assumption_fails(self):
        p = subprocess.run(
            [sys.executable, str(SCRIPT), "--policy", "HRT_SCHED_PRIORITY",
             "--task-capacity", "8", "--fifo-predecessors", "7",
             "--hold-us", "200"],
            text=True, capture_output=True,
        )
        self.assertNotEqual(p.returncode, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
