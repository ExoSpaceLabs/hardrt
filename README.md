# 🫀 HeaRTOS

**HeaRTOS** — the _heartbeat_ of small embedded systems.  
A tiny, portable, modular real-time operating system written in C, with optional C++17 wrappers.  
Minimal footprint, predictable behavior, and zero hardware dependencies in its core.

---
> A compact cooperative/preemptive kernel built for clarity first, performance second.

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![CMake](https://img.shields.io/badge/CMake-≥3.16-orange)](#)
[![Language](https://img.shields.io/badge/lang-C/C++17-lightgrey)](#)

## 🤔 What is an RTOS? Why HeaRTOS?

An RTOS (Real-Time Operating System) is a tiny scheduler and time base that lets you run multiple independent tasks while meeting timing constraints. Unlike a general-purpose OS, an RTOS prioritizes predictability over throughput: tasks run in well-defined order based on priority and time-slicing, with millisecond (or sub-millisecond) timing derived from a periodic system tick.

When you need one
- You have concurrent activities: sampling sensors, talking to peripherals, blinking status LEDs, serving a control loop.
- Some activities must react within bounded time (tens of microseconds to a few milliseconds).
- You want simple, composable tasks instead of a single giant loop with complex state.

How HeaRTOS is different
- Minimal core by design: static tasks, static stacks, no heap, no drivers — just scheduling, time, and a few knobs.
- Deterministic behavior: fixed-priority and round-robin policies with clear rules; FIFO within a class.
- Portable without baggage: the core is architecture-agnostic; small “ports” adapt ticks and context switch mechanics.
- Transparent and teachable: compact code base intended to be read, understood, and verified with a host-side test suite.

What HeaRTOS is not
- Not a monolithic platform: it ships no HAL, filesystem, networking, or drivers.
- Not preempt-everywhere: preemption depends on the selected port/policy; cooperative within a class remains a first-class mode.
- Not a kitchen sink kernel: queues/semaphores/mailboxes are added only when they help the core remain minimal and predictable.

At a glance vs typical RTOSes
- Many RTOSes: rich feature sets, multiple allocators, dynamic objects, integrated middleware.
- HeaRTOS: keep the heartbeat small — predictable scheduler, static tasks, portable ports, and tests. Pair it with exactly the libraries your product needs.

Architecture (conceptual)

![arch](docs/images/Architecture.png)

Scheduling flow (priority + RR)
```mermaid
flowchart LR
  subgraph "Tick ISR: hrt__tick_isr()"
    T1[inc tick] --> T2{Any sleepers due?}
    T2 -- yes --> T3[wake: move to READY]
    T2 -- no --> T4[ ]
    T3 --> T5{RR policy active and running?}
    T4 --> T5
    T5 -- yes --> T6[decrement running slice]
    T6 --> T7{slice==0?}
    T7 -- yes --> T8[pend reschedule]
    T7 -- no --> T9[no-op]
    T8 --> T10["pend reschedule (also for wakes)"]
    T9 --> T10
  end

  subgraph "Scheduler loop (safe context)"
    S1{slice expired?}
    S1 -- yes --> S2[move running to tail of its ready queue\n                     + refresh slice]
    S1 -- no  --> S3["pick highest-prio READY (FIFO in class)"]
    S2 --> S3 --> S4[context switch]
  end

  T10 -->|pend| S1
```
NOTE: Will replace with Drawio

Time & sleep flow
```mermaid
sequenceDiagram
  autonumber
  participant App as App Task
  participant Core as HeaRTOS Core
  participant Port as Port (Tick)

  App->>Core: hrt_sleep(ms)
  Core-->>App: state = SLEEP, remove from READY
  Port-->>Core: periodic hrt__tick_isr()
  Core-->>Core: advance tick, if wake_tick lte now then make READY
  Core-->>App: READY re-enters priority queue (FIFO within prio)
  Core-->>App: scheduled when selected by scheduler
```

### Priority queues and within-class round-robin
```mermaid
flowchart TB
  subgraph "Priority 0 (highest)"
    direction LR
    Q0T1[Task P0-A] --> Q0T2[Task P0-B] --> Q0T3[Task P0-C]
  end

  subgraph "Priority 1"
    direction LR
    Q1T1[Task P1-A] --> Q1T2[Task P1-B]
  end

  subgraph "Priority 2"
    direction LR
    Q2T1[Task P2-A]
  end

  CPU[[CPU]] -->|pick next READY| Q0T1
  Q0T1 -->|FIFO within same priority| CPU
```
NOTE: Will replace with Drawio

### Round‑robin (sequence with tick handoffs)
```mermaid
sequenceDiagram
  autonumber
  participant T1 as Task T1
  participant T2 as Task T2
  participant T3 as Task T3


  T1->>T1: T0 running
  T1->>T1: T1 running
  T1-->>T2: handoff at T2 (slice expired)

  T2->>T2: T3 running
  T2-->>T3: handoff at T4 (slice expired)

  T3->>T3: T5 running
  T3-->>T1: handoff at T6 (slice expired)

  T1->>T1: T7 running
  T1-->>T2: handoff at T8 (slice expired)

  T2->>T2: T9 running
  T2-->>T3: handoff at T10 (slice expired)

  T3->>T3: T11 running
```
NOTE: Will replace with Drawio

### Priority preemption (sequence with tick handoffs)
```mermaid
sequenceDiagram
  autonumber
  participant P1 as Task P1 (lower)
  participant P0 as Task P0 (higher)


  P1->>P1: T0 running
  P1->>P1: T1 running
  P1->>P1: T2 running
  P1->>P1: T3 running
  P1->>P1: T4 running
  P1->>P1: T5 running

  P1-->>P0: preempt at T6 (higher priority READY)

  P0->>P0: T7 running
  P0->>P0: T8 running
  P0-->>P1: yield at T10 (resume lower)

  P1->>P1: T11 running
  P1->>P1: T12 running
  P1->>P1: T13 running
  P1->>P1: T14 running
  P1->>P1: T15 running
  P1->>P1: T16 running
```
NOTE: Will replace with Drawio

Explanation:
- In RR, each task keeps the CPU for its slice, then a handoff occurs at the next tick boundary.
- In priority preemption, the higher-priority task takes over immediately at its arrival tick; the lower resumes when the higher finishes.
- Self-transitions indicate ticks where the same task continues to run (no interrupt/context switch).

## ✨ Features

- **Pure C Core** — no dynamic allocation, no HAL dependencies.
- **Portable** — architecture-agnostic; ports live in `/src/port/`.
- **Deterministic Scheduler** — priority-based, round-robin, or hybrid modes.
- **Static Tasks** — no heap or fancy containers; all memory defined by you.
- **C++ Wrappers (optional)** — enable via CMake if you like your RTOS with templates.
- **CMake Package** — installable and discoverable via `find_package(HeaRTOS)`.
- **Version API** — runtime `hrt_version_string()`/`hrt_version_u32()` and generated header.
- **Apache 2.0 Licensed** — permissive; free to use, modify, and ship.

---

## 📁 Repository Structure

```
heartos/
├── CMakeLists.txt          # build configuration
├── inc/                    # public headers
├── src/                    # core + port implementations
│   ├── core/               # kernel internals
│   └── port/               # architecture-specific backends
├── cpp/                    # optional C++17 interface
├── examples/               # simple demos (two_tasks first)
├── LICENSE
└── README.md
```

---

## 🚀 Quick Start

```bash
# clone and configure (POSIX port)
git clone https://github.com/<your-username>/heartos.git
cd heartos && mkdir build && cd build
cmake -DHEARTOS_PORT=posix -DHEARTOS_BUILD_EXAMPLES=ON ..
cmake --build . --target two_tasks -j
./examples/two_tasks/two_tasks
```

- For full build/install options and using `find_package(HeaRTOS)`, see docs/BUILD.md.
- For the POSIX test suite, see docs/TESTS_POSIX.md.

### Build-time configuration (CMake)
You can tune kernel sizing at configure time without editing sources. These map to preprocessor macros used by the public headers and all targets:

- `HEARTOS_CFG_MAX_TASKS` → `HEARTOS_MAX_TASKS` (default 8)
- `HEARTOS_CFG_MAX_PRIO` → `HEARTOS_MAX_PRIO` (default 4)

Examples:
```bash
# POSIX port, 12 priority levels and 12 tasks, build and run tests
cmake -DHEARTOS_PORT=posix -DHEARTOS_BUILD_TESTS=ON \
      -DHEARTOS_CFG_MAX_PRIO=12 -DHEARTOS_CFG_MAX_TASKS=12 \
      ..
cmake --build . --target heartos_tests -j && ./heartos_tests

# Two tasks example with 8 tasks max and 4 priorities (defaults)
cmake -DHEARTOS_PORT=posix -DHEARTOS_BUILD_EXAMPLES=ON ..
cmake --build . --target two_tasks -j && ./examples/two_tasks/two_tasks
```

Constraints and notes:
- Priorities have a physical cap of 12 levels in this release (`HRT_PRIO0..HRT_PRIO11`).
- There is no hard cap on the number of tasks in the source; it’s limited by memory and your configuration.
- CMake validates at configure time: `HEARTOS_CFG_MAX_PRIO` must be 1..12 and `HEARTOS_CFG_MAX_TASKS >= HEARTOS_CFG_MAX_PRIO`.
- Practical tip: set `HEARTOS_CFG_MAX_TASKS` >= `HEARTOS_CFG_MAX_PRIO` (often equal) so each priority can have at least one runnable task.

For the exhaustive list of options, see docs/BUILD.md.

---

## 🧪 Tests (POSIX)

Deterministic host-side suite in a single executable `heartos_tests` validating identity, sleep/wake timing, RR/priority semantics, runtime tuning, FIFO order, tick‑rate independence, wraparound, and more.

See docs/TESTS_POSIX.md for setup and full coverage.

## 🧠 API docs

- C API overview: docs/API_C.md
- Doxygen/comment style and generating HTML docs: docs/DOCUMENTATION.md

---

### C++ wrapper (optional)
See docs/CPP.md for enabling the C++ target and a short usage example.

---

## 🧪 Example

See docs/EXAMPLES_C.md for a walkthrough of the bundled C example, build/run instructions for the `null` and `posix` ports, and notes on observing round-robin behavior.

---

## 🔒 Semaphores

HeaRTOS includes a minimal binary semaphore for synchronization and simple mutual exclusion. It is binary (0/1), wakes waiters in FIFO order, and preempts correctly when a higher‑priority task is woken. See the full guide, example, and tests:

- Documentation: docs/SEMAPHORES.md
- Example target: `sem_basic` (source at examples/sem_basic)
- Tests: part of `heartos_tests` (see tests/test_semaphore.c)

---

## 🧱 Porting

Ports live under `/src/port/` and integrate tick, context setup/switch, and the core scheduler.
See docs/PORTING.md for the full contract, required hooks, and integration rules.

---

## 📦 Module status

See docs/MODULE_STATUS.md for current components and generated headers.

---

## 🚀 Roadmap

See docs/ROADMAP.md for planned features and progress.

---

## 📜 License

Apache License 2.0 © 2025 — you may use, modify, and distribute HeaRTOS freely,  
provided you include this notice and comply with the terms of the Apache 2.0 license.

See [LICENSE](LICENSE) for full text.

---

**HeaRTOS** — small core, steady beat.
