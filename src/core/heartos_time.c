#include "heartos.h"
#include "heartos_time.h"

extern _hrt_tcb_t* hrt__tcb(int id);
extern void       hrt__make_ready(int id);
extern int        hrt__get_current(void);
extern void       hrt__pend_context_switch(void);

/* This minimal file intentionally keeps tick handling light.
   The actual increment and switch request are in sched.c via hrt_tick_from_isr(). */

/* No extra API here yet; placeholder for future time functions. */

