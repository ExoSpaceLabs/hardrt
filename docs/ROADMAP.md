# 🧭 HeaRTOS Roadmap

This roadmap outlines the evolution of HeaRTOS from its current v0.2.x stage to the planned 1.0.0 release.

> Note: may change during its course of development.

---

## ✅ Completed (v0.2.x)

- Core scheduler (static tasks)
- Null and POSIX ports (simulation / verification tester application)
- Binary semaphores (ISR-safe give)
- Version + port metadata via CMake
- Example: `two_tasks` and `sem_basic`

---

## 🔧 v0.3.0 — *Cortex‑M Foundation*
- Cortex‑M port: context switching, SysTick, PendSV
- STM32H7 compilation target
- Tick + timeslice enforcement
- Port abstraction cleanup (`hrt_port_yield_to_scheduler`)
- Example: Blinky / UART echo demo

---

## ⚙️ v0.4.0 — *Synchronization & Mutexes*
- Counting semaphores
- Mutex wrapper with basic priority inheritance
- Immediate handoff optimization on semaphore give
- Expanded unit tests (POSIX)

---

## 📬 v0.5.0 — *Queues & Events*
- Message queues (SPSC → MPMC)
- Event flags (bitmask groups)
- Task notification API
- Timeout variants of IPC (`hrt_sem_take_timeout`, etc.)

---

## 🕒 v0.6.0 — *Timing & Tickless Idle*
- Tickless idle (auto sleep until next event)
- High‑resolution timers
- `hrt_delay_until()` API

---

## 🧩 v0.7.0 — *Dual‑Core & AMP Support*
- CM4↔CM7 communication primitives (AMP)
- Shared memory mailbox interface

---

## 🧪 v0.8.0 — *Testing & Determinism*
- POSIX simulation test harness
- Cortex‑M simulation validation
- Continuous integration with coverage

---

## 🧱 v0.9.0 — *Stabilization*
- Code cleanup, strict warnings
- Static analysis & MISRA review
- Docs freeze draft

---

## 🏁 v1.0.0 — *Production Release*
- Fully verified Cortex‑M port (STM32H7)
- Deterministic scheduler with preemption
- Complete IPC suite (semaphores, mutexes, queues, events)
- Tickless idle + timers
- Unit tests and examples
- Documentation freeze (API, Porting, Design)

---

## 🧭 Beyond 1.0
- Multi‑core load balancing (SMP experiment)
- Dynamic memory configuration (optional)
- Distributed scheduling primitives
- File‑backed POSIX simulation mode
