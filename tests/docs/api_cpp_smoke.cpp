/* SPDX-License-Identifier: Apache-2.0 */
/* Compile-only probe extracted from the public C++ wrapper documentation. */
#include "hardrtpp.hpp"
#include "hardrt_signals.hpp"

#include <cstdint>

int hardrt_doc_cpp_smoke() {
    const hrt_config_t cfg{
        1000u,
        HRT_SCHED_PRIORITY_RR,
        5u,
        0u,
        HRT_TICK_EXTERNAL
    };

    hardrt::EventFlags event;
    uint32_t matched = 0u;
    uint32_t value = 0u;
    int need_switch = 0;

    (void)event.bits();
    (void)event.set(0x1u);
    (void)event.clear(0x1u);
    (void)event.set_from_isr(0x2u, need_switch);
    (void)event.clear_from_isr(0x2u);
    (void)event.wait_any(0x1u, matched, false);
    (void)event.wait_all(0x3u, matched, true);
    (void)event.native_handle();

    (void)hardrt::TaskNotification::notify(
        0, 0x1u, hardrt::NotifyAction::set_bits);
    (void)hardrt::TaskNotification::notify_from_isr(
        0, 0x2u, hardrt::NotifyAction::overwrite, need_switch);
    (void)hardrt::TaskNotification::wait(value, 0u, UINT32_MAX);
    (void)hardrt::TaskNotification::take(true);

    (void)hardrt::System::init(cfg);
    (void)hardrt::System::tick_now();
    (void)hardrt::System::now_ms();
    (void)hardrt::System::version_string();
    (void)hardrt::System::version();
    (void)hardrt::System::version_u32();
    (void)hardrt::System::port_name();
    (void)hardrt::System::port_id();

    hardrt::Semaphore sem(0u);
    (void)sem.try_take();
    (void)sem.give();
    (void)sem.give_from_isr(need_switch);

    hardrt::Mutex mutex;
    (void)mutex.try_lock();
    (void)mutex.unlock();

    hardrt::StaticQueue<uint32_t, 4> queue;
    uint32_t item = 1u;
    (void)queue.try_send(item);
    (void)queue.try_recv(item);
    (void)queue.try_send_from_isr(item, need_switch);
    (void)queue.try_recv_from_isr(item, need_switch);
    (void)queue.native_handle();

    return static_cast<int>(matched ^ value ^ item ^
                            static_cast<uint32_t>(need_switch));
}
