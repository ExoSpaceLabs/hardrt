
## 🧪 Examples (C)

This guide shows how to build and run the bundled `two_tasks` example with both the `null` and `posix` ports, and how to observe round-robin behavior.

### Build with the null port

```bash
cmake -DHEARTOS_PORT=null -DHEARTOS_BUILD_EXAMPLES=ON ..
cmake --build . --target two_tasks -j
./examples/two_tasks/two_tasks
```
Expected output (program exits immediately; no live scheduling):
```
HeaRTOS version: 0.2.0 (0x000200), port: null (id=0)
```
Notes:
- The null port doesn’t start a tick or switch contexts; `hrt_start()` returns.

### Build with the POSIX port

```bash
cmake -DHEARTOS_PORT=posix -DHEARTOS_BUILD_EXAMPLES=ON ..
cmake --build . --target two_tasks -j
./examples/two_tasks/two_tasks
```
Expected output (excerpt; continues indefinitely):
```terminaloutput
HeaRTOS version: 0.2.0 (0x000200), port: posix (id=1)
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
#include "heartos.h"
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

## Visualizing scheduling

### Round‑robin within one priority (Sequence at tick times)
> Note: this diagram will be replaced with a draw io version.
```mermaid
sequenceDiagram
  autonumber
  participant T1 as Task T1
  participant T2 as Task T2


  T1->>T1: T0 running
  T1->>T1: T1 running
  T1->>T1: T2 running
  T1->>T1: T3 running
  T1->>T1: T4 running
  T1-->>T2: handoff at T5 (slice expired)

  T2->>T2: T6 running
  T2->>T2: T7 running
  T2->>T2: T8 running
  T2->>T2: T9 running
  T2-->>T1: handoff at T10 (slice expired)

  T1->>T1: T11 running
  T1->>T1: T12 running
  T1->>T1: T13 running
  T1->>T1: T14 running
  T1-->>T2: handoff at T15 (slice expired)

  T2->>T2: T16 running
  T2->>T2: T17 running
  T2->>T2: T18 running
  T2->>T2: T19 running
```
Caption:
- Policy: `HRT_SCHED_PRIORITY_RR` (RR applies within same priority).
- Two READY tasks with `timeslice=5 ms`. Handoffs occur exactly at T5, T10, T15, ...
- Self-transitions mark per-tick continuity when no interrupt/context switch occurs.

### Priority preemption (Sequence at tick times)
> Note: this diagram will be replaced with a draw io version.
```mermaid
sequenceDiagram
  autonumber
  participant B as Task B (lower)
  participant A as Task A (higher)


  B->>B: T0 running
  B->>B: T1 running
  B->>B: T2 running
  B->>B: T3 running
  B->>B: T4 running
  B->>B: T5 running
  B->>B: T6 running
  B->>B: T7 running
  B->>B: T8 running
  B->>B: T9 running
  B->>B: T10 running
  B->>B: T11 running

  B-->>A: preempt at T12 (higher priority READY)

  A->>A: T13 running
  A->>A: T14 running
  A->>A: T15 running
  A->>A: T16 running
  A-->>B: yield at T18 (resume lower)

  B->>B: T19 running
  B->>B: T20 running
  B->>B: T21 running
  B->>B: T22 running
  B->>B: T23 running
  B->>B: T24 running
  B->>B: T25 running
  B->>B: T26 running
  B->>B: T27 running
  B->>B: T28 running
  B->>B: T29 running

  B-->>A: preempt at T30 (higher priority READY)

  A->>A: T31 running
  A->>A: T32 running
  A->>A: T33 running
  A->>A: T34 running
  A-->>B: yield at T36 (resume lower)

  B->>B: T37 running
  B->>B: T38 running
  B->>B: T39 running
  B->>B: T40 running
  B->>B: T41 running
  B->>B: T42 running
```
Caption:
- Two preemption events are shown explicitly: at T12 and T30 when A arrives (higher priority preempts B).
- A yields/completes at T18 and T36, allowing B to resume.
- Self-transitions mark per-tick continuity when no interrupt/context switch occurs.

---

### External tick example (two_tasks_external)

This example is like `two_tasks` but HeaRTOS time advances only when the application calls `hrt_tick_from_isr()` — here driven by a small POSIX thread that sleeps one tick and calls the API every loop.

Build and run on POSIX:
```bash
cmake -DHEARTOS_PORT=posix -DHEARTOS_BUILD_EXAMPLES=ON ..
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
