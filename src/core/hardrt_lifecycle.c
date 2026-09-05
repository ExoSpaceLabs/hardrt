/* SPDX-License-Identifier: Apache-2.0 */
#include "hardrt.h"
#include "hardrt_port.h"
#include "hardrt_port_int.h"

/* Private legacy core entry points. hardrt_core.c is compiled with these public
 * names remapped so this file can own the checked v0.5 lifecycle facade. */
int hrt__init_impl(const hrt_config_t *cfg);
int hrt__create_task_impl(hrt_task_fn fn, void *arg,
                          uint32_t *stack_words, size_t n_words,
                          const hrt_task_attr_t *attr);
void hrt__start_impl(void);

typedef enum {
    HRT_KERNEL_UNINITIALIZED = 0,
    HRT_KERNEL_INITIALIZED,
    HRT_KERNEL_RUNNING
} hrt_kernel_state_t;

static hrt_kernel_state_t g_kernel_state = HRT_KERNEL_UNINITIALIZED;

static int valid_policy(const hrt_policy_t policy) {
    return policy == HRT_SCHED_PRIORITY ||
           policy == HRT_SCHED_RR ||
           policy == HRT_SCHED_PRIORITY_RR;
}

static int valid_tick_source(const hrt_tick_source_t source) {
    return source == HRT_TICK_SYSTICK || source == HRT_TICK_EXTERNAL;
}

static int valid_tick_hz(const uint32_t tick_hz) {
    return tick_hz >= HRT_TICK_HZ_MIN && tick_hz <= HRT_TICK_HZ_MAX;
}

hrt_status_t hrt_init(const hrt_config_t *cfg) {
    if (g_kernel_state != HRT_KERNEL_UNINITIALIZED) {
        hrt_error(ERR_INVALID_STATE);
        return HRT_ERR_ALREADY_INITIALIZED;
    }

    hrt_config_t effective = {
        .tick_hz = 1000u,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5u,
        .core_hz = 0u,
        .tick_src = HRT_TICK_SYSTICK
    };

    if (cfg != NULL) {
        if (!valid_tick_hz(cfg->tick_hz) ||
            !valid_policy(cfg->policy) ||
            !valid_tick_source(cfg->tick_src)) {
            hrt_error(ERR_INVALID_CONFIG);
            return HRT_ERR_INVALID_CONFIG;
        }
        effective = *cfg;
    }

    if (hrt__init_impl(&effective) != 0) {
        /* The implementation resets its core-owned structures before port
         * configuration. Keep lifecycle UNINITIALIZED so a corrected
         * configuration can retry initialization safely. */
        return HRT_ERR_PORT_INIT;
    }

    g_kernel_state = HRT_KERNEL_INITIALIZED;
    return HRT_OK;
}

int hrt_create_task(hrt_task_fn fn, void *arg,
                    uint32_t *stack_words, size_t n_words,
                    const hrt_task_attr_t *attr) {
    if (g_kernel_state == HRT_KERNEL_UNINITIALIZED) {
        hrt_error(ERR_INVALID_STATE);
        return -1;
    }

    if (g_kernel_state == HRT_KERNEL_RUNNING) {
        /* Dynamic creation is permitted after scheduler start. Serialize the
         * allocation and READY publication against tick/scheduler activity so
         * EXITED-slot reclamation remains safe on both reference ports. Task
         * creation itself does not force immediate preemption; the new READY
         * task participates at the next scheduling point. */
        hrt_port_crit_enter();
        const int id = hrt__create_task_impl(fn, arg, stack_words, n_words, attr);
        hrt_port_crit_exit();
        return id;
    }

    return hrt__create_task_impl(fn, arg, stack_words, n_words, attr);
}

hrt_status_t hrt_start(void) {
    if (g_kernel_state != HRT_KERNEL_INITIALIZED) {
        hrt_error(ERR_INVALID_STATE);
        return HRT_ERR_INVALID_STATE;
    }

    g_kernel_state = HRT_KERNEL_RUNNING;
    hrt__start_impl();
    return HRT_OK;
}

#ifdef HARDRT_TEST_HOOKS
void hrt__test_reset_kernel_state(void) {
    g_kernel_state = HRT_KERNEL_UNINITIALIZED;
}

int hrt__test_kernel_state(void) {
    return (int)g_kernel_state;
}
#endif
