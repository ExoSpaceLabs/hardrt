#include "hardrtpp.hpp"

#include <cstdio>
#include <cstdint>

/* POSIX port demo (C++): producer -> queue -> consumer */

static hardrt::StaticQueue<uint32_t, 32> q;

static void producer(void*) {
    uint32_t v = 0;
    for (;;) {
        q.send(v);
        std::printf("[producer] sent %u\n", (unsigned)v);
        std::fflush(stdout);
        v++;
        hardrt::Task::sleep(200);
    }
}

static void consumer(void*) {
    uint32_t v = 0;
    for (;;) {
        q.recv(v);
        std::printf("[consumer] got  %u\n", (unsigned)v);
        std::fflush(stdout);
        hardrt::Task::sleep(500);
    }
}

int main() {
    std::printf("HardRT version: %s (0x%06X), port: %s (id=%d)\n",
                hardrt::System::version_string(), hardrt::System::version_u32(),
                hardrt::System::port_name(), hardrt::System::port_id());

    const hrt_config_t cfg = {
        .tick_hz = 1000,
        .policy  = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5
    };

    if (hardrt::System::init(cfg) != 0) {
        std::puts("HardRT init failed");
        return 1;
    }

    /* Task stacks are static (template) in Task::create. */
    (void)hardrt::Task::create<2048, 0>(producer, nullptr, HRT_PRIO0, 0);
    (void)hardrt::Task::create<2048, 1>(consumer, nullptr, HRT_PRIO1, 5);

    hardrt::System::start();
    return 0;
}
