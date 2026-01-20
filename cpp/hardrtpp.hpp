#pragma once
#include "hardrt.h"
#include "hardrt_sem.h"
#include "hardrt_queue.h"

#include <array>
#include <cstddef>
#include <type_traits>

namespace hardrt {

    /**
     * @brief C++ wrapper for task control and management.
     *
     * Provides methods for task creation and control (sleep, yield).
     */
    class Task {
    public:
        /**
         * @brief Create a task with a statically allocated stack.
         *
         * Each unique combination of `StackWords` and `Tag` results in a separate
         * static stack array being allocated.
         *
         * @tparam StackWords Size of the stack in 32-bit words.
         * @tparam Tag Unique identifier to differentiate stacks of the same size.
         *
         * @param fn     Pointer to the task function.
         * @param arg    User argument passed to the task function.
         * @param prio   Task priority.
         * @param slice  Timeslice in ticks (0 for default).
         * @return 0 on success, or a negative error code on failure.
         */
        template <size_t StackWords = 1024, int Tag = 0>
        static int create(const hrt_task_fn fn, void* arg, const hrt_prio_t prio, const uint16_t slice = 0) {
            alignas(8) static uint32_t stack[StackWords];
            const hrt_task_attr_t a{ prio, slice };
            return hrt_create_task(fn, arg, stack, StackWords, &a);
        }

        /**
         * @brief Create a task using a user-provided external stack.
         *
         * @param fn     Pointer to the task function.
         * @param arg    User argument passed to the task function.
         * @param stack  Pointer to the beginning of the stack array (uint32_t).
         * @param words  Size of the stack in 32-bit words.
         * @param prio   Task priority.
         * @param slice  Timeslice in ticks (0 to use system default).
         * @return 0 on success.
         */
        static int create_with_stack(const hrt_task_fn fn, void* arg, uint32_t* stack, const size_t words,
                                     const hrt_prio_t prio, const uint16_t slice = 0) {
            const hrt_task_attr_t a{ prio, slice };
            return hrt_create_task(fn, arg, stack, words, &a);
        }

        /**
         * @brief Put the current task to sleep for a specified duration.
         * @param ms Duration in milliseconds.
         */
        static void sleep(uint32_t ms) {
            hrt_sleep(ms);
        }

        /**
         * @brief Yield the CPU to another task of the same or higher priority.
         */
        static void yield() {
            hrt_yield();
        }
    };

    /* UniqueTask is kept for future reference but currently not recommended for use.
    template <size_t StackWords = 1024>
    class UniqueTask {
    public:
        static int create(const hrt_task_fn fn, void* arg, const hrt_prio_t prio, const uint16_t slice = 0) {
            return Task::create<StackWords, __COUNTER__>(fn, arg, prio, slice);
        }
    };
    */

    /**
     * @brief Global system management and information.
     *
     * Wraps core RTOS functions for initialization, starting the scheduler,
     * and retrieving system-wide metadata like version and port info.
     */
    class System {
    public:
        /**
         * @brief Initialize the RTOS kernel.
         *
         * Must be called exactly once before any other RTOS function.
         * @param cfg Configuration structure (tick rate, scheduling policy, etc.).
         * @return 0 on success, or a negative error code on failure.
         */
        static int init(const hrt_config_t& cfg) {
            return hrt_init(&cfg);
        }

        /**
         * @brief Start the RTOS scheduler.
         *
         * This function normally never returns. It transfers control to the highest
         * priority task created.
         */
        static void start() {
            hrt_start();
        }

        /**
         * @brief Get the elapsed system ticks since boot.
         * @return Current tick count.
         */
        static uint32_t tick_now() {
            return hrt_tick_now();
        }

        /**
         * @brief Get the RTOS version as a human-readable string.
         * @return Version string (e.g., "0.3.0").
         */
        static const char* version_string() {
            return hrt_version_string();
        }

        /**
         * @brief Get the RTOS version as a packed 32-bit integer.
         * @return Packed version (0xMMmmPP).
         */
        static uint32_t version_u32() {
            return hrt_version_u32();
        }

        /**
         * @brief Get the descriptive name of the active port.
         * @return Port name string (e.g., "posix", "cortex_m").
         */
        static const char* port_name() {
            return hrt_port_name();
        }

        /**
         * @brief Get the unique identifier of the active port.
         * @return Port ID integer.
         */
        static int port_id() {
            return hrt_port_id();
        }
    };

    /**
     * @brief Object-oriented wrapper for binary semaphores.
     *
     * Used for task synchronization and resource protection.
     */
    class Semaphore {
    public:
        /**
         * @brief Initialize a binary semaphore.
         * @param init Initial state: 1 (available/given), 0 (unavailable/taken).
         */
        explicit Semaphore(unsigned init = 0) {
            hrt_sem_init(&_sem, init);
        }

        /**
         * @brief Take (wait for) the semaphore.
         *
         * Blocks the calling task if the semaphore is not available.
         * @return 0 on success.
         */
        int take() {
            return hrt_sem_take(&_sem);
        }

        /**
         * @brief Attempt to take the semaphore without blocking.
         * @return 0 if taken successfully, -1 if it was already unavailable.
         */
        int try_take() {
            return hrt_sem_try_take(&_sem);
        }

        /**
         * @brief Give (release) the semaphore.
         *
         * Wakes up the highest priority task waiting on this semaphore.
         * @return 0 on success.
         */
        int give() {
            return hrt_sem_give(&_sem);
        }

        /**
         * @brief Give (release) the semaphore from an Interrupt Service Routine (ISR).
         *
         * Special version to be used in hardware interrupts.
         * @param need_switch [out] Set to 1 if a context switch is required after the ISR.
         * @return 0 on success.
         */
        int give_from_isr(int& need_switch) {
            return hrt_sem_give_from_isr(&_sem, &need_switch);
        }

    private:
        hrt_sem_t _sem;
    };

    /**
     * @brief C++ wrapper for HardRT queues.
     *
     * HardRT queues are fixed-capacity and copy items into a user-provided
     * storage buffer (no malloc). This wrapper provides two options:
     *
     * - QueueRef<T>: bind to externally provided storage
     * - StaticQueue<T, Capacity>: owns a static buffer sized at compile-time
     */
    template <typename T>
    class QueueRef {
        static_assert(!std::is_void<T>::value, "QueueRef<T>: T cannot be void");

    public:
        QueueRef() = default;

        /**
         * @brief Initialize a queue using external storage.
         *
         * @param storage   Byte buffer large enough for (capacity * sizeof(T)).
         * @param capacity  Number of T elements.
         */
        void init(void* storage, uint16_t capacity) {
            hrt_queue_init(&_q, storage, capacity, sizeof(T));
        }

        int send(const T& item) {
            return hrt_queue_send(&_q, &item);
        }

        int try_send(const T& item) {
            return hrt_queue_try_send(&_q, &item);
        }

        int recv(T& out) {
            return hrt_queue_recv(&_q, &out);
        }

        int try_recv(T& out) {
            return hrt_queue_try_recv(&_q, &out);
        }

        int try_send_from_isr(const T& item, int& need_switch) {
            return hrt_queue_try_send_from_isr(&_q, &item, &need_switch);
        }

        int try_recv_from_isr(T& out, int& need_switch) {
            return hrt_queue_try_recv_from_isr(&_q, &out, &need_switch);
        }

        hrt_queue_t* native_handle() { return &_q; }

    private:
        hrt_queue_t _q{};
    };

    template <typename T, size_t Capacity>
    class StaticQueue {
        static_assert(Capacity > 0, "StaticQueue<T, Capacity>: Capacity must be > 0");

    public:
        StaticQueue() {
            // The storage is byte-addressed, but aligned for T.
            hrt_queue_init(&_q, _storage.data(), static_cast<uint16_t>(Capacity), sizeof(T));
        }

        int send(const T& item) {
            return hrt_queue_send(&_q, &item);
        }

        int try_send(const T& item) {
            return hrt_queue_try_send(&_q, &item);
        }

        int recv(T& out) {
            return hrt_queue_recv(&_q, &out);
        }

        int try_recv(T& out) {
            return hrt_queue_try_recv(&_q, &out);
        }

        int try_send_from_isr(const T& item, int& need_switch) {
            return hrt_queue_try_send_from_isr(&_q, &item, &need_switch);
        }

        int try_recv_from_isr(T& out, int& need_switch) {
            return hrt_queue_try_recv_from_isr(&_q, &out, &need_switch);
        }

        hrt_queue_t* native_handle() { return &_q; }

    private:
        alignas(T) std::array<std::byte, Capacity * sizeof(T)> _storage{};
        hrt_queue_t _q{};
    };

} // namespace hardrt
