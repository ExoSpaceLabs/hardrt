# C++17 Wrapper (`hardrtpp.hpp`)

HardRT v0.4.0 provides an optional header-only C++17 wrapper in `cpp/hardrtpp.hpp`. The wrapper is a thin layer over the C API and does not add dynamic allocation, exceptions, or a separate scheduler model.

Enable it with:

```bash
cmake -S . -B build \
  -DHARDRT_PORT=posix \
  -DHARDRT_ENABLE_CPP=ON
```

Downstream CMake projects link `HardRT::hardrtpp` when the package was built and installed with the wrapper enabled.

## Available wrapper types

- `hardrt::System`
- `hardrt::Task`
- `hardrt::Semaphore`
- `hardrt::Mutex`
- `hardrt::QueueRef<T>`
- `hardrt::StaticQueue<T, Capacity>`

There is no `hardrt::Queue<T, Capacity>` alias in v0.4.0.

## System management

```cpp
#include <hardrtpp.hpp>

int main() {
    const hrt_config_t cfg{
        1000,
        HRT_SCHED_PRIORITY_RR,
        5,
        0,
        HRT_TICK_SYSTICK
    };

    const int rc = hardrt::System::init(cfg);
    if (rc != 0) {
        return rc;
    }

    const auto ticks = hardrt::System::tick_now();
    const auto now_ms = hardrt::System::now_ms();
    const char *version = hardrt::System::version_string();
    const auto packed_version = hardrt::System::version_u32();
    const char *port = hardrt::System::port_name();
    const int port_id = hardrt::System::port_id();

    (void)ticks;
    (void)now_ms;
    (void)version;
    (void)packed_version;
    (void)port;
    (void)port_id;

    hardrt::System::start();
}
```

The current wrapper method is `version_string()`, not `version()`.

`System::init()` forwards directly to `hrt_init()`. In the current C implementation, initialization returns `0` and does not validate repeated initialization or invalid lifecycle ordering.

## Task management

### Wrapper-owned static stack

```cpp
#include <hardrtpp.hpp>

void worker(void *arg) {
    (void)arg;
    for (;;) {
        hardrt::Task::sleep(100);
    }
}

int create_tasks() {
    const int first = hardrt::Task::create<1024, 0>(
        worker, nullptr, HRT_PRIO1, 5);
    const int second = hardrt::Task::create<2048, 1>(
        worker, nullptr, HRT_PRIO0, 0);

    return first < 0 ? first : second;
}
```

Each unique `<StackWords, Tag>` template combination owns one function-local static stack array.

The return value is the non-negative task ID returned by `hrt_create_task()`, or a negative failure result. It is not merely `0` on success.

The `slice` argument is passed through an explicit `hrt_task_attr_t`:

- `slice > 0` configures that many ticks per slice;
- `slice == 0` creates a cooperative task.

It does not request the system default slice. To use the C default-attribute path, call `hrt_create_task()` with `attr == nullptr` directly.

### Application-owned stack

```cpp
alignas(8) static uint32_t worker_stack[512];

const int id = hardrt::Task::create_with_stack(
    worker,
    nullptr,
    worker_stack,
    512,
    HRT_PRIO1,
    5);
```

### Task control

```cpp
hardrt::Task::sleep(500);
hardrt::Task::yield();
hardrt::Task::delete_current();
```

In v0.4.0, `Task::sleep(0)` sleeps for one tick because it forwards to `hrt_sleep(0)`. Use `Task::yield()` for an immediate voluntary scheduling point.

A task that returns from its entry function is deleted by the port trampoline.

## Semaphores

```cpp
hardrt::Semaphore ready(1);      // binary, initially available
hardrt::Semaphore slots(0, 5);   // counting, range 0..5

void consumer(void *arg) {
    (void)arg;
    ready.take();
    ready.give();
}
```

Available operations:

```cpp
sem.take();
sem.try_take();
sem.give();
sem.give_from_isr(need_switch);
```

`give_from_isr()` forwards to the C API. In v0.4.0, `need_switch` becomes `1` whenever a waiter is awakened, regardless of the waiter's priority relative to the interrupted task.

## Mutexes

```cpp
hardrt::Mutex lock;

void worker_with_lock(void *arg) {
    (void)arg;
    for (;;) {
        lock.lock();
        /* Critical section. */
        lock.unlock();
        hardrt::Task::sleep(10);
    }
}
```

The wrapper exposes `lock()`, `try_lock()`, and `unlock()`. The underlying mutex is non-recursive, owner-tracked, task-context-only, and has no timed lock or priority inheritance.

## Queues with external storage

```cpp
#include <array>
#include <cstddef>

alignas(int) std::array<std::byte, 8 * sizeof(int)> storage{};
hardrt::QueueRef<int> queue;

void init_queue() {
    queue.init(storage.data(), 8);
}
```

`QueueRef<T>` stores the `hrt_queue_t` object but uses caller-provided item storage.

## Queues with wrapper-owned storage

```cpp
hardrt::StaticQueue<int, 8> queue;

void producer(void *arg) {
    (void)arg;
    const int value = 42;
    queue.send(value);
}

void consumer(void *arg) {
    (void)arg;
    int value{};
    queue.recv(value);
}
```

Both queue wrappers expose:

- `send` and `recv`;
- `try_send` and `try_recv`;
- `try_send_from_isr` and `try_recv_from_isr`;
- `native_handle`.

The C queue implementation copies objects as raw bytes with `memcpy`. Use queue element types that are safe to copy byte-for-byte, normally trivially copyable types. The v0.4.0 wrapper does not enforce that requirement with a `static_assert`.

## Allocation and ownership

- No wrapper performs heap allocation.
- Task and queue storage remains static or caller-owned.
- Wrapper objects must outlive every task that accesses their underlying C object.
- Pointer-valued queue elements do not transfer ownership; the application remains responsible for the pointed-to storage.