#include "hardrtpp.hpp"
#include <cstdio>

using namespace hardrt;

static Semaphore sem(0, 5);

static void producer(void *arg) {
    (void)arg;
    for (;;) {
        std::puts("[P] burst give x3");
        sem.give();
        sem.give();
        sem.give();
        Task::sleep(300);
    }
}

static void consumer(void *arg) {
    (void)arg;
    for (;;) {
        sem.take();
        std::puts("[C] took token");
        Task::sleep(200);
    }
}

int main() {
    const hrt_config_t cfg = { 1000, HRT_SCHED_PRIORITY_RR, 5, 0, HRT_TICK_SYSTICK };
    if (System::init(cfg) != 0) {
        std::puts("HardRT init failed");
        return 1;
    }

    if (Task::create<2048, 0>(producer, nullptr, HRT_PRIO0, 0) < 0) {
        std::puts("create producer failed");
        return 1;
    }
    if (Task::create<2048, 1>(consumer, nullptr, HRT_PRIO1, 0) < 0) {
        std::puts("create consumer failed");
        return 1;
    }

    System::start();
    return 0;
}
