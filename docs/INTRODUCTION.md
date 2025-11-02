# 🫀 Introduction to HeaRTOS

HeaRTOS — the **heartbeat of small embedded systems** — is a minimal, deterministic real‑time operating system
written in C, designed for clarity, portability, and educational transparency.

---

## 🔍 What Is an RTOS?

A *Real‑Time Operating System (RTOS)* manages multiple concurrent tasks while guaranteeing predictable timing.
It provides:

- **Scheduling**: deciding which task runs next.
- **Synchronization**: semaphores, mutexes, queues for communication.
- **Timing control**: deterministic delays and periodic execution.

Unlike full OSes (Linux, Windows), an RTOS runs **without dynamic allocation** and often without file systems,
focusing purely on *time‑critical task execution*.

---

## ❤️ Why HeaRTOS?

HeaRTOS was built to be *small, understandable, and verifiable*.
Where many RTOS kernels grow opaque, HeaRTOS keeps every subsystem visible in just a few C files.

| Feature           | Philosophy                                                    |
|-------------------|---------------------------------------------------------------|
| **Minimal Core**  | Just tasks, scheduler, and synchronization — no heap, no HAL. |
| **Portable**      | Ports for POSIX and Cortex‑M are separate from kernel logic.  |
| **Deterministic** | No unbounded loops or dynamic allocation in the kernel.       |
| **Transparent**   | All state visible; no black‑box APIs.                         |
| **Educational**   | Ideal for learning or building your own derived RTOS.         |

---

## ⚙️ Typical Use Cases

- Bare‑metal STM32 (Cortex‑M) firmware
- Real‑time subsystems in hybrid Linux + MCU designs
- Spacecraft housekeeping and payload task managers
- Embedded vision and control loops on constrained devices

---

## 📚 Further Reading

- [Build Guide](BUILD.md)
- [C API Reference](API_C.md)
- [Porting Guide](PORTING.md)
- [Roadmap](ROADMAP.md)
