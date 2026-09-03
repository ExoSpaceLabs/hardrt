# STM32H755 Qualification Evidence

Physical-board qualification has two evidence classes:

1. **Development runs** are generated under `.qualification/stm32/` and are gitignored. They may contain any number of local experiments, regressions, and performance checkpoints.
2. **Release evidence** is stored under `validation/stm32/releases/vX.Y.Z/`. The repository keeps exactly one promoted qualification package per release.

Do not commit timestamped development runs.

## Development run

Use the complete wrapper rather than the legacy base runner directly:

```bash
./scripts/stm32_qualification.sh \
  /path/to/STM32CubeH7 \
  --clean-builds
```

The wrapper writes a timestamped local directory under `.qualification/stm32/`, runs the base matrix plus the extended Cortex-M cases, and appends `Full matrix overall: PASS/FAIL` to the report.

For LED validation the human criterion is deliberately qualitative: both LEDs must visibly toggle and the configured relative rate must be obvious, for example one is roughly twice as fast. The tester is **not** expected to distinguish exact millisecond periods by eye. Automated counters and DWT measurements provide the quantitative evidence.

## Release promotion

Once the exact release-candidate SHA has a complete PASS run:

```bash
./scripts/promote_stm32_qualification.sh \
  .qualification/stm32/<run-id> \
  v0.5.0
```

Promotion refuses:

- an incomplete or failing full hardware matrix;
- tracked source modifications;
- a dirty or unrecorded STM32CubeH7 checkout;
- a run whose recorded HardRT SHA differs from current HEAD;
- a second package for the same release unless `--replace` is explicitly supplied.

The destination is exactly `validation/stm32/releases/vX.Y.Z/`, preventing accumulation of timestamped development reports in the repository.

## Current hardware matrix

The full wrapper currently executes **13 cases**:

1. board/OpenOCD connectivity;
2. C task/blinky integration;
3. C++ task/blinky integration;
4. scheduler counter/task-lifecycle demo;
5. DWT `event_to_task` timing;
6. DWT `sem_isr_ready` timing;
7. DWT `ready_to_task` timing;
8. fixed-priority ISR preemption;
9. `PRIORITY_RR` queue precedence and retained quantum;
10. semaphore counting/saturation plus real-ISR wake/preemption;
11. queue FIFO/full/empty behavior, ISR send -> blocked receiver, and ISR receive -> blocked sender with payload checks;
12. mutex ownership, blocking, direct handoff, and priority-preempting unlock;
13. external tick integration using periodic TIM2 interrupts to drive `hrt_tick_from_isr()`, wake sleeping work, and preempt a lower-priority task while SysTick remains disabled.

Hardware coverage should grow when a feature has Cortex-M-specific timing, interrupt, critical-section, or context-switch behavior. Pure data/argument edge cases stay primarily in the broader hosted suite unless the port can materially change their behavior.
