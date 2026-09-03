#ifndef HARDRT_TIMING_INTERNAL_H
#define HARDRT_TIMING_INTERNAL_H

#include "hardrt_timing_config.h"

/*
 * Private compile-time timing hooks.
 *
 * The generated config optionally includes one benchmark-provided header that
 * may define only the hooks needed by the selected timing profile. Every hook
 * has a no-op fallback, so the default profile generates no timing operations
 * or storage references.
 */

#ifndef HRT_TIMING_ISR_IPC_ENTRY
#define HRT_TIMING_ISR_IPC_ENTRY() ((void)0)
#endif

#ifndef HRT_TIMING_ISR_WAITER_READY
#define HRT_TIMING_ISR_WAITER_READY(task_id) ((void)(task_id))
#endif

#ifndef HRT_TIMING_ISR_IPC_EXIT
#define HRT_TIMING_ISR_IPC_EXIT() ((void)0)
#endif

#endif /* HARDRT_TIMING_INTERNAL_H */
