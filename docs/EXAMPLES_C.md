
## 🧪 Examples (C)

This guide shows how to build and run the bundled `two_tasks` example with both the `null` and `posix` ports, and how to observe round-robin behavior.

### Build with the null port

```bash
cmake -DHARDRT_PORT=null -DHARDRT_BUILD_EXAMPLES=ON ..
cmake --build . --target two_tasks -j
./examples/two_tasks/two_tasks
```
Expected output (program exits immediately; no live scheduling):
```
HardRT version: 0.3.0 (0x000300), port: null (id=0)
```
Notes:
- The null port doesn’t start a tick or switch contexts; `hrt_start()` returns.

### Build with the POSIX port

```bash
cmake -DHARDRT_PORT=posix -DHARDRT_BUILD_EXAMPLES=ON ..
cmake --build . --target two_tasks -j
./examples/two_tasks/two_tasks
```
Expected output (excerpt; continues indefinitely):
```terminaloutput
HardRT version: 0.3.0 (0x000300), port: posix (id=1)
[A] tick count [0]
[B] tock -----
[A] tick count [1]
[A] tick count [2]
[B] tock -----
...
```
- Task A sleeps 500 ms; Task B sleeps 1000 ms. With `HRT_SCHED_PRIORITY_RR`, Task A has priority 0 (higher), Task B priority 1; thus Task A runs whenever READY.

### Seeing round-robin within a priority class

To observe time-slice rotation, create two tasks with the same priority and non-zero `timeslice`:

```c++
#include "hardrt.h"
#include <stdio.h>

static uint32_t sa[2048], sb[2048];
static void t1(void*){ for(;;){ puts("T1"); hrt_sleep(10); } }
static void t2(void*){ for(;;){ puts("T2"); hrt_sleep(10); } }

int main(void){
    hrt_config_t cfg = { .tick_hz=1000, .policy=HRT_SCHED_PRIORITY_RR, .default_slice=5 };
    hrt_init(&cfg);
    hrt_task_attr_t a = { .priority=HRT_PRIO1, .timeslice=5 };
    hrt_create_task(t1, 0, sa, 2048, &a);
    hrt_create_task(t2, 0, sb, 2048, &a);
    hrt_start();
}
```
You should see `T1` and `T2` alternate over time as their slices expire.

---

### External tick example (two_tasks_external)

This example is like `two_tasks` but HardRT time advances only when the application calls `hrt_tick_from_isr()` — here driven by a small POSIX thread that sleeps one tick and calls the API every loop.

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
- See also: [TICK_SOURCE.md](TICK_SOURCE.md) for a concise overview of external tick usage.
