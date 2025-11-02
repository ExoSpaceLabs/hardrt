#include "heartos.h"
#include "heartos_time.h"

/* Access to TCBs from core */
typedef struct {
    uint32_t *sp;
    uint32_t *stack_base;
    size_t    stack_words;
    hrt_task_fn entry;
    void*     arg;
    uint32_t  wake_tick;
    uint16_t  timeslice_cfg;
    uint16_t  slice_left;
    uint8_t   prio;
    uint8_t   state;
} hrt_tcb_t;

extern hrt_tcb_t* hrt__tcb(int id);
extern void       hrt__make_ready(int id);
extern int        hrt__get_current(void);
extern void       hrt__pend_context_switch(void);

/* This minimal file intentionally keeps tick handling light.
   The actual increment and switch request are in sched.c via hrt_tick_from_isr(). */

/* No extra API here yet; placeholder for future time functions. */

