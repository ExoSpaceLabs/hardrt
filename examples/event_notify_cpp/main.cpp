#include "hardrtpp.hpp"
#include "hardrt_signals.hpp"

#include <cstdint>
#include <cstdio>

static hardrt::EventFlags signals;
static int receiver_id = -1;

static void receiver(void *) {
    for (;;) {
        uint32_t matched = 0u;
        if (signals.wait_any(0x1u, matched, true) == 0) {
            std::printf("[event-cpp] matched=0x%08x\n", static_cast<unsigned>(matched));
        }

        uint32_t value = 0u;
        if (hardrt::TaskNotification::wait(value, 0u, UINT32_MAX) == 0) {
            std::printf("[notify-cpp] value=%u\n", static_cast<unsigned>(value));
        }
    }
}

static void producer(void *) {
    uint32_t sequence = 1u;
    for (;;) {
        hardrt::Task::sleep(200u);
        signals.set(0x1u);

        hardrt::Task::sleep(100u);
        hardrt::TaskNotification::notify(receiver_id, sequence++, HRT_NOTIFY_OVERWRITE);
    }
}

int main() {
    const hrt_config_t cfg{
        1000u,
        HRT_SCHED_PRIORITY_RR,
        5u,
        0u,
        HRT_TICK_SYSTICK
    };
    if (hardrt::System::init(cfg) != HRT_OK) {
        std::puts("HardRT init failed");
        return 1;
    }

    receiver_id = hardrt::Task::create<2048, 1>(receiver, nullptr, HRT_PRIO0, 3u);
    if (receiver_id < 0) {
        std::puts("create receiver failed");
        return 1;
    }
    if (hardrt::Task::create<2048, 2>(producer, nullptr, HRT_PRIO2, 3u) < 0) {
        std::puts("create producer failed");
        return 1;
    }

    hardrt::System::start();
    return 0;
}
