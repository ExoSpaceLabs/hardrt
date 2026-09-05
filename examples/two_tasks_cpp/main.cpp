#include "hardrtpp.hpp"
#include <cstdio>
#include <cstring>

using namespace hardrt;

static void taskA(void *arg) {
    (void)arg;
    uint32_t counter = 0;
    for (;;) {
        std::printf("[A] tick count [%u]\n", (unsigned)counter);
        std::fflush(stdout);
        Task::sleep(500);
        counter++;
    }
}

static void taskB(void *arg) {
    (void)arg;
    for (;;) {
        std::puts("[B] tock -----");
        std::fflush(stdout);
        Task::sleep(1000);
    }
}

int main() {
    if (std::strcmp(System::version(), System::version_string()) != 0) {
        std::puts("System::version alias mismatch");
        return 1;
    }

    std::printf("HardRT version: %s (0x%06X), port: %s (id=%d)\n",
                System::version(), System::version_u32(),
                System::port_name(), System::port_id());

    const hrt_config_t cfg = {
        1000,
        HRT_SCHED_PRIORITY_RR,
        5,
        0,
        HRT_TICK_SYSTICK
    };

    if (System::init(cfg) != 0) {
        std::puts("HardRT init failed");
        return 1;
    }

    if (Task::create<1024, 0>(taskA, nullptr, HRT_PRIO0, 0) < 0) {
        std::puts("create taskA failed");
        return 1;
    }
    if (Task::create<2048, 1>(taskB, nullptr, HRT_PRIO1, 5) < 0) {
        std::puts("create taskB failed");
        return 1;
    }

    System::start();
    return 0;
}
