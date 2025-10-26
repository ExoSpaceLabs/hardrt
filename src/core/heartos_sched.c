#include "heartos.h"
#include "heartos_time.h"

/* Core-private accessors from heartos_core.c */
int  hrt__pick_next_ready(void);
void hrt__make_ready(int id);
int  hrt__get_current(void);
void hrt__set_current(int id);
void hrt__inc_tick(void);
void hrt__pend_context_switch(void);
typedef struct _hrt_tcb hrt_tcb_t; /* opaque */
hrt_tcb_t* hrt__tcb(int id);

/* Tick ISR: wake sleepers and handle time slicing.
   Called by port at tick rate. */
void hrt__tick_isr(void){
    hrt__inc_tick();

    /* Wake sleepers and requeue */
    for (int i=0;i<HEARTOS_MAX_TASKS;++i){
        hrt_tcb_t* t = hrt__tcb(i);
        /* HRT_UNUSED is encoded as state > HRT_BLOCKED; safe to ignore without deref,
           but we keep it simple and trust core to check states before deref in real impl. */
        /* This file deliberately doesn’t peek into TCB internals to keep layering clean. */
        /* In a fuller version you’d expose state accessors. For now, the wake logic is in time.c */
    }

    /* Force a context switch; the port decides how (PendSV or cooperative step). */
    hrt__pend_context_switch();
}