#include "hardrtpp.hpp"
#include <cstdio>

using namespace hardrt;

static Semaphore sem(1);

static void A(void*) {
    for (;;) {
        sem.take();
        puts("[A] got sem");
        Task::sleep(200);
        sem.give();
        Task::sleep(200);
    }
}

static void B(void*) {
    for (;;) {
        if (sem.try_take() == 0) {
            puts("[B] got sem");
            Task::sleep(100);
            sem.give();
        } else {
            puts("[B] waiting");
        }
        Task::sleep(100);
    }
}

int main() {
    const hrt_config_t cfg = { .tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5 };
    System::init(cfg);

    if (Task::create<2048, 0>(A, nullptr, HRT_PRIO0, 0) < 0)
        puts("create A failed");
    if (Task::create<2048, 1>(B, nullptr, HRT_PRIO1, 5) < 0)
        puts("create B failed");

    System::start();
    return 0;
}
