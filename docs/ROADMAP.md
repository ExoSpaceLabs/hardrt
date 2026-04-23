# 🧭 HardRT Roadmap

This roadmap outlines the evolution of HardRT toward the planned 1.0.0 release.

> Roadmap items may shift as implementation priorities change.

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

## ⚙️ Next synchronization work

- Timeout variants of IPC (`hrt_sem_take_timeout`, queue timeout variants, mutex timed lock if adopted)
- Event flags / task notification API
- Priority inheritance or another priority inversion mitigation strategy for mutexes

## 🕒 Timing work

- Tickless idle
- High-resolution timers
- `hrt_delay_until()` API

## 🧩 Broader platform work

- CM4↔CM7 communication primitives (AMP)
- Shared memory mailbox interface
- More Cortex-M validation and production hardening

## 🧪 Verification work

- Continuous integration with broader coverage
- More Cortex-M validation scenarios
- Static analysis and MISRA-oriented cleanup

## 🏁 1.0.0 target themes

- Verified Cortex-M port
- Deterministic scheduler with clear behavioral guarantees
- Complete IPC suite: semaphores, mutexes, queues, events
- Timing primitives beyond basic sleep/tick
- Documentation freeze
