
## 🧠 API Overview (C)

*** Add listing of API functions here ***

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