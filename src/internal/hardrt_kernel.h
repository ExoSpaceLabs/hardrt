#ifndef HARDRT_KERNEL_INTERNAL_H
#define HARDRT_KERNEL_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "hardrt.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Kernel-private TCB-slot ownership. Slot allocation is deliberately separate
 * from task execution state: an EXITED task may continue to own its slot until
 * the kernel reclaims it for a later creation. Slot ownership is allocation /
 * lifetime metadata; normal scheduling, wake, and tick paths rely on task state
 * and queue membership instead of consulting it.
 */
typedef enum {
    HRT_SLOT_UNUSED = 0,
    HRT_SLOT_USED
} hrt_slot_state_t;

/*
 * Kernel-private task execution states. A value is meaningful only while the
 * containing TCB slot is HRT_SLOT_USED.
 */
typedef enum {
    HRT_READY = 0,
    HRT_RUNNING,
    HRT_SLEEP,
    HRT_BLOCKED,
    HRT_EXITED
} hrt_state_t;

/* Kernel-private task control block. Ports may access this only through the
 * private core/port contract; applications must never depend on its layout. */
typedef struct {
    uint32_t *sp;
    uint32_t *stack_base;
    size_t stack_words;
    void (*entry)(void *);
    void *arg;
    uint32_t wake_tick;
    uint16_t timeslice_cfg;
    uint16_t slice_left;
    uint8_t prio;
    uint8_t state;
    uint8_t slot_state;
} _hrt_tcb_t;

/* HARDRT_APP_MAX_TASKS is the number of creatable application tasks.
 * HARDRT_TOTAL_TASKS adds exactly one private idle slot. HARDRT_MAX_TASKS is
 * retained publicly as the legacy total-slot compatibility macro. */
#define HARDRT_TOTAL_TASKS HARDRT_MAX_TASKS
#define HRT_IDLE_ID HARDRT_APP_MAX_TASKS
#define HARDRT_IDLE_STACK_WORDS 64u

#if HARDRT_TOTAL_TASKS != (HARDRT_APP_MAX_TASKS + 1)
#error "HardRT internal task capacity must be application tasks + one idle slot"
#endif

_hrt_tcb_t *hrt__tcb(int id);
uint32_t *_get_sp(int id);
void _set_sp(int id, uint32_t *sp);

int hrt__get_current(void);
void hrt__set_current(int id);
void hrt__make_ready(int id);
void hrt__requeue_noreset(int id);
void hrt__requeue_front_noreset(int id);
void hrt__prepare_current_for_reschedule(void);
int hrt__should_preempt_after_wake(int woken_id);
int hrt__sleep_tick(void);
int hrt__pick_next_ready(void);
void hrt__on_scheduler_entry(void);

void hrt__inc_tick(void);
hrt_policy_t hrt__policy(void);
uint32_t hrt__cfg_core_hz(void);
hrt_tick_source_t hrt__cfg_tick_src(void);
uint32_t hrt__cfg_tick_hz(void);
void hrt__tick_isr(void);

void hrt__save_current_sp(uintptr_t sp);
uintptr_t hrt__load_next_sp_and_set_current(int next_id);
uintptr_t hrt__schedule(uintptr_t old_sp);

#ifdef HARDRT_TEST_HOOKS
void hrt__test_set_tick(uint32_t v);
uint32_t hrt__test_get_tick(void);
uint16_t hrt__test_task_slice_left(int id);
int hrt__test_ready_occurrences(int id);
int hrt__test_task_ready_queued(int id);
uint32_t hrt__test_ready_prio_mask(void);
int hrt__test_task_state(int id);
int hrt__test_slot_state(int id);
void hrt__test_reset_kernel_state(void);
int hrt__test_kernel_state(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* HARDRT_KERNEL_INTERNAL_H */
