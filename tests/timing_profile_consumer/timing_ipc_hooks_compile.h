#ifndef HARDRT_TIMING_IPC_HOOKS_COMPILE_H
#define HARDRT_TIMING_IPC_HOOKS_COMPILE_H

extern volatile unsigned hrt_timing_fixture_counter;

#define HRT_TIMING_ISR_IPC_ENTRY() \
    do { hrt_timing_fixture_counter += 1u; } while (0)

#define HRT_TIMING_ISR_WAITER_READY(task_id) \
    do { (void)(task_id); hrt_timing_fixture_counter += 4u; } while (0)

#define HRT_TIMING_ISR_IPC_EXIT() \
    do { hrt_timing_fixture_counter += 2u; } while (0)

#endif
