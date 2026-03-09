## 🧠 API Overview (C)

This page summarizes the public C API available in HardRT v0.3.1. See `inc/hardrt.h` for authoritative declarations.

### Types

```c
/* Task function signature */
typedef void (*hrt_task_fn)(void*);

/* Task state (internal for reference) */
typedef enum { HRT_READY=0, HRT_SLEEP, HRT_BLOCKED, HRT_UNUSED } hrt_state_t;

/* Scheduler policies */
typedef enum {
    HRT_SCHED_PRIORITY,     /* strict priority, FIFO within class */
    HRT_SCHED_RR,           /* round-robin across all READY tasks */
    HRT_SCHED_PRIORITY_RR   /* priority + RR within the same class */
} hrt_policy_t;

/* Priority levels (0 is highest) */
typedef enum { HRT_PRIO0=0, HRT_PRIO1, /* ... */ HRT_PRIO11 } hrt_prio_t;

/* Init-time configuration */
typedef struct {
    uint32_t     tick_hz;
    hrt_policy_t policy;
    uint16_t     default_slice;
    uint32_t     core_hz;
    hrt_tick_source_t tick_src;
} hrt_config_t;

/* Per-task attributes passed at creation */
typedef struct {
    hrt_prio_t priority;
    uint16_t   timeslice;
} hrt_task_attr_t;
```

### Version and port identity

```c
const char* hrt_version_string(void);  /* e.g. "0.3.1" */
unsigned    hrt_version_u32(void);     /* (maj<<16)|(min<<8)|patch */
const char* hrt_port_name(void);       /* e.g. "posix" or "null" */
int         hrt_port_id(void);         /* 0=null, 1=posix, ... */
```

### Core lifecycle

```c
int  hrt_init(const hrt_config_t* cfg);
int  hrt_create_task(hrt_task_fn fn, void* arg,
                     uint32_t* stack_words, size_t n_words,
                     const hrt_task_attr_t* attr);
void hrt_start(void);
```

- `hrt_init` initializes the kernel, applies scheduler configuration, and starts the port tick when the selected tick source requires it.
- `hrt_create_task` registers a static task using the provided stack buffer. Minimum stack constraints depend on the port.
- `hrt_start` enters the scheduler loop. On the null port it returns immediately.

### Control and time

```c
void     hrt_sleep(uint32_t ms);
void     hrt_yield(void);
uint32_t hrt_tick_now(void);
```

### Runtime tuning

```c
void hrt_set_policy(hrt_policy_t p);
void hrt_set_default_timeslice(uint16_t t);
```

- Changing policy affects scheduling decisions from the next scheduling point onward.
- Changing the default slice affects tasks created after the change. Existing tasks keep their configured timeslice.

### Round-robin semantics

- When policy is `HRT_SCHED_RR` or `HRT_SCHED_PRIORITY_RR` and a task has a non-zero `timeslice`, the running task’s quantum is decremented on tick accounting.
- When the quantum reaches zero, the scheduler reschedule path is pended and the actual rotation happens at a safe scheduling point.
- Tasks with `timeslice == 0` are cooperative within their priority class and will not be rotated by time slicing.

### Semaphores

The kernel provides a minimal semaphore primitive in `hardrt_sem.h`.

```c
void hrt_sem_init(hrt_sem_t* s, unsigned init);
void hrt_sem_init_counting(hrt_sem_t* s, unsigned init, uint8_t max_count);

int  hrt_sem_take(hrt_sem_t* s);
int  hrt_sem_try_take(hrt_sem_t* s);
int  hrt_sem_give(hrt_sem_t* s);
int  hrt_sem_give_from_isr(hrt_sem_t* s, int* need_switch);
```

Notes:
- Binary semaphores saturate at `1`.
- Counting semaphores saturate at `max_count`.
- Waiters are queued FIFO.
- `hrt_sem_give_from_isr()` is supported.
- Semaphores are **not owner-tracked**. For mutual exclusion, prefer `hrt_mutex_t`.

See `docs/SEMAPHORES.md` for details.

### Mutexes

The kernel provides a dedicated mutex primitive in `hardrt_mutex.h`.

```c
#define HRT_MUTEX_NO_OWNER (-1)

typedef struct {
    volatile uint8_t locked;
    int16_t owner;
    uint8_t q[HARDRT_MAX_TASKS];
    uint8_t head;
    uint8_t tail;
    uint8_t count_wait;
} hrt_mutex_t;

void hrt_mutex_init(hrt_mutex_t* m);
int  hrt_mutex_lock(hrt_mutex_t* m);
int  hrt_mutex_try_lock(hrt_mutex_t* m);
int  hrt_mutex_unlock(hrt_mutex_t* m);
```

Notes:
- Mutexes are **owner-tracked** and **non-recursive**.
- `hrt_mutex_lock()` blocks until ownership is acquired.
- `hrt_mutex_unlock()` may directly hand ownership to the next waiter.
- Waiters are queued FIFO.
- Mutex calls are **task-context only**. There is no ISR mutex API.
- The current implementation does **not** include timed lock, recursive mutexes, or priority inheritance.

See `docs/MUTEXES.md` for full semantics.

### Queues

The kernel provides fixed-size copy-based message queues in `hardrt_queue.h`.

```c
void hrt_queue_init(hrt_queue_t *q, void *storage, uint16_t capacity, size_t item_size);
int  hrt_queue_send(hrt_queue_t *q, const void *item);
int  hrt_queue_try_send(hrt_queue_t *q, const void *item);
int  hrt_queue_try_send_from_isr(hrt_queue_t *q, const void *item, int *need_switch);
int  hrt_queue_recv(hrt_queue_t *q, void *out);
int  hrt_queue_try_recv(hrt_queue_t *q, void *out);
int  hrt_queue_try_recv_from_isr(hrt_queue_t *q, void *out, int *need_switch);
uint16_t hrt_queue_count(const hrt_queue_t *q);
```

### Minimal example

```c
#include "hardrt.h"

static uint32_t stackA[2048];
static uint32_t stackB[2048];

static void taskA(void*){ for(;;){ hrt_sleep(500); } }
static void taskB(void*){ for(;;){ hrt_sleep(1000);} }

int main(void){
    hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5,
        .core_hz = 0,
        .tick_src = HRT_TICK_SYSTICK
    };
    hrt_init(&cfg);
    hrt_task_attr_t p0 = { .priority = HRT_PRIO0, .timeslice = 0 };
    hrt_task_attr_t p1 = { .priority = HRT_PRIO1, .timeslice = 5 };
    hrt_create_task(taskA, 0, stackA, 2048, &p0);
    hrt_create_task(taskB, 0, stackB, 2048, &p1);
    hrt_start();
}
```
