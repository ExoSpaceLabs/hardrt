# C++17 wrappers

HardRT provides optional header-only C++17 wrappers. The core wrapper is in `cpp/hardrtpp.hpp`; v0.5 event flags and task notifications are provided by the companion `cpp/hardrt_signals.hpp` header. The wrappers are thin layers over the C API and do not add dynamic allocation, exceptions, or a separate scheduler model.

Enable them with:

```bash
cmake -S . -B build \
  -DHARDRT_PORT=posix \
  -DHARDRT_ENABLE_CPP=ON
```

Downstream CMake projects link `HardRT::hardrtpp` when the package was built and installed with the wrapper enabled.

## Available wrapper types

From `<hardrtpp.hpp>`:

- `hardrt::System`
- `hardrt::Task`
- `hardrt::Semaphore`
- `hardrt::Mutex`
- `hardrt::QueueRef<T>`
- `hardrt::StaticQueue<T, Capacity>`
- `hardrt::Queue<T, Capacity>` as a convenience alias for `StaticQueue<T, Capacity>`

From `<hardrt_signals.hpp>`:

- `hardrt::EventFlags`
- `hardrt::NotifyAction`
- `hardrt::TaskNotification`

The signal header is intentionally separate rather than being silently pulled into `hardrtpp.hpp`; applications include only the wrapper surface they use.

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
    const char *version_alias = hardrt::System::version();
    const auto packed_version = hardrt::System::version_u32();
    const char *port = hardrt::System::port_name();
    const int port_id = hardrt::System::port_id();

    (void)ticks;
    (void)now_ms;
    (void)version;
    (void)version_alias;
    (void)packed_version;
    (void)port;
    (void)port_id;

    hardrt::System::start();
}
```

`System::version_string()` is the canonical explicit name. `System::version()` is a forwarding convenience alias and returns the same string.

`System::init()` forwards directly to `hrt_init()` and therefore uses the same lifecycle and configuration validation as the C API.

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

Each unique `<StackWords, Tag>` template combination owns one function-local static stack array. Repeating the same specialization therefore refers to the same stack storage.

The v0.5 kernel enforces stack ownership at the common C task-creation boundary. A second live task cannot reuse or partially overlap another live task's stack, including a wrapper-owned static stack from the same `Task::create<StackWords, Tag>` specialization. Such creation fails rather than allowing two contexts to corrupt one stack. Once the previous task has entered `EXITED`, that stack is safe to reuse and its occupied task slot may be reclaimed.

The return value is the non-negative task ID returned by `hrt_create_task()`, or a negative failure result.

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

The same non-overlap rule applies to application-owned stacks.

### Task control

```cpp
hardrt::Task::sleep(500);
hardrt::Task::yield();
hardrt::Task::delete_current();
```

`Task::sleep(0)` forwards to the v0.5 C contract: it yields immediately without entering the sleep queue or waiting for a tick. Positive sub-tick values still round up to one tick.

A dispatched task is internally `RUNNING`. A voluntary scheduling point returns it to `READY` as appropriate. A task that returns from its entry function, or calls `Task::delete_current()`, enters `EXITED`; its TCB slot remains occupied until later reclamation.

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

`give_from_isr()` forwards to the C API and exposes the scheduler-aware wake/preemption decision through `need_switch`.

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

The wrapper exposes `lock()`, `try_lock()`, and `unlock()`. The underlying mutex is non-recursive, owner-tracked, task-context-only, and has no timed lock, priority inheritance, or automatic owner-death recovery. A task must release its owned mutexes before returning or deleting itself.

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

The explicit canonical type remains `StaticQueue<T, Capacity>`:

```cpp
hardrt::StaticQueue<int, 8> queue;
```

The shorter `Queue<T, Capacity>` name is exactly the same type:

```cpp
hardrt::Queue<int, 8> queue_alias;
```

Both spellings own the fixed-capacity storage inside the wrapper object.

```cpp
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

All queue wrappers expose:

- `send` and `recv`;
- `try_send` and `try_recv`;
- `try_send_from_isr` and `try_recv_from_isr`;
- `native_handle`.

The C queue implementation copies objects as raw bytes with `memcpy`. `QueueRef<T>`, `StaticQueue<T, Capacity>`, and therefore the `Queue<T, Capacity>` alias enforce `std::is_trivially_copyable<T>` at compile time. `StaticQueue` also rejects zero capacity and any capacity above the C API's `uint16_t` range. The CI compile-contract suite includes both accepted and expected-failure cases for these rules.

## Event flags

Include the companion signal wrapper:

```cpp
#include <hardrt_signals.hpp>

hardrt::EventFlags events;
```

`EventFlags` owns one statically stored `hrt_event_t`; construction calls `hrt_event_init()` and performs no allocation.

Available operations are:

```cpp
uint32_t matched = 0;
int need_switch = 0;

events.bits();
events.set(0x01u);
events.set_from_isr(0x02u, need_switch);
events.clear(0x01u);
events.clear_from_isr(0x02u);
events.wait_any(0x03u, matched, true);   // true = clear matched bits on exit
events.wait_all(0x03u, matched, false);  // retain bits
```

`wait_any()` and `wait_all()` preserve the C semantics documented in [EVENTS_NOTIFICATIONS.md](EVENTS_NOTIFICATIONS.md), including the common post-set snapshot used when one update satisfies multiple waiters.

`native_handle()` returns the underlying `hrt_event_t*` (or const pointer on a const wrapper) for integration with code that needs the C API directly.

## Task notifications

Task notifications do not own a separate object; the notification word lives in each application task's private TCB. The C++ interface is therefore a static helper:

```cpp
#include <hardrt_signals.hpp>

int need_switch = 0;
uint32_t value = 0;

hardrt::TaskNotification::notify(
    task_id, 0x10u, hardrt::NotifyAction::set_bits);

hardrt::TaskNotification::notify_from_isr(
    task_id, 0x20u, hardrt::NotifyAction::overwrite, need_switch);

hardrt::TaskNotification::wait(value, 0u, 0xFFu);
const uint32_t count = hardrt::TaskNotification::take(false);
```

`NotifyAction` maps directly to the four C producer actions:

- `NotifyAction::set_bits`
- `NotifyAction::overwrite`
- `NotifyAction::no_overwrite`
- `NotifyAction::increment`

`TaskNotification::notify()` defaults to `overwrite`. `notify_from_isr()` requires an explicit action and reports the scheduler decision through `need_switch`. `wait()` accepts clear-on-entry and clear-on-exit masks. `take(false)` decrements a counting notification by one; `take(true)` clears the count.

The wrapper intentionally does not add timeout behavior, heap state, or hidden synchronization. See [EVENTS_NOTIFICATIONS.md](EVENTS_NOTIFICATIONS.md) for the complete contract.

## Allocation and ownership

- No wrapper performs heap allocation.
- Task, queue, and event storage remains static or caller-owned.
- Wrapper objects must outlive every task that accesses their underlying C object.
- Pointer-valued queue elements do not transfer ownership; the application remains responsible for the pointed-to storage.
- Task notifications are kernel-owned per-task state and require no application-side storage object.
