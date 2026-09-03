# STM32H755 Qualification Evidence

Physical-board qualification has two evidence classes:

1. **Development runs** are generated under `.qualification/stm32/` by default. That tree is gitignored and may contain any number of local experiments, regressions, or performance checkpoints.
2. **Release evidence** is stored under `validation/stm32/releases/vX.Y.Z/`. The repository keeps exactly one promoted qualification package per release.

Do not commit timestamped development runs.

## Development run

```bash
./scripts/stm32_manual_test_full.sh \
  /path/to/STM32CubeH7 \
  --clean-builds
```

The runner writes a timestamped local directory containing `qualification.md` and `raw/` logs. It records the exact HardRT SHA, STM32CubeH7 SHA/state, toolchain versions, automated results, and the small amount of human visual feedback required for the LED examples.

For LED validation the human criterion is intentionally qualitative: both LEDs must visibly toggle and the configured relative rate must be obvious (for example one is roughly twice as fast). The tester is **not** expected to visually distinguish 200 ms from 250 ms or perform stopwatch-grade timing. Timing correctness is covered by automated counters/DWT measurements instead.

## Release promotion

Once the exact release-candidate SHA has a complete PASS run:

```bash
./scripts/promote_stm32_qualification.sh \
  .qualification/stm32/<run-id> \
  v0.5.0
```

Promotion refuses:

- a non-PASS run;
- tracked source modifications;
- a dirty or unrecorded STM32CubeH7 checkout;
- a run whose recorded HardRT SHA differs from current HEAD;
- a second package for the same release unless `--replace` is explicitly supplied.

The destination is exactly `validation/stm32/releases/vX.Y.Z/`, so a release cannot accumulate multiple timestamped qualification directories by accident.

## Current hardware matrix

The full runner validates:

- board/OpenOCD connectivity;
- C and C++ task execution with automated progress counters plus qualitative LED-rate observation;
- scheduler/task lifecycle progress, including task return/re-entry behavior used by the demo;
- DWT `event_to_task`, `sem_isr_ready`, and `ready_to_task` timing;
- fixed-priority ISR preemption;
- `PRIORITY_RR` queue precedence and retained quantum;
- semaphore counting/saturation plus real-ISR wake/preemption;
- queue FIFO/full/empty behavior, ISR send -> blocked receiver, and ISR receive -> blocked sender with payload checks;
- mutex ownership, blocking, direct handoff, and priority-preempting unlock.

The matrix should grow whenever a feature gains Cortex-M-specific behavior. Pure data/argument behavior that is already exhaustively deterministic in hosted tests does not need a decorative duplicate hardware test; port-sensitive scheduling, interrupt, critical-section, timing, and context-switch behavior does.
