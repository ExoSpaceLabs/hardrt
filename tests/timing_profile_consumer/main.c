#include "hardrt.h"
#include "hardrt_sem.h"

volatile unsigned hrt_timing_fixture_counter = 0u;

int main(void) {
    if (hrt_init(NULL) != 0) return 1;

    hrt_sem_t sem;
    hrt_sem_init(&sem, 0);

    int need_switch = -1;
    if (hrt_sem_give_from_isr(&sem, &need_switch) != 0) return 2;

    /* No waiter exists, so only IPC ENTRY (+1) and EXIT (+2) should fire. */
    if (need_switch != 0) return 3;
    if (hrt_timing_fixture_counter != 3u) return 4;

    return 0;
}
