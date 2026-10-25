#!/usr/bin/env python3
"""Conservative conditional mutex blocking calculator for HardRT 0.6.

The caller is responsible for proving finite application bounds. Calculating
with arbitrary inputs is NOT evidence of a Cortex-M WCET/response-time bound.
"""
from __future__ import annotations

import argparse
import json


def positive_integer(raw: str) -> int:
    value = int(raw)
    if value <= 0:
        raise argparse.ArgumentTypeError("the bound must be strictly positive")
    return value


def task_capacity(raw: str) -> int:
    value = positive_integer(raw)
    if value > 254:
        raise argparse.ArgumentTypeError("HardRT application task capacity must be <= 254")
    return value


def nonnegative_integer(raw: str) -> int:
    value = int(raw)
    if value < 0:
        raise argparse.ArgumentTypeError("predecessors cannot be negative")
    return value


def blocking_bound(
    policy: str,
    task_capacity: int,
    fifo_predecessors: int,
    remaining_hold_us: int,
    handoff_dispatch_us: int,
) -> int:
    """Compute K*(H+D) only under separately verified elapsed-time assumptions.

    H includes *all* execution, nested mutex blocking, higher/equal priority
    interference, interrupts and bounded non-mutex waits during each tenure.
    D includes time from release to successor being dispatched/resuming,
    including the final waiter's dispatch. No term may be unbounded.
    """
    if policy not in ("HRT_SCHED_PRIORITY", "HRT_SCHED_PRIORITY_RR"):
        raise ValueError("priority inheritance provides no inversion bound under global RR")
    if type(task_capacity) is not int or not 1 <= task_capacity <= 254:
        raise ValueError("task capacity must be an integer in [1,254]")
    if type(fifo_predecessors) is not int or not 0 <= fifo_predecessors <= task_capacity - 1:
        raise ValueError("FIFO predecessors must be between 0 and N-1")
    for name, value in (("owner tenure", remaining_hold_us),
                        ("handoff/dispatch", handoff_dispatch_us)):
        if type(value) is not int or value <= 0:
            raise ValueError(f"{name} must have a finite positive integer microsecond bound")
    return fifo_predecessors * (remaining_hold_us + handoff_dispatch_us)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--policy", choices=("HRT_SCHED_PRIORITY", "HRT_SCHED_PRIORITY_RR",
                                        "HRT_SCHED_RR"), required=True)
    p.add_argument("--task-capacity", type=task_capacity, required=True,
                   help="compiled HARDRT_APP_MAX_TASKS, excluding the idle slot")
    p.add_argument("--fifo-predecessors", type=nonnegative_integer, required=True,
                   help="validated maximum number of owners ahead in FIFO (<= N-1)")
    p.add_argument("--hold-us", type=positive_integer, required=True,
                   help="proven upper bound on elapsed predecessor ownership tenure")
    p.add_argument("--handoff-us", type=positive_integer, required=True,
                   help="proven bound from predecessor release to successor dispatch")
    p.add_argument("--json", action="store_true", help="machine-readable conditional result")
    args = p.parse_args()

    try:
        total = blocking_bound(args.policy, args.task_capacity,
                               args.fifo_predecessors, args.hold_us,
                               args.handoff_us)
    except ValueError as exc:
        p.error(str(exc))

    result = {
        "status": "CONDITIONAL_NOT_HARDWARE_QUALIFIED",
        "policy": args.policy,
        "application_task_capacity": args.task_capacity,
        "max_fifo_predecessors": args.fifo_predecessors,
        "max_elapsed_ownership_us": args.hold_us,
        "max_release_to_successor_dispatch_us": args.handoff_us,
        "conditional_blocking_bound_us": total,
        "formula": "K*(H+D)",
    }
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(f"Conditional mutex blocking upper bound: {total} us")
        print(f"K={args.fifo_predecessors}, H={args.hold_us} us, D={args.handoff_us} us")
        print("NOT a measured or hardware-qualified WCET. "
              "Independent proof of ALL input bounds is required.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
