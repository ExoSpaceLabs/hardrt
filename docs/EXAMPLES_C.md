
## 🧪 Examples C


*** describe the example here ***

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

*** add other examples here ***