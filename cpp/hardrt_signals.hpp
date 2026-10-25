#pragma once

#include "hardrt_event.h"
#include "hardrt_notify.h"

#include <cstdint>

namespace hardrt {

/** Object-oriented wrapper for a statically stored HardRT event-flag group. */
class EventFlags {
public:
    EventFlags() {
        hrt_event_init(&_event);
    }

    uint32_t bits() const {
        return hrt_event_get(&_event);
    }

    int set(const uint32_t bits) {
        return hrt_event_set(&_event, bits);
    }

    int set_from_isr(const uint32_t bits, int& need_switch) {
        return hrt_event_set_from_isr(&_event, bits, &need_switch);
    }

    int clear(const uint32_t bits) {
        return hrt_event_clear(&_event, bits);
    }

    int clear_from_isr(const uint32_t bits) {
        return hrt_event_clear_from_isr(&_event, bits);
    }

    int wait_any(const uint32_t mask,
                 uint32_t& matched,
                 const bool clear_on_exit = false) {
        const unsigned options = clear_on_exit
            ? static_cast<unsigned>(HRT_EVENT_CLEAR_ON_EXIT)
            : static_cast<unsigned>(HRT_EVENT_WAIT_ANY);
        return hrt_event_wait(&_event, mask, options, &matched);
    }

    int wait_all(const uint32_t mask,
                 uint32_t& matched,
                 const bool clear_on_exit = false) {
        unsigned options = static_cast<unsigned>(HRT_EVENT_WAIT_ALL);
        if (clear_on_exit) {
            options |= static_cast<unsigned>(HRT_EVENT_CLEAR_ON_EXIT);
        }
        return hrt_event_wait(&_event, mask, options, &matched);
    }

    hrt_wait_result_t wait_any_until(const uint32_t mask,
                                     uint32_t& matched,
                                     const hrt_tick_t deadline,
                                     const bool clear_on_exit = false) {
        const unsigned options = clear_on_exit
            ? static_cast<unsigned>(HRT_EVENT_CLEAR_ON_EXIT)
            : static_cast<unsigned>(HRT_EVENT_WAIT_ANY);
        return hrt_event_wait_until(&_event, mask, options, deadline, &matched);
    }

    hrt_wait_result_t wait_all_until(const uint32_t mask,
                                     uint32_t& matched,
                                     const hrt_tick_t deadline,
                                     const bool clear_on_exit = false) {
        unsigned options = static_cast<unsigned>(HRT_EVENT_WAIT_ALL);
        if (clear_on_exit) {
            options |= static_cast<unsigned>(HRT_EVENT_CLEAR_ON_EXIT);
        }
        return hrt_event_wait_until(&_event, mask, options, deadline, &matched);
    }

    hrt_event_t* native_handle() {
        return &_event;
    }

    const hrt_event_t* native_handle() const {
        return &_event;
    }

private:
    hrt_event_t _event{};
};

/** Strongly typed C++ notification producer actions. */
enum class NotifyAction : uint8_t {
    set_bits,
    overwrite,
    no_overwrite,
    increment
};

/** Lightweight static helpers for per-task notification operations. */
class TaskNotification {
public:
    static int notify(const int task_id,
                      const uint32_t value,
                      const NotifyAction action = NotifyAction::overwrite) {
        return hrt_task_notify(task_id, value, to_c_action(action));
    }

    static int notify_from_isr(const int task_id,
                               const uint32_t value,
                               const NotifyAction action,
                               int& need_switch) {
        return hrt_task_notify_from_isr(task_id, value, to_c_action(action), &need_switch);
    }

    static int wait(uint32_t& value,
                    const uint32_t clear_on_entry = 0u,
                    const uint32_t clear_on_exit = 0u) {
        return hrt_task_notify_wait(clear_on_entry, clear_on_exit, &value);
    }

    static hrt_wait_result_t wait_until(uint32_t& value,
                                        const hrt_tick_t deadline,
                                        const uint32_t clear_on_entry = 0u,
                                        const uint32_t clear_on_exit = 0u) {
        return hrt_task_notify_wait_until(clear_on_entry, clear_on_exit,
                                          deadline, &value);
    }

    static uint32_t take(const bool clear_count_on_exit = false) {
        return hrt_task_notify_take(clear_count_on_exit ? 1 : 0);
    }

    static hrt_wait_result_t take_until(uint32_t& value,
                                        const hrt_tick_t deadline,
                                        const bool clear_count_on_exit = false) {
        return hrt_task_notify_take_until(clear_count_on_exit ? 1 : 0,
                                          deadline, &value);
    }

private:
    static constexpr hrt_notify_action_t to_c_action(const NotifyAction action) {
        switch (action) {
            case NotifyAction::set_bits: return HRT_NOTIFY_SET_BITS;
            case NotifyAction::overwrite: return HRT_NOTIFY_OVERWRITE;
            case NotifyAction::no_overwrite: return HRT_NOTIFY_NO_OVERWRITE;
            case NotifyAction::increment: return HRT_NOTIFY_INCREMENT;
        }
        return HRT_NOTIFY_OVERWRITE;
    }
};

} // namespace hardrt
