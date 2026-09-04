#ifndef HARDRT_H
#define HARDRT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Maximum number of concurrent application tasks.
 *
 * CMake defines this from HARDRT_CFG_MAX_TASKS. One additional private task
 * control slot is reserved by the kernel for idle and is not part of this
 * application capacity.
 *
 * For compatibility with direct/non-CMake builds that define only the legacy
 * HARDRT_MAX_TASKS total-slot macro, the application capacity is inferred as
 * HARDRT_MAX_TASKS - 1.
 */
#ifndef HARDRT_APP_MAX_TASKS
# ifdef HARDRT_MAX_TASKS
#  if HARDRT_MAX_TASKS < 2
#   error "HARDRT_MAX_TASKS must leave at least one application slot plus idle"
#  endif
#  define HARDRT_APP_MAX_TASKS (HARDRT_MAX_TASKS - 1)
# else
#  define HARDRT_APP_MAX_TASKS 8
# endif
#endif

/**
 * @brief Total number of task-control slots, including the private idle slot.
 *
 * This legacy macro is retained for source compatibility. Application code
 * that needs the number of creatable tasks should use HARDRT_APP_MAX_TASKS.
 */
#ifndef HARDRT_MAX_TASKS
#define HARDRT_MAX_TASKS (HARDRT_APP_MAX_TASKS + 1)
#endif

#if HARDRT_MAX_TASKS != (HARDRT_APP_MAX_TASKS + 1)
#error "HARDRT_MAX_TASKS must equal HARDRT_APP_MAX_TASKS + 1 private idle slot"
#endif

/**
 * @brief Number of priority classes supported by the scheduler.
 * @note Priority 0 is the highest priority.
 */
#ifndef HARDRT_MAX_PRIO
#define HARDRT_MAX_PRIO 4
#endif

#include "hardrt_cfg.h"
#include "hardrt_time.h"
#include "hardrt_sem.h"
#include "hardrt_mutex.h"
#include "hardrt_queue.h"

/** Task entry function signature. */
typedef void (*hrt_task_fn)(void *);

/** Kernel error identifiers currently exposed for diagnostics. */
typedef enum {
    NONE = 0,
    ERR_INVALID_ID = 1,
    ERR_INVALID_NEXT_ID = 2,
    ERR_SP_NULL = 3,
    ERR_TCB_NULL = 4,
    ERR_INVALID_TASK = 5,
    ERR_NO_TASKS = 6,
    ERR_INVALID_PRIO = 7,
    ERR_RQ_OVERFLOW = 8,
    ERR_RQ_UNDERFLOW = 9,
    ERR_INVALID_ID_FROM_RQ = 10,
    ERR_STACK_UNDERFLOW_INIT = 11,
    ERR_STACK_RANGE = 12,
    ERR_STACK_ALIGN = 13,
    ERR_INVALID_RAM_RANGE = 14,
    ERR_DUP_READY = 15,
    ERR_MUTEX_OWNER = 16,
    ERR_MUTEX_RECURSIVE = 17,
    ERR_MUTEX_BAD_CTX = 18,
    ERR_TICK_SOURCE_MISMATCH = 19
} hrt_err;

/**
 * @brief Scheduler policy.
 *
 * `HRT_SCHED_PRIORITY` uses strict fixed-priority FIFO scheduling.
 * `HRT_SCHED_RR` uses one global FIFO and ignores task priority.
 * `HRT_SCHED_PRIORITY_RR` uses strict priority with round-robin rotation
 * inside each priority class.
 */
typedef enum {
    HRT_SCHED_PRIORITY = 0,
    HRT_SCHED_RR,
    HRT_SCHED_PRIORITY_RR
} hrt_policy_t;

/** Logical priority values; 0 is highest. */
typedef enum {
    HRT_PRIO0 = 0,
    HRT_PRIO1,
    HRT_PRIO2,
    HRT_PRIO3,
    HRT_PRIO4,
    HRT_PRIO5,
    HRT_PRIO6,
    HRT_PRIO7,
    HRT_PRIO8,
    HRT_PRIO9,
    HRT_PRIO10,
    HRT_PRIO11
} hrt_prio_t;

typedef enum {
    HRT_TICK_SYSTICK = 0,
    HRT_TICK_EXTERNAL = 1
} hrt_tick_source_t;

/** Kernel initialization parameters. */
typedef struct {
    uint32_t tick_hz;
    hrt_policy_t policy;
    uint16_t default_slice;
    uint32_t core_hz;
    hrt_tick_source_t tick_src;
} hrt_config_t;

/** Per-task attributes supplied at task creation. */
typedef struct {
    hrt_prio_t priority;
    uint16_t timeslice;
} hrt_task_attr_t;

/** Return the semantic version string, for example "0.4.0". */
const char *hrt_version_string(void);

/** Return the version encoded as 0xMMmmpp. */
unsigned hrt_version_u32(void);

/** Return the selected port name. */
const char *hrt_port_name(void);

/** Return the selected numeric port identifier. */
int hrt_port_id(void);

/** Initialize kernel state. */
int hrt_init(const hrt_config_t *cfg);

/** Create a task and place it into the ready state. */
int hrt_create_task(hrt_task_fn fn, void *arg,
                    uint32_t *stack_words, size_t n_words,
                    const hrt_task_attr_t *attr);

/** Enter the scheduler. */
void hrt_start(void);

/** Sleep the calling task for at least the specified milliseconds. */
void hrt_sleep(uint32_t ms);

/** Voluntarily yield the processor. */
void hrt_yield(void);

/** Permanently remove the current task from the scheduler. */
void hrt_task_delete(void);

/** Return the current system tick count. */
uint32_t hrt_tick_now(void);

/** Return elapsed system time in milliseconds. */
uint32_t hrt_now_ms(void);

/**
 * @brief Switch scheduler policy at runtime.
 *
 * READY queues are rebuilt for the target policy under the kernel critical
 * section. A running application task treats the change as a scheduling point.
 */
void hrt_set_policy(hrt_policy_t p);

/** Set the default timeslice for subsequently created tasks. */
void hrt_set_default_timeslice(uint16_t t);

/** Record a kernel error according to the configured error policy. */
void hrt_error(hrt_err code);

#ifdef __cplusplus
}
#endif

#endif /* HARDRT_H */