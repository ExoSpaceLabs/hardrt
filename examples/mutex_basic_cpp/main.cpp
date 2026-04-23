#include "hardrtpp.hpp"
#include <cstdio>

using namespace hardrt;

static Mutex mutex;

static void A(void* arg) {
    (void)arg;
    for (;;) {
        printf("[A] waiting for mutex\n");
        if (mutex.lock() == 0) {
            printf("[A] locked mutex\n");
            Task::sleep(200);
            printf("[A] unlocking mutex\n");
            mutex.unlock();
        }
        Task::sleep(200);
    }
}

static void B(void* arg) {
    (void)arg;
    for (;;) {
        if (mutex.try_lock() == 0) {
            printf("[B] got mutex via try_lock\n");
            Task::sleep(150);
            mutex.unlock();
        } else {
            printf("[B] mutex busy, waiting\n");
        }
        Task::sleep(50);
    }
}

int main() {
    const hrt_config_t cfg = { .tick_hz = 1000, .policy = HRT_SCHED_PRIORITY_RR, .default_slice = 5 };
    System::init(cfg);

    if (Task::create<2048, 0>(A, nullptr, HRT_PRIO0, 5) < 0)
        printf("create A failed\n");
    if (Task::create<2048, 1>(B, nullptr, HRT_PRIO1, 5) < 0)
        printf("create B failed\n");

    printf("Starting mutex basic C++ example...\n");
    System::start();
    return 0;
}
