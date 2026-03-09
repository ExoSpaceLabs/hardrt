# C++ Wrapper (`hardrtpp.hpp`)

HardRT provides an optional header-only C++17 wrapper in `cpp/hardrtpp.hpp`.
It exposes thin, inline wrappers around the C API.

## Overview

The wrapper currently provides:
- `hardrt::System` for kernel init/start and version metadata
- `hardrt::Task` for task creation and control
- `hardrt::Semaphore` for binary and counting semaphores
- `hardrt::Queue<T, Capacity>` for typed fixed-capacity queues
- `hardrt::Mutex` for owner-tracked mutual exclusion

## System Management

```cpp
#include <hardrtpp.hpp>

hrt_config_t cfg{
    .tick_hz = 1000,
    .policy = HRT_SCHED_PRIORITY_RR,
    .default_slice = 5,
    .core_hz = 0,
    .tick_src = HRT_TICK_SYSTICK,
};

hardrt::System::init(cfg);
hardrt::System::start();
```

## Task Management

### Automatic stack management

The `Task::create<Size, Tag>` helper allocates a unique static stack per `<Size, Tag>` combination.

```cpp
#include <hardrtpp.hpp>

void worker(void* arg) {
    (void)arg;
    for (;;) {
        hardrt::Task::sleep(100);
    }
}

int main() {
    hardrt::Task::create<1024, 0>(worker, nullptr, HRT_PRIO1);
    hardrt::Task::create<2048, 1>(worker, nullptr, HRT_PRIO0);
}
```

### Manual stack management

```cpp
static uint32_t my_stack[512];
hardrt::Task::create_with_stack(my_task, nullptr, my_stack, 512, HRT_PRIO1);
```

### Task control

```cpp
hardrt::Task::sleep(500);
hardrt::Task::yield();
```

## Semaphores

The `hardrt::Semaphore` wrapper maps directly to `hrt_sem_t`.

```cpp
hardrt::Semaphore sem(1);      // binary semaphore (available)
hardrt::Semaphore slots(0, 5); // counting semaphore: 0..5 tokens

void worker(void*) {
    sem.take();
    // event/resource synchronization
    sem.give();
}
```

Notes:
- `give_from_isr()` is available on the wrapper.
- For critical sections and ownership enforcement, use `hardrt::Mutex`.

## Mutexes

The `hardrt::Mutex` wrapper maps directly to `hrt_mutex_t`.

```cpp
hardrt::Mutex lock;

void worker(void*) {
    for (;;) {
        lock.lock();
        // critical section
        lock.unlock();
        hardrt::Task::sleep(10);
    }
}
```

Semantics:
- non-recursive
- owner-tracked
- task context only
- no ISR API
- no timed lock or priority inheritance in the current implementation

## Queues

The `Queue<T, Capacity>` wrapper provides a typed front-end over `hrt_queue_t`.

```cpp
hardrt::Queue<int, 8> q;

void producer(void*) {
    int value = 42;
    q.send(value);
}

void consumer(void*) {
    int out{};
    q.recv(out);
}
```

## Features

- **Zero-overhead shape**: wrappers are inline and call into the C API directly.
- **Typed convenience**: queue wrapper removes some boilerplate.
- **No hidden allocation**: still fully static. The cult of heap abuse can stay elsewhere.
