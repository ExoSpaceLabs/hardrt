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
#include "hardrt_event.h"
#include "hardrt_notify.h"

/** Task entry function signature. */
typedef void (*hrt_task_fn)(void *);

/**
 * @brief Public lifecycle/configuration status codes.
 *
 * These statuses are deliberately separate from hrt_err, which remains the
 * lower-level kernel diagnostic channel used for invariant/API diagnostics.
 */
typedef enum {
    HRT_OK = 0,
    HRT_ERR_ALREADY_INITIALIZED = -1,
    HRT_ERR_INVALID_CONFIG = -2,
    HRT_ERR_INVALID_STATE = -3,
    HRT_ERR_PORT_INIT = -4
} hrt_status_t;

/** Tick/deadline value in the wrapping HardRT tick domain. */
typedef uint32_t hrt_tick_t;

/**
 * Absolute-deadline ordering is unambiguous only within half of the uint32_t
 * tick domain. Applications must keep each intended future/past comparison
 * within HRT_TICK_MAX_HORIZON ticks.
 */
#define HRT_TICK_HALF_RANGE UINT32_C(0x80000000)
#define HRT_TICK_MAX_HORIZON UINT32_C(0x7FFFFFFF)

/** Result of waiting for an absolute periodic release deadline. */
typedef enum {
    HRT_DELAY_OK = 0,
    HRT_DELAY_MISSED = 1,
    HRT_DELAY_INVALID_DEADLINE = -1,
    HRT_DELAY_INVALID_CONTEXT = -2
} hrt_delay_result_t;

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
    ERR_TICK_SOURCE_MISMATCH = 19,
    ERR_STACK_IN_USE = 20,
    ERR_INVALID_STATE = 21,
    ERR_INVALID_CONFIG = 22
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

/** Tick-source ownership mode selected at initialization. */
typedef enum {
    HRT_TICK_SYSTICK = 0,
    HRT_TICK_EXTERNAL = 1
} hrt_tick_source_t;

/** Public tick-frequency range for explicit configuration. */
#define HRT_TICK_HZ_MIN 1u
#define HRT_TICK_HZ_MAX UINT32_MAX

/**
 * @brief Kernel initialization parameters.
 *
 * `tick_hz` must be in `[HRT_TICK_HZ_MIN, HRT_TICK_HZ_MAX]` for an explicit
 * configuration. With a port-owned tick source, the selected port may impose a
 * narrower representable range and `hrt_init()` reports `HRT_ERR_PORT_INIT`
 * when the requested frequency cannot be represented.
 *
 * `core_hz` is consumed by the Cortex-M port only with `HRT_TICK_SYSTICK`.
 * Zero asks that port to obtain the clock through `hrt_port_get_core_hz()`; a
 * non-zero value explicitly overrides that clock for SysTick reload
 * calculation. Ports/tick modes that do not need a CPU core clock ignore this
 * field, so callers may preserve board clock metadata when selecting an
 * external tick source.
 */
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

/** Return the semantic version string, for example "0.5.1". */
const char *hrt_version_string(void);

/** Return the version encoded as 0xMMmmpp. */
unsigned hrt_version_u32(void);

/** Return the selected port name. */
const char *hrt_port_name(void);

/** Return the selected numeric port identifier. */
int hrt_port_id(void);

/* The legacy core implementation is privately renamed at build time so the
 * lifecycle facade can preserve the historical implementation signatures while
 * exposing the checked public v0.5 contract. */
#ifdef HARDRT_CORE_IMPL
int hrt_init(const hrt_config_t *cfg);
#else
/**
 * @brief Initialize HardRT exactly once before task creation/start.
 * @return HRT_OK on success or a public HRT_ERR_* lifecycle/config status.
 */
hrt_status_t hrt_init(const hrt_config_t *cfg);
#endif

/**
 * Create a task and place it into the ready state.
 *
 * Task creation is valid after successful `hrt_init()`, both before and after
 * `hrt_start()`. Runtime creation is serialized against scheduler/tick state;
 * the new task becomes READY but does not force immediate preemption and joins
 * scheduling at the next scheduling point. The supplied stack must not overlap
 * the stack of any live task. An exited task no longer uses its stack and may
 * have its occupied TCB slot reclaimed by a later task creation.
 *
 * @return Non-negative task ID on success, or -1 on validation/lifecycle error.
 */
int hrt_create_task(hrt_task_fn fn, void *arg,
                    uint32_t *stack_words, size_t n_words,
                    const hrt_task_attr_t *attr);

#ifdef HARDRT_CORE_IMPL
void hrt_start(void);
#else
/**
 * @brief Start the scheduler from the INITIALIZED state.
 * @return HRT_ERR_INVALID_STATE if called before init or after scheduler start;
 *         HRT_OK only on ports/test harnesses where scheduler entry returns.
 * @note Cortex-M normally never returns after a successful call.
 */
hrt_status_t hrt_start(void);
#endif

/**
 * Sleep the calling task for at least the specified milliseconds.
 *
 * A zero duration is an immediate scheduling point equivalent to hrt_yield()
 * and does not enter the sleep queue or wait for a tick. Positive durations
 * shorter than one tick round up to one tick. Calls outside the current
 * RUNNING application task are rejected as safe no-ops with ERR_INVALID_TASK.
 */
void hrt_sleep(uint32_t ms);

/**
 * Sleep until an absolute tick deadline without rebasing periodic phase.
 *
 * If the deadline is still in the future, the task enters the existing bounded
 * sleeper queue for exactly the remaining ticks. On return, the function checks
 * the actual current tick again: equality is on time, while a later tick returns
 * HRT_DELAY_MISSED. This catches both arrival at the API after the deadline and
 * dispatch latency after an on-time sleeper expiry.
 *
 * @param deadline       Absolute deadline in the wrapping tick domain.
 * @param lateness_ticks Optional output. Set to zero when on time, otherwise to
 *                       the number of ticks by which the task resumed/called
 *                       after the deadline.
 * @return HRT_DELAY_OK, HRT_DELAY_MISSED, or a negative validation result.
 *
 * The intended deadline must be within HRT_TICK_MAX_HORIZON ticks of the
 * current tick so wrap-safe ordering remains unambiguous. A deadline exactly
 * HRT_TICK_HALF_RANGE ticks away is rejected.
 */
hrt_delay_result_t hrt_delay_until(hrt_tick_t deadline,
                                   hrt_tick_t *lateness_ticks);

/** Voluntarily yield the processor. Non-task context is rejected safely. */
void hrt_yield(void);

/**
 * Permanently remove the current task from scheduling.
 *
 * The task enters the internal EXITED state. Its TCB slot remains occupied
 * until reclaimed by a later task creation. Calls outside the current RUNNING
 * application task are rejected as safe no-ops with ERR_INVALID_TASK.
 */
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
