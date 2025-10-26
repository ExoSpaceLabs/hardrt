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

Expected output:
```
HeaRTOS example compiled. With null port there is no live scheduling yet.
```

> 💡 The `null` port is a stub — it doesn’t simulate time or context switches.  
> Add the `posix` or `cortex_m` port later to see actual scheduling.

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

## 🧠 API Overview (C)

```c

/* Version */
const char* hrt_version_string(void);
unsigned    hrt_version_u32(void);

/* Core */
int  hrt_init(const hrt_config_t* cfg);
int  hrt_create_task(hrt_task_fn fn, void* arg,
                     uint32_t* stack_words, size_t n_words,
                     const hrt_task_attr_t* attr);
void hrt_start(void);

/* Control */
void     hrt_sleep(uint32_t ms);
void     hrt_yield(void);
uint32_t hrt_tick_now(void);

/* Runtime tuning */
void hrt_set_policy(hrt_policy_t p);
void hrt_set_default_timeslice(uint16_t t);
```

Scheduler policies:
```c

typedef enum {
    HRT_SCHED_PRIORITY,     // strict priority
    HRT_SCHED_RR,           // equal-time round robin
    HRT_SCHED_PRIORITY_RR   // priority + RR within same class
} hrt_policy_t;
```

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

```c

#include "heartos.h"

static uint32_t stackA[256];
static uint32_t stackB[256];

void TaskA(void* arg) {
    for (;;) { hrt_sleep(500); }
}

void TaskB(void* arg) {
    for (;;) { hrt_sleep(1000); }
}

int main(void) {
    hrt_config_t cfg = { .tick_hz=1000, .policy=HRT_SCHED_PRIORITY_RR, .default_slice=5 };
    hrt_init(&cfg);

    hrt_task_attr_t fast = { .priority=HRT_PRIO0, .timeslice=0 };
    hrt_task_attr_t slow = { .priority=HRT_PRIO1, .timeslice=5 };

    hrt_create_task(TaskA, NULL, stackA, 256, &fast);
    hrt_create_task(TaskB, NULL, stackB, 256, &slow);

    hrt_start(); // jump into scheduler (no effect with null port)
}
```

---

## 🧱 Porting

All hardware-specific logic lives in `/src/port/`.

Each port must implement:
```c

void hrt_port_start_systick(uint32_t hz);
void hrt_port_idle_wait(void);
void hrt__pend_context_switch(void);
void hrt_port_prepare_task_stack(int id, void (*tramp)(void),
                                 uint32_t* stack_base, size_t words);
```

Additionally, the port must call the kernel tick hook on every system tick (ISR or timer thread):
```c

void hrt__tick_isr(void); // defined by the core (see inc/heartos_time.h)
```

Current ports:
- `port/null/port_null.c` → bare stubs (used for build-time sanity; no scheduling)
- `port/posix/` → Linux desktop simulation (planned)
- `port/cortex_m/` → ARM Cortex-M (planned)

Note: as of v0.1.0 only the `null` port is available; selecting other ports will fail at configure time.

---

## 📦 Module status

- `inc/heartos_sem.h` — placeholder for binary semaphores (API not implemented yet)
- `inc/heartos_queue.h` — placeholder for simple SPSC queues (API not implemented yet)
- `inc/heartos_time.h` — exposes `hrt__tick_isr()` for ports to call on every tick
- `inc/heartos_version.h` — generated by CMake into `build/generated/heartos_version.h` and installed alongside public headers

Current version: `0.1.0` (see `hrt_version_string()` and `hrt_version_u32()`).

---

## 🚀 Roadmap

- [ ] POSIX port for Linux simulation
- [ ] Cortex-M port (STM32H7 target)
- [ ] Binary semaphores and queues
- [ ] Dual-core support (AMP message passing)
- [ ] Unit test harness
- [ ] Docs and diagrams

---

## 📜 License

Apache License 2.0 © 2025 — you may use, modify, and distribute HeaRTOS freely,  
provided you include this notice and comply with the terms of the Apache 2.0 license.

See [LICENSE](LICENSE) for full text.

---

**HeaRTOS** — small core, steady beat.
