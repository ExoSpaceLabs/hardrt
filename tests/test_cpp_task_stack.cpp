#include "hardrtpp.hpp"

#include <cstdint>

static void dummy_task(void *arg) {
    (void)arg;
}

int main() {
    const hrt_config_t cfg{
        1000,
        HRT_SCHED_PRIORITY_RR,
        0,
        0,
        HRT_TICK_SYSTICK
    };

    if (hardrt::System::init(cfg) != 0) return 1;

    const int first = hardrt::Task::create<128, 42>(
        dummy_task, nullptr, HRT_PRIO0, 0);
    if (first < 0) return 2;

    /* Same specialization means the same function-local static stack. A second
       live task must be rejected rather than sharing one execution stack. */
    const int duplicate = hardrt::Task::create<128, 42>(
        dummy_task, nullptr, HRT_PRIO0, 0);
    if (duplicate >= 0) return 3;

    /* A distinct Tag owns distinct storage and remains a valid task create. */
    const int distinct = hardrt::Task::create<128, 43>(
        dummy_task, nullptr, HRT_PRIO0, 0);
    if (distinct < 0 || distinct == first) return 4;

    return 0;
}
