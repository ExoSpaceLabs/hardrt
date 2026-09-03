#include "hardrtpp.hpp"
#include <cstdio>

using namespace hardrt;

static Semaphore sem(1);

static void A(void *arg) {
    (void)arg;
    for (;;) {
        sem.take();
        std::puts("[A] got sem");
        Task::sleep(200);
        sem.give();
        Task::sleep(200);
    }
}

static void B(void *arg) {
    (void)arg;
    for (;;) {
        if (sem.try_take() == 0) {
            std::puts("[B] got sem");
            Task::sleep(100);
            sem.give();
        } else {
            std::puts("[B] waiting");
        }
        Task::sleep(100);
    }
}

int main() {
    const hrt_config_t cfg = { 1000, HRT_SCHED_PRIORITY_RR, 5, 0, HRT_TICK_SYSTICK };
    if (System::init(cfg) != 0) {
        std::puts("HardRT init failed");
        return 1;
    }

    if (Task::create<2048, 0>(A, nullptr, HRT_PRIO0, 0) < 0) {
        std::puts("create A failed");
        return 1;
    }
    if (Task::create<2048, 1>(B, nullptr, HRT_PRIO1, 5) < 0) {
        std::puts("create B failed");
        return 1;
    }

    System::start();
    return 0;
}
