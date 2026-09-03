#include "hardrtpp.hpp"
#include <cstdio>

using namespace hardrt;

static Mutex mutex;

static void A(void *arg) {
    (void)arg;
    for (;;) {
        std::puts("[A] waiting for mutex");
        if (mutex.lock() == 0) {
            std::puts("[A] locked mutex");
            Task::sleep(200);
            std::puts("[A] unlocking mutex");
            mutex.unlock();
        }
        Task::sleep(200);
    }
}

static void B(void *arg) {
    (void)arg;
    for (;;) {
        if (mutex.try_lock() == 0) {
            std::puts("[B] got mutex via try_lock");
            Task::sleep(150);
            mutex.unlock();
        } else {
            std::puts("[B] mutex busy, waiting");
        }
        Task::sleep(50);
    }
}

int main() {
    const hrt_config_t cfg = { 1000, HRT_SCHED_PRIORITY_RR, 5, 0, HRT_TICK_SYSTICK };
    if (System::init(cfg) != 0) {
        std::puts("HardRT init failed");
        return 1;
    }

    if (Task::create<2048, 0>(A, nullptr, HRT_PRIO0, 5) < 0) {
        std::puts("create A failed");
        return 1;
    }
    if (Task::create<2048, 1>(B, nullptr, HRT_PRIO1, 5) < 0) {
        std::puts("create B failed");
        return 1;
    }

    std::puts("Starting mutex basic C++ example...");
    System::start();
    return 0;
}
