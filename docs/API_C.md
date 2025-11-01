
## 🧠 API Overview (C)

This page summarizes the public C API available in HeaRTOS v0.2.0. See `inc/heartos.h` for authoritative declarations.

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
    uint32_t     tick_hz;        /* default: 1000 */
    hrt_policy_t policy;         /* default: PRIORITY_RR */
    uint16_t     default_slice;  /* RR timeslice in ticks; 0 disables RR by default */
} hrt_config_t;

/* Per-task attributes passed at creation */
typedef struct {
    hrt_prio_t priority;   /* default: HRT_PRIO1 if attr==NULL */
    uint16_t   timeslice;  /* 0 = cooperative in class; otherwise quantum in ticks */
} hrt_task_attr_t;
```

### Version and port identity

```c
const char* hrt_version_string(void);  /* e.g. "0.2.0" */
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

- `hrt_init` starts the port tick using `cfg->tick_hz` (defaults to 1000 Hz if 0). It also sets the scheduler policy and default timeslice.
- `hrt_create_task` registers a static task using the provided stack buffer. Minimum 64 words are required on hosted platforms (POSIX). If `attr==NULL`, the task inherits `cfg->default_slice` and default priority `HRT_PRIO1`.
- `hrt_start` enters the scheduler loop (does not return on POSIX; is a no-op on the null port).

### Control and time

```c
void     hrt_sleep(uint32_t ms);  /* sleep for ms; wakes on future ticks */
void     hrt_yield(void);         /* yield to allow others in same class */
uint32_t hrt_tick_now(void);      /* system tick counter (increments at tick_hz; wraps on overflow) */
```

### Runtime tuning

```c
void hrt_set_policy(hrt_policy_t p);
void hrt_set_default_timeslice(uint16_t t);
```

- Changing policy or default slice affects scheduling of READY tasks created after the change; existing tasks keep their configured `timeslice`.

### Round-robin semantics

- When policy is `HRT_SCHED_RR` or `HRT_SCHED_PRIORITY_RR` and a task has a non-zero `timeslice`, the running task’s quantum (`slice_left`) is decremented every tick.
- When the quantum reaches zero, a reschedule is pended from the tick ISR, and the actual rotation to the tail of the ready queue occurs when returning to the scheduler (safe context). This is how the POSIX and null ports integrate today.
- Tasks with `timeslice == 0` are cooperative within their priority class and will not be rotated by time slicing (but they can `hrt_yield()`).

### Semaphores

The kernel provides a minimal binary semaphore in `heartos_sem.h`:

```c
/* Binary semaphore type */
typedef struct { volatile uint8_t count; /* 0/1 */ } hrt_sem_t; /* internal fields omitted */

void hrt_sem_init(hrt_sem_t* s, unsigned init);
int  hrt_sem_take(hrt_sem_t* s);          /* blocks until available; 0 on success */
int  hrt_sem_try_take(hrt_sem_t* s);      /* 0 on success, -1 if not available */
int  hrt_sem_give(hrt_sem_t* s);          /* wakes exactly one waiter if present */
int  hrt_sem_give_from_isr(hrt_sem_t* s, int* need_switch); /* ISR-safe */
```

Notes
- Binary semantics: multiple consecutive `give()` calls do not overflow; one subsequent `take()` will succeed.
- Waiters are queued FIFO; on wake the task is made READY and normal priority/RR scheduling applies.
- If a waiter is woken from task context, the giver yields so a higher‑priority waiter can run immediately.
- The ISR variant only pends a context switch and optionally sets `*need_switch=1`.

See `docs/SEMAPHORES.md` for a full guide and examples.

### Minimal example

```c
#include "heartos.h"

static uint32_t stackA[2048];
static uint32_t stackB[2048];

static void taskA(void*){ for(;;){ hrt_sleep(500); } }
static void taskB(void*){ for(;;){ hrt_sleep(1000);} }

int main(void){
    hrt_config_t cfg = { .tick_hz=1000, .policy=HRT_SCHED_PRIORITY_RR, .default_slice=5 };
    hrt_init(&cfg);
    hrt_task_attr_t p0 = { .priority=HRT_PRIO0, .timeslice=0 };
    hrt_task_attr_t p1 = { .priority=HRT_PRIO1, .timeslice=5 };
    hrt_create_task(taskA, 0, stackA, 2048, &p0);
    hrt_create_task(taskB, 0, stackB, 2048, &p1);
    hrt_start();
}
```