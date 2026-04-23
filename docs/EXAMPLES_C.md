## 🧪 Examples

This guide summarizes the bundled examples and includes small usage snippets for features that are implemented in the codebase but do not yet have a dedicated standalone example application.

## Bundled repository examples

### C examples
- `examples/two_tasks`
- `examples/two_tasks_external`
- `examples/sem_basic`
- `examples/mutex_basic`
- `examples/sem_counting`
- `examples/queue_posix`
- `examples/hardrt_h755_blinky`
- `examples/hardrt_h755_demo`
- `examples/hardrt_h755_dwt_timing`

### C++ examples
- `examples/two_tasks_cpp`
- `examples/sem_basic_cpp`
- `examples/mutex_basic_cpp`
- `examples/sem_counting_cpp`
- `examples/queue_posix_cpp`
- `examples/hardrt_h755_blinky_cpp`

> Dedicated `mutex` example applications in C and C++ are available in `examples/mutex_basic` and `examples/mutex_basic_cpp`. The API is also documented below with usage snippets.

---

## Build with the null port

```bash
cmake -DHARDRT_PORT=null -DHARDRT_BUILD_EXAMPLES=ON ..
cmake --build . --target two_tasks -j
./examples/two_tasks/two_tasks
```

Expected output (program exits immediately; no live scheduling):
```
HardRT version: 0.3.1 (0x000301), port: null (id=0)
```

Notes:
- The null port doesn’t start a tick or switch contexts; `hrt_start()` returns.

## Build with the POSIX port

```bash
cmake -DHARDRT_PORT=posix -DHARDRT_BUILD_EXAMPLES=ON ..
cmake --build . --target two_tasks -j
./examples/two_tasks/two_tasks
```

Expected output (excerpt; continues indefinitely):
```terminaloutput
HardRT version: 0.3.1 (0x000301), port: posix (id=1)
[A] tick count [0]
[B] tock -----
[A] tick count [1]
[A] tick count [2]
[B] tock -----
...
```

- Task A sleeps 500 ms; Task B sleeps 1000 ms. With `HRT_SCHED_PRIORITY_RR`, Task A has priority 0 and Task B priority 1, so Task A runs whenever READY.

## Seeing round-robin within a priority class

To observe time-slice rotation, create two tasks with the same priority and non-zero `timeslice`:

```c
#include "hardrt.h"
#include <stdio.h>

static uint32_t sa[2048], sb[2048];
static void t1(void*){ for(;;){ puts("T1"); hrt_sleep(10); } }
static void t2(void*){ for(;;){ puts("T2"); hrt_sleep(10); } }

int main(void){
    hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5,
        .core_hz = 0,
        .tick_src = HRT_TICK_SYSTICK
    };
    hrt_init(&cfg);
    hrt_task_attr_t a = { .priority = HRT_PRIO1, .timeslice = 5 };
    hrt_create_task(t1, 0, sa, 2048, &a);
    hrt_create_task(t2, 0, sb, 2048, &a);
    hrt_start();
}
```

## Mutex usage snippet in C

```c
#include "hardrt.h"

static hrt_mutex_t g_lock;
static uint32_t s1[1024], s2[1024];

static void producer(void*) {
    for (;;) {
        hrt_mutex_lock(&g_lock);
        /* critical section */
        hrt_mutex_unlock(&g_lock);
        hrt_sleep(10);
    }
}

static void consumer(void*) {
    for (;;) {
        if (hrt_mutex_try_lock(&g_lock) == 0) {
            /* short critical section */
            hrt_mutex_unlock(&g_lock);
        }
        hrt_sleep(5);
    }
}

int main(void) {
    hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5,
        .core_hz = 0,
        .tick_src = HRT_TICK_SYSTICK
    };

    hrt_init(&cfg);
    hrt_mutex_init(&g_lock);

    hrt_task_attr_t p = { .priority = HRT_PRIO1, .timeslice = 5 };
    hrt_create_task(producer, 0, s1, 1024, &p);
    hrt_create_task(consumer, 0, s2, 1024, &p);
    hrt_start();
}
```

Notes:
- mutexes are task-context only
- unlock must be performed by the owner
- mutexes are non-recursive

## Mutex usage snippet in C++

```cpp
#include <hardrtpp.hpp>

static hardrt::Mutex g_lock;

static void worker(void*) {
    for (;;) {
        g_lock.lock();
        // critical section
        g_lock.unlock();
        hardrt::Task::sleep(10);
    }
}
```

## External tick example (`two_tasks_external`)

This example is like `two_tasks` but HardRT time advances only when the application calls `hrt_tick_from_isr()`.

Build and run on POSIX:
```bash
cmake -DHARDRT_PORT=posix -DHARDRT_BUILD_EXAMPLES=ON ..
cmake --build . --target two_tasks_external -j
./examples/two_tasks_external/two_tasks_external
```

Expected behavior:
- Task A prints a counter roughly every 500 ms.
- Task B prints a heartbeat roughly every 1000 ms.
- The scheduler advances solely due to the external tick thread.

Notes:
- The example configures `hrt_config_t.tick_src = HRT_TICK_EXTERNAL`.
- See also: [TICK_SOURCE.md](TICK_SOURCE.md).
