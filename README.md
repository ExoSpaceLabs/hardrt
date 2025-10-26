# 🫀 HeaRTOS

**HeaRTOS** — the _heartbeat_ of small embedded systems.  
A tiny, portable, modular real-time operating system written in C, with optional C++17 wrappers.  
Minimal footprint, predictable behavior, and zero hardware dependencies in its core.

---
> A compact cooperative/preemptive kernel built for clarity first, performance second.

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![CMake](https://img.shields.io/badge/CMake-≥3.16-orange)](#)
[![Language](https://img.shields.io/badge/lang-C/C++17-lightgrey)](#)

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

## ⚙️ Build Instructions

### Prerequisites
- A C compiler (GCC/Clang)
- [CMake ≥ 3.16](https://cmake.org/)
- (Optional) CLion or any IDE that supports CMake

### Build from terminal
```bash

git clone https://github.com/<your-username>/heartos.git
cd heartos
mkdir build && cd build
cmake -DHEARTOS_PORT=null -DHEARTOS_BUILD_EXAMPLES=ON ..
cmake --build . -j$(nproc)
```

This will:
- build the static library `libheartos.a`
- compile the example program `examples/two_tasks/two_tasks`

Run it:
```bash

./examples/two_tasks/two_tasks
```

Expected output (null port):
```
HeaRTOS version: 0.2.0 (0x000200), port: null (id=0)
```

> 💡 The `null` port is a stub — it doesn’t start a tick or switch contexts. `hrt_start()` returns immediately, so tasks do not run here. Use the `posix` port to see live scheduling on a desktop.

### Run with POSIX port (desktop)
Build the library and example with the POSIX port to see live scheduling:
```bash
# from a build directory
cmake -DHEARTOS_PORT=posix -DHEARTOS_BUILD_EXAMPLES=ON ..
cmake --build . --target two_tasks -j
./examples/two_tasks/two_tasks
```
Expected output (excerpt, continues indefinitely):
```
HeaRTOS version: 0.2.0 (0x000200), port: posix (id=1)
[A] tick count [0]
[B] tock -----
[A] tick count [1]
[A] tick count [2]
[B] tock -----
...
```

### Install (optional)
You can install the library and headers to a prefix to consume from other projects:
```bash
# locally
cmake --install . --prefix "$PWD/install"
# system wide
sudo cmake --install .
```
This installs:
- archive `libheartos.a`
- public headers from `inc/`
- generated header `heartos_version.h`
- CMake package files under `lib/cmake/HeaRTOS`

Consume from another CMake project:
```cmake

find_package(HeaRTOS REQUIRED)
add_executable(app main.c)
target_link_libraries(app PRIVATE HeaRTOS::heartos)
```

---

## 🧩 CMake Options

| Option                   | Default | Description                                       |
|:-------------------------|:--------|:--------------------------------------------------|
| `HEARTOS_PORT`           | `null`  | Select build port: `null`, `posix`, or `cortex_m` |
| `HEARTOS_ENABLE_CPP`     | `OFF`   | Build C++17 header-only wrapper (`heartospp`)     |
| `HEARTOS_BUILD_EXAMPLES` | `ON`    | Build bundled demo projects                       |

Example:
```bash

cmake -DHEARTOS_PORT=posix -DHEARTOS_ENABLE_CPP=ON ..
```

---

## 🧪 Tests (POSIX)

HeaRTOS is validated on the POSIX host port with a deterministic, dependency‑free test suite that runs as a single executable (`heartos_tests`). It checks identity, sleep/wake timing, round‑robin and priority scheduling semantics, runtime tuning, FIFO ready‑queue order, tick‑rate independence, and more.

- The tests require `HEARTOS_TEST_HOOKS` and CMake enables it automatically for POSIX when `HEARTOS_BUILD_TESTS=ON`.
- How to build/run and full coverage are described here: [Posix tests](docs/TESTS_POSIX.md)

## 🧠 API Overview

See [C API](docs/API_C.md) for a detailed C API reference, including configuration, task creation, scheduling policies, sleep/yield, runtime tuning, and version/port identity.

---

### C++ wrapper (optional)
Enable with CMake option `-DHEARTOS_ENABLE_CPP=ON` and include the header-only wrapper `cpp/heartospp.hpp`:
```cpp

#include <heartospp.hpp>

void my_task(void*){}

int create_task_cpp(){
    static uint32_t stack[256];
    return heartos::Task::create(my_task, nullptr, stack, 256, HRT_PRIO1, /*slice*/5);
}
```
The C++ target is `HeaRTOS::heartospp` and links the C core automatically.

---

## 🧪 Example

See docs/EXAMPLES_C.md for a walkthrough of the bundled C example, build/run instructions for the `null` and `posix` ports, and notes on observing round-robin behavior.

---

## 🧱 Porting

All hardware-specific logic lives in `/src/port/`. A port provides tick, context setup/switching, and integrates with the core scheduler. See docs/PORTING.md for the full contract, required hooks, and important rules (no switching from ISR, mask ticks during swaps, and apply RR rotation at scheduler re-entry).

Current ports:
- `port/null/` → stub for build-time validation; no tick, no scheduling.
- `port/posix/` → Linux hosted simulation using `setitimer` + `SIGALRM` and `ucontext` (available).
- `port/cortex_m/` → planned.

---

## 📦 Module status

- `inc/heartos_sem.h` — placeholder for binary semaphores (API not implemented yet)
- `inc/heartos_queue.h` — placeholder for simple SPSC queues (API not implemented yet)
- `inc/heartos_time.h` — exposes `hrt__tick_isr()` for ports to call on every tick
- `inc/heartos_version.h` — generated by CMake into `build/generated/heartos_version.h` and installed alongside public headers

Current version: `0.2.0` (see `hrt_version_string()` and `hrt_version_u32()`).

---

## 🚀 Roadmap

- [x] POSIX port for Linux simulation
- [ ] Cortex-M port (STM32H7 target)
- [ ] Binary semaphores and queues
- [ ] Dual-core support (AMP message passing)
- [x] Unit test harness
- [ ] Docs and diagrams

---

## 📜 License

Apache License 2.0 © 2025 — you may use, modify, and distribute HeaRTOS freely,  
provided you include this notice and comply with the terms of the Apache 2.0 license.

See [LICENSE](LICENSE) for full text.

---

**HeaRTOS** — small core, steady beat.
