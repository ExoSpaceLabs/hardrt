#ifndef HEARTOS_H
#define HEARTOS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HEARTOS_MAX_TASKS
#define HEARTOS_MAX_TASKS 8
#endif

#ifndef HEARTOS_MAX_PRIO
#define HEARTOS_MAX_PRIO  4  /* 0..3 (0 is highest) */
#endif

    /* Version */
    const char* hrt_version_string(void);
    unsigned    hrt_version_u32(void);

    /* Port identity */
    const char* hrt_port_name(void);
    int         hrt_port_id(void);


    typedef void (*hrt_task_fn)(void*);

    typedef enum { HRT_READY=0, HRT_SLEEP, HRT_BLOCKED, HRT_UNUSED } hrt_state_t;

    typedef enum { HRT_SCHED_PRIORITY=0, HRT_SCHED_RR, HRT_SCHED_PRIORITY_RR } hrt_policy_t;

    typedef enum { HRT_PRIO0=0, HRT_PRIO1, HRT_PRIO2, HRT_PRIO3 } hrt_prio_t;

    typedef struct {
        uint32_t       tick_hz;        /* default: 1000 */
        hrt_policy_t   policy;         /* default: PRIORITY_RR */
        uint16_t       default_slice;  /* RR timeslice ticks; 0 disables RR */
    } hrt_config_t;

    typedef struct {
        hrt_prio_t     priority;
        uint16_t       timeslice;      /* 0 = cooperative in class */
    } hrt_task_attr_t;

    /* Core API */
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

    /* -------- Port hooks (implemented by port layer) -------- */
    void hrt_port_start_systick(uint32_t tick_hz);
    void hrt_port_idle_wait(void);               /* called when idle */
    void hrt__pend_context_switch(void);         /* request a switch (e.g., PendSV) */
    void hrt__task_trampoline(void);             /* arch-specific entry trampoline */
    void hrt_port_prepare_task_stack(int id, void (*tramp)(void),
                                     uint32_t* stack_base, size_t words);

#ifdef __cplusplus
}
#endif
#endif

