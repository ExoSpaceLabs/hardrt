#ifndef HARDRT_H
#define HARDRT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {



#endif

/**
 * @brief Maximum number of concurrent tasks supported by the kernel.
 * @note Can be overridden at compile time via -DHARDRT_MAX_TASKS.
 */
#ifndef HARDRT_MAX_TASKS
#define HARDRT_MAX_TASKS 8
#endif

/**
 * @brief Number of priority classes supported by the scheduler.
 * @note Priority 0 is the highest priority. Can be overridden via -DHARDRT_MAX_PRIO.
 */
#ifndef HARDRT_MAX_PRIO
#define HARDRT_MAX_PRIO  4  /* 0..3 (0 is highest) */
#endif


/**
 * @brief Task entry function signature.
 * @param arg Opaque pointer passed to the task on start.
 */
typedef void (*hrt_task_fn)(void *);

/**
 * @brief Runtime state of a task.
 */
typedef enum {
    HRT_READY = 0, /**< Runnable (on ready queue) */
    HRT_SLEEP, /**< Sleeping until a wake tick */
    HRT_BLOCKED, /**< Blocked on a primitive (future use) */
    HRT_UNUSED /**< Slot not in use */
} hrt_state_t;


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
    ERR_DUP_READY = 15
}hrt_err;


/**
 * @brief Scheduler policy.
 * - HRT_SCHED_PRIORITY: strict fixed-priority, cooperative within a class.
 * - HRT_SCHED_RR: single round-robin class.
 * - HRT_SCHED_PRIORITY_RR: fixed-priority with round-robin within each class.
 */
typedef enum {
    HRT_SCHED_PRIORITY = 0,
    HRT_SCHED_RR,
    HRT_SCHED_PRIORITY_RR
} hrt_policy_t;

/**
 * @brief Logical priority values (0 is highest).
 * @note The range effectively usable depends on HARDRT_MAX_PRIO.
 */
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
    HRT_TICK_SYSTICK = 0, // port owns the periodic tick (e.g., SysTick)
    HRT_TICK_EXTERNAL = 1 // app owns a timer and calls hrt_tick_from_isr() in its ISR
} hrt_tick_source_t;

/**
 * @brief Kernel initialization parameters.
 * @note All fields are optional; zero initializes to defaults.
 */
typedef struct {
    uint32_t tick_hz; // kernel tick frequency (Hz)
    hrt_policy_t policy; // scheduler policy
    uint16_t default_slice; // RR timeslice in ticks (0 => default)
    uint32_t core_hz; // CPU clock frequency in Hz (needed by SysTick). 0 means “unknown”
    hrt_tick_source_t tick_src; // HRT_TICK_SYSTICK (default) or HRT_TICK_EXTERNAL
} hrt_config_t;

/**
 * @brief Per-task attributes provided at creation time.
 */
typedef struct {
    hrt_prio_t priority; /**< Task base priority */
    uint16_t timeslice; /**< 0 = cooperative within class; otherwise ticks per slice */
} hrt_task_attr_t;

/* -------- Mirror TCB for stack initialization -------- */
typedef struct {
    uint32_t *sp;
    uint32_t *stack_base;
    size_t    stack_words;
    void    (*entry)(void*);
    void*     arg;
    uint32_t  wake_tick;
    uint16_t  timeslice_cfg;
    uint16_t  slice_left;
    uint8_t   prio;
    uint8_t   state;
} _hrt_tcb_t;

_hrt_tcb_t *hrt__tcb(int id);

/** @brief Stack pointer helper, retrieves the stack pointer for the specified task id.
 *
 * @param id
 * @return
 */
uint32_t *_get_sp(int id);

/** @brief Stack pointer helper, sets the stack pointer for the specified task id.
 *
 * @param id
 * @param sp
 */
void _set_sp(int id, uint32_t *sp);

/**
 *  idle task specification
 */
#define HRT_IDLE_ID   (HARDRT_MAX_TASKS - 1)
#define HARDRT_IDLE_STACK_WORDS 64
static _hrt_tcb_t   g_idle_tcb;
static uint32_t     g_idle_stack[HARDRT_IDLE_STACK_WORDS] __attribute__((aligned(8)));

/**
 * @brief Get the semantic version string of HardRT at runtime.
 * @return NUL-terminated string, e.g. "0.3.0".
 */
const char *hrt_version_string(void);

/**
 * @brief Get a compact 24-bit numeric version.
 * @return Version encoded as 0xMMmmpp (Major, minor, patch).
 */
unsigned hrt_version_u32(void);

/**
 * @brief Get the human-readable port name selected at build time.
 * @return NUL-terminated port string, e.g. "posix", "null", "cortex_m".
 */
const char *hrt_port_name(void);

/**
 * @brief Get the numeric port identifier.
 * @return Integer port id matching HARDRT_PORT_ID from the generated header.
 */
int hrt_port_id(void);

/**
 * @brief Initialize the kernel and start the system tick on the active port.
 * @param cfg Optional configuration; pass NULL for defaults.
 * @return 0 on success; negative on error.
 * @note Must be called before any other API. Safe to call once.
 */
int hrt_init(const hrt_config_t *cfg);

/**
 * @brief Create a task and place it into the ready state.
 * @param fn Task entry function.
 * @param arg Opaque argument forwarded to the task entry.
 * @param stack_words Pointer to the task stack buffer (array of 32-bit words).
 * @param n_words Number of 32-bit words in the stack buffer; minimum 64.
 * @param attr Optional task attributes (priority, timeslice); defaults are applied if NULL.
 * @return Task id (non-negative) on success; negative on failure.
 * @note The buffer pointed to by stack_words must remain valid for the task lifetime.
 * @code
 * static uint32_t stackA[256];
 * void worker (void* arg) { // task body }
 * int id = hrt_create_task(worker, NULL, stackA, 256, NULL);
 * @endcode
 */
int hrt_create_task(hrt_task_fn fn, void *arg,
                    uint32_t *stack_words, size_t n_words,
                    const hrt_task_attr_t *attr);

/**
 * @brief Enter the scheduler loop; does not return on preemptive ports.
 * @note On the null port this returns immediately without running tasks.
 */
void hrt_start(void);

/**
 * @brief Sleep the calling task for at least the specified milliseconds.
 * @param ms Milliseconds to sleep; 0 yields immediately.
 */
void hrt_sleep(uint32_t ms);

/**
 * @brief Yield the processor voluntarily to allow other ready tasks to run.
 */
void hrt_yield(void);

/**
 * @brief Get the current system tick count (monotonic, wraps on overflow).
 * @return Current tick counter.
 */
uint32_t hrt_tick_now(void);

/**
 * @brief Change the scheduler policy at runtime.
 * @param p New policy to apply.
 * @note Effect takes place on the next scheduling point.
 */
void hrt_set_policy(hrt_policy_t p);

/**
 * @brief Change the default round-robin timeslice used when creating tasks.
 * @param t Timeslice in ticks; 0 disables RR for tasks without an explicit slice.
 */
void hrt_set_default_timeslice(uint16_t t);

/* -------- Port hooks (implemented by port layer) -------- */
/**
 * @brief Start the port-specific system tick source with the given frequency.
 * @param tick_hz Tick frequency in Hz.
 * @note Called by hrt_init(). Port must call hrt_tick_from_isr() at each tick.
 */
void hrt_port_start_systick(uint32_t tick_hz);

/**
 * @brief Optional low-power or blocking wait used by the idle loop.
 * @note Port may sleep the CPU until the next interrupt or event.
 */
void hrt_port_idle_wait(void); /* called when idle */

/**
 * @brief Pend a context switch to be performed at a safe point.
 * @note Ports typically implement this by triggering a soft interrupt (e.g., PendSV on Cortex-M).
 */
void hrt__pend_context_switch(void); /* request a switch (e.g., PendSV) */

/**
 * @brief Architecture-specific trampoline that enters the task function.
 * @note Provided by the port; sets up a call to the user task entry and handles return.
 */
void hrt__task_trampoline(void); /* arch-specific entry trampoline */

/**
 * @brief Prepare an initial stack frame / context for a new task.
 * @param id Task id.
 * @param tramp Entry trampoline to start execution.
 * @param stack_base Base of the stack buffer (array of 32-bit words).
 * @param words Number of words in the stack buffer.
 */
void hrt_port_prepare_task_stack(int id, void (*tramp)(void),
                                 uint32_t *stack_base, size_t words);


/**
 * @brief function to identify the error during debug.
 *
 * @param code
 */
void hrt_error(hrt_err code);



#ifdef __cplusplus
}
#endif
#endif
