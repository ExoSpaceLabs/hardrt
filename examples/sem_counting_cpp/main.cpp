#include "hardrtpp.hpp"
#include <cstdio>

using namespace hardrt;

/* Counting semaphore: 0 initial tokens, saturate at 5. */
static Semaphore sem(0, 5);

static void producer(void*) {
    for (;;) {
        puts("[P] burst give x3");
        sem.give();
        sem.give();
        sem.give();
        Task::sleep(300);
    }
}

static void consumer(void*) {
    for (;;) {
        sem.take();
        puts("[C] took token");
        Task::sleep(200);
    }
}

int main() {
    const hrt_config_t cfg = { .tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5 };
    System::init(cfg);

    if (Task::create<2048, 0>(producer, nullptr, HRT_PRIO0, 0) < 0)
        puts("create producer failed");
    if (Task::create<2048, 1>(consumer, nullptr, HRT_PRIO1, 0) < 0)
        puts("create consumer failed");

    System::start();
    return 0;
}