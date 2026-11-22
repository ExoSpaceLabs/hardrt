#!/usr/bin/env python3
"""Regenerate STM32H755 timing CSV from a single HardRT qualification run.

Observed DWT cycle counts are configuration-specific diagnostics, not WCET.
The source inputs live in the untracked run's raw/ folder; no historical
logs need to be committed or maintained. Fail closed on malformed PASS data.
"""
from __future__ import annotations

import argparse
import csv
import re
import sys
import tempfile
from pathlib import Path

COLUMNS = [
    "case", "metric", "profile", "source_sha", "core_hz", "samples",
    "min_cycles", "avg_cycles", "max_cycles", "source_file",
]
SYNC_METRICS = [
    "sem_take_available", "sem_give_uncontended",
    "mutex_lock_uncontended", "mutex_unlock_uncontended",
    "queue_send_4", "queue_recv_4", "queue_send_16", "queue_recv_16",
    "queue_send_64", "queue_recv_64", "queue_send_256", "queue_recv_256",
    "queue_send_1024", "queue_recv_1024",
]
TRIPLE = re.compile(r"^([a-z_]+)=(\d+) / (\d+) / (\d+) cycles", re.M)
SAMPLE = re.compile(
    r"^min=(\d+) cycles, avg=(\d+) cycles \(sum/count=\d+\), max=(\d+) cycles$",
    re.M,
)


def field(content: str, pattern: str, label: str) -> str:
    match = re.search(pattern, content, re.M)
    if match is None:
        raise ValueError(f"missing {label}")
    return match.group(1)


def row(case: str, metric: str, profile: str, source_sha: str,
        core_hz: int, samples: int, lo: int, avg: int, hi: int,
        source_file: str) -> dict[str, str | int]:
    if core_hz <= 0 or samples <= 0 or not (0 <= lo <= avg <= hi):
        raise ValueError(f"invalid DWT range for {case}/{metric}")
    return dict(zip(COLUMNS, (case, metric, profile, source_sha, core_hz,
                               samples, lo, avg, hi, source_file)))


def parse_timing(path: Path, run: Path, source_sha: str,
                 target_samples: int) -> list[dict[str, str | int]]:
    data = path.read_text(encoding="utf-8", errors="replace")
    if len(re.findall(r"^RESULT: PASS$", data, re.M)) != 1:
        raise ValueError(f"{path.name}: missing unique PASS")
    case = field(data, r"^case=([a-z_]+)$", "case")
    core_hz = int(field(data, r"^SystemCoreClock=(\d+) Hz$", "clock"))
    samples = int(field(data, r"^count=(\d+)$", "sample count"))
    if samples != target_samples:
        raise ValueError(f"{path.name}: {samples} != {target_samples} samples")
    hit = SAMPLE.search(data)
    if hit is None:
        raise ValueError(f"{path.name}: missing primary DWT range")
    profile = field(data, r"^HardRT timing profile=([a-z]+)", "timing profile")
    origin = str(path.relative_to(run))
    rows = [row(case, case, profile, source_sha, core_hz, samples,
                *(int(x) for x in hit.groups()), origin)]
    if case == "scheduler_decision":
        metrics = ("pendsv_save", "pendsv_restore", "pendsv_software",
                   "pendsv_to_task", "irq_tail_to_pendsv")
        triples = {name: (int(lo), int(avg), int(hi))
                   for name, lo, avg, hi in TRIPLE.findall(data)}
        for name in metrics:
            if name not in triples:
                raise ValueError(f"{path.name}: missing {name}")
            rows.append(row(case, name, profile, source_sha, core_hz,
                            samples, *triples[name], origin))
    return rows


def parse_sync(run: Path, source_sha: str, target_samples: int
               ) -> list[dict[str, str | int]]:
    source = run / "raw" / "sync_benchmark_metrics.csv"
    if not source.exists():
        return []
    gdb = (run / "raw" / "sync_benchmark_gdb.log").read_text(
        encoding="utf-8", errors="replace")
    if len(re.findall(r"^RESULT: PASS$", gdb, re.M)) != 1:
        raise ValueError("sync benchmark missing unique PASS")
    core_hz = int(field(gdb, r"^core_hz=(\d+)", "sync clock"))
    with source.open(newline="", encoding="utf-8") as stream:
        reader = list(csv.DictReader(stream))
    if len(reader) != 14:
        raise ValueError(f"sync benchmark expected 14 rows; got {len(reader)}")
    rows = []
    for number, item in enumerate(reader):
        if int(item["id"]) != number or int(item["count"]) != target_samples:
            raise ValueError("sync benchmark row index/sample count mismatch")
        for metric, prefix in (("api", "api"), ("masked", "masked")):
            rows.append(row("sync_" + SYNC_METRICS[number], metric, "diagnostic",
                            source_sha, core_hz, target_samples,
                            *(int(item[prefix + "_" + part])
                              for part in ("min", "avg", "max")),
                            str(source.relative_to(run))))
    return rows


def parse_periodic(run: Path, source_sha: str
                   ) -> list[dict[str, str | int]]:
    path = run / "raw" / "periodic_delay_gdb.log"
    if not path.exists():
        return []
    data = path.read_text(encoding="utf-8", errors="replace")
    if len(re.findall(r"^RESULT: PASS$", data, re.M)) != 1:
        raise ValueError("periodic validation missing unique PASS")
    hit = re.search(
        r"^periodic_tick_to_task=(\d+) / (\d+) / (\d+) cycles "
        r"\(min/avg/max\); samples=(\d+)$", data, re.M)
    if hit is None:
        raise ValueError("periodic validation missing DWT data")
    lo, avg, hi, samples = (int(x) for x in hit.groups())
    # Periodic validator runs at a fixed 16-release workload.
    if samples != 16:
        raise ValueError("periodic release count must be 16")
    # The validation GDB record now includes the actual hardware clock.
    core_hz = int(field(data, r"^periodic_core_hz=(\d+)$", "periodic clock"))
    return [row("periodic_delay", "periodic_tick_to_task", "none",
                source_sha, core_hz, samples, lo, avg, hi,
                str(path.relative_to(run)))]


def export(run: Path, output: Path, source_sha: str,
           target_samples: int, mode: str) -> int:
    raw = run / "raw"
    if not raw.is_dir():
        raise ValueError(f"missing raw directory: {raw}")
    results: list[dict[str, str | int]] = []
    for pattern in ("timing_*_gdb.log", "tick_*_gdb.log"):
        for path in sorted(raw.glob(pattern)):
            # tick image has no 'case=' line, but shares the DWT range schema.
            if pattern.startswith("tick"):
                data = path.read_text(encoding="utf-8", errors="replace")
                if len(re.findall(r"^RESULT: PASS$", data, re.M)) != 1:
                    raise ValueError(f"{path.name}: missing unique PASS")
                clock = int(field(data, r"^SystemCoreClock=(\d+) Hz$", "tick clock"))
                n = int(field(data, r"^count=(\d+)$", "tick samples"))
                match = SAMPLE.search(data)
                if n != target_samples or match is None:
                    raise ValueError(f"{path.name}: invalid tick samples/range")
                case = path.name.removesuffix("_gdb.log")
                results.append(row(case, "tick_from_isr", "none", source_sha,
                                   clock, n, *(int(v) for v in match.groups()),
                                   str(path.relative_to(run))))
            else:
                results.extend(parse_timing(path, run, source_sha, target_samples))
    results.extend(parse_sync(run, source_sha, target_samples))
    results.extend(parse_periodic(run, source_sha))
    if mode != "functional" and not results:
        raise ValueError("no hardware timing records to export")
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=COLUMNS)
        writer.writeheader()
        writer.writerows(results)
    return len(results)


def self_test() -> None:
    with tempfile.TemporaryDirectory() as temp:
        run = Path(temp)
        raw = run / "raw"
        raw.mkdir()
        (raw / "timing_yield_switch_gdb.log").write_text(
            "case=yield_switch\nSystemCoreClock=64000000 Hz\n"
            "HardRT timing profile=none\ncount=8\n"
            "min=100 cycles, avg=120 cycles (sum/count=120), max=130 cycles\n"
            "RESULT: PASS\n", encoding="utf-8")
        output = run / "metrics.csv"
        assert export(run, output, "abc", 8, "benchmark") == 1
        with output.open(newline="", encoding="utf-8") as file:
            assert next(csv.DictReader(file))["metric"] == "yield_switch"
        (raw / "timing_yield_switch_gdb.log").write_text("RESULT: FAIL\n")
        try:
            export(run, output, "abc", 8, "benchmark")
        except ValueError:
            pass
        else:
            raise AssertionError("malformed fixture accepted")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-dir", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--source-sha")
    parser.add_argument("--samples", type=int)
    parser.add_argument("--mode", choices=("all", "functional", "benchmark"),
                        default="all")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        if args.self_test:
            self_test()
            print("STM32 timing export self-test: PASS")
            return 0
        if (args.run_dir is None or args.output is None or
                not args.source_sha or not args.samples or args.samples < 1):
            parser.error("run-dir, output, source-sha, and positive samples required")
        count = export(args.run_dir, args.output, args.source_sha,
                       args.samples, args.mode)
        print(f"exported {count} DWT metric rows to {args.output}")
        return 0
    except (ValueError, OSError, KeyError) as exc:
        print(f"STM32 timing export rejected: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
