#include "hardrtpp.hpp"
#include <cstdio>

// Using namespaced C++ wrapper
using namespace hardrt;

static void taskA(void* arg) {
    (void)arg;
    uint32_t counter = 0;
    for (;;) {
        printf("[A] tick count [%u]\n", counter);
        fflush(stdout);
        Task::sleep(500);
        counter++;
    }
}

static void taskB(void* arg) {
    (void)arg;
    for (;;) {
        puts("[B] tock -----");
        fflush(stdout);
        Task::sleep(1000);
    }
}

int main() {
    printf("HardRT version: %s (0x%06X), port: %s (id=%d)\n",
           System::version_string(), System::version_u32(),
           System::port_name(), System::port_id());

    const hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy  = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5
    };

    if (System::init(cfg) != 0) {
        puts("HardRT init failed");
        return 1;
    }

    if (Task::create<1024, 0>(taskA, nullptr, HRT_PRIO0, 0) < 0)
        puts("create taskA failed");

    if (Task::create<2048, 1>(taskB, nullptr, HRT_PRIO1, 5) < 0)
        puts("create taskB failed");

    /* Run the scheduler. */
    System::start();

    return 0;
}
