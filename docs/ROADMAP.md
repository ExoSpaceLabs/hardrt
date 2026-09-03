# 🧭 HardRT Roadmap

This roadmap outlines the evolution of HardRT toward the planned 1.0.0 release.

> Roadmap items may shift as implementation priorities change.

## 🎯 Hard real-time direction

HardRT is intended to evolve into a small Cortex-M RTOS with documented hard real-time properties, not merely a lightweight scheduler with real-time terminology.

The project should therefore prefer designs that are statically bounded, analyzable, and reproducible over designs that are only convenient or fast on average.

The hard real-time direction means:

- no dynamic allocation in the kernel runtime;
- bounded kernel data structures and explicitly bounded iteration counts;
- bounded critical sections with documented interrupt-priority assumptions;
- deterministic scheduler, wake-up, timeout, and ISR semantics;
- explicit priority-inversion handling for shared resources;
- defined execution-time and memory costs for kernel primitives;
- reproducible Cortex-M timing measurements tied to exact builds and hardware configurations;
- clear separation between measured maxima and proven/configuration-specific upper bounds;
- no hard-real-time claim for the POSIX port, which remains a functional and scheduler-validation environment;
- release gates that prevent unsupported latency or compatibility guarantees from becoming documentation folklore.

Hard real-time qualification is a progressive target. Until the required timing model and evidence exist, public documentation must describe HardRT as moving toward hard real-time guarantees rather than claiming universal hard real-time behavior.

## ✅ Completed foundations

- Core scheduler with static tasks
- Null and POSIX ports
- Cortex-M port foundation
- Binary and counting semaphores
- Mutex primitive
- Message queues
- Version and port metadata via CMake
- C and C++ example set for tasks, semaphores, and queues
- POSIX test harness expansion

## 🧱 Contract and determinism work

Before expanding the API significantly, the kernel and port contracts must be made suitable for analysis:

- authoritative private core/port interface;
- removal of kernel internals from installed public headers;
- explicit initialization and lifecycle state machine;
- precise scheduler-policy semantics;
- scheduler-aware ISR wake/preemption decisions;
- explicit application-task versus idle-context capacity;
- compatibility and pre-1.0 versioning policy.

## ⚙️ Next synchronization work

- Timeout variants of IPC (`hrt_sem_take_timeout`, queue timeout variants, mutex timed lock if adopted)
- Event flags / task notification API
- Priority inheritance or another bounded priority-inversion mitigation strategy for mutexes

## 🕒 Timing work

- Reproducible latency model and Cortex-M benchmark evidence
- Worst-case critical-section characterization for kernel primitives
- `hrt_delay_until()` / periodic-task API with bounded release behavior
- Tickless idle, only if its timing and wake-up behavior remain explicitly bounded
- High-resolution timers, only with a defined deterministic contract

## 🧩 Broader platform work

- CM4↔CM7 communication primitives (AMP)
- Shared memory mailbox interface
- More Cortex-M validation and production hardening

## 🧪 Verification work

- Continuous integration with broader coverage
- Compile documentation examples and downstream installed-package consumers
- More Cortex-M validation scenarios
- Static analysis and MISRA-oriented cleanup
- Machine-readable timing results tied to commit, toolchain, hardware, clock, cache, FPU, and IRQ configuration
- Regression thresholds only after normal measurement variance is understood

## 🏁 1.0.0 target themes

- Verified Cortex-M port with explicit supported configuration assumptions
- Deterministic scheduler with clear behavioral guarantees
- Bounded kernel operations suitable for worst-case analysis
- Defined priority-inversion strategy
- Complete IPC suite: semaphores, mutexes, queues, events
- Timing primitives suitable for periodic real-time tasks
- Reproducible evidence for advertised latency bounds
- Documentation and public API freeze
