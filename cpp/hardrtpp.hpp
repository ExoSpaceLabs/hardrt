#pragma once
#include "hardrt.h"
#include "hardrt_sem.h"
#include "hardrt_queue.h"
#include "hardrt_mutex.h"

#include <array>
#include <cstddef>
#include <limits>
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
         * @param slice  Timeslice in ticks; zero creates a cooperative task.
         * @return Non-negative task ID on success, or a negative error code.
         * @note Because an explicit attribute object is passed to the C API, a
         *       zero slice does not request the system default.
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
         * @param slice  Timeslice in ticks; zero creates a cooperative task.
         * @return Non-negative task ID on success, or a negative error code.
         */
        static int create_with_stack(const hrt_task_fn fn, void* arg, uint32_t* stack, const size_t words,
                                     const hrt_prio_t prio, const uint16_t slice = 0) {
            const hrt_task_attr_t a{ prio, slice };
            return hrt_create_task(fn, arg, stack, words, &a);
        }

        /**
         * @brief Put the current task to sleep for a specified duration.
         * @param ms Duration in milliseconds. In v0.4.0, zero sleeps for one tick.
         */
        static void sleep(uint32_t ms) {
            hrt_sleep(ms);
        }

        /**
         * @brief Yield the CPU, moving the current READY task to the tail of its
         * priority queue and refreshing its configured slice.
         */
        static void yield() {
            hrt_yield();
        }

        /**
         * @brief Permanently remove the current task from the scheduler.
         */
        static void delete_current() {
            hrt_task_delete();
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
         * Applications should call this once before creating tasks or starting
         * the scheduler. The v0.4.0 C implementation returns zero and does not
         * validate repeated initialization or lifecycle ordering.
         * @param cfg Configuration structure (tick rate, scheduling policy, etc.).
         * @return The value returned by hrt_init().
         */
        static int init(const hrt_config_t& cfg) {
            return hrt_init(&cfg);
        }

        /**
         * @brief Start the RTOS scheduler.
         *
         * This function normally does not return on Cortex-M. The null port
         * returns without running tasks.
         */
        static void start() {
            hrt_start();
        }

        /**
         * @brief Get the elapsed system ticks since initialization.
         * @return Current tick count.
         */
        static uint32_t tick_now() {
            return hrt_tick_now();
        }

        /**
         * @brief Get the elapsed system time in milliseconds.
         * @return Milliseconds derived from the current tick and tick rate.
         */
        static uint32_t now_ms() {
            return hrt_now_ms();
        }

        /**
         * @brief Get the RTOS version as a human-readable string.
         * @return Version string (e.g., "0.4.0").
         */
        static const char* version_string() {
            return hrt_version_string();
        }

        /**
         * @brief Get the RTOS version as a packed integer.
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
     * @brief Object-oriented wrapper for binary or counting semaphores.
     */
    class Semaphore {
    public:
        /**
         * @brief Initialize a semaphore. Binary by default, counting if max_count > 1.
         * @param init Initial token count, clamped by the C implementation.
         * @param max_count Maximum token count.
         */
        explicit Semaphore(unsigned init = 0, uint8_t max_count = 1) {
            if (max_count <= 1) {
                // Preserve strict binary semantics
                hrt_sem_init(&_sem, init);
            } else {
                hrt_sem_init_counting(&_sem, init, max_count);
            }
        }

        /**
         * @brief Take the semaphore, blocking if no token is available.
         * @return 0 after the semaphore has been acquired.
         */
        int take() {
            return hrt_sem_take(&_sem);
        }

        /**
         * @brief Attempt to take the semaphore without blocking.
         * @return 0 on success, -1 when unavailable.
         */
        int try_take() {
            return hrt_sem_try_take(&_sem);
        }

        /**
         * @brief Give the semaphore.
         * @return 0.
         * @note The current implementation yields when it wakes a waiter.
         */
        int give() {
            return hrt_sem_give(&_sem);
        }

        /**
         * @brief Give the semaphore from an ISR.
         * @param need_switch Set to 1 when any waiter is awakened. In v0.4.0
         *        this is not a higher-priority comparison.
         * @return 0.
         */
        int give_from_isr(int& need_switch) {
            return hrt_sem_give_from_isr(&_sem, &need_switch);
        }

    private:
        hrt_sem_t _sem;
    };

    /**
     * @brief C++ wrappers for fixed-capacity, copy-based HardRT queues.
     *
     * - QueueRef<T>: binds to externally provided storage.
     * - StaticQueue<T, Capacity>: owns storage sized at compile time.
     *
     * The C implementation copies T with memcpy. Queue wrappers therefore
     * require trivially-copyable payload types, and StaticQueue additionally
     * enforces the C API's uint16_t capacity range at compile time.
     */
    template <typename T>
    class QueueRef {
        static_assert(!std::is_void<T>::value, "QueueRef<T>: T cannot be void");
        static_assert(std::is_trivially_copyable<T>::value,
                      "QueueRef<T>: T must be trivially copyable because HardRT queues use memcpy");

    public:
        QueueRef() = default;

        /**
         * @brief Initialize a queue using external storage.
         * @param storage Byte buffer large enough for capacity * sizeof(T).
         * @param capacity Number of T elements.
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
        static_assert(std::is_trivially_copyable<T>::value,
                      "StaticQueue<T, Capacity>: T must be trivially copyable because HardRT queues use memcpy");
        static_assert(Capacity > 0, "StaticQueue<T, Capacity>: Capacity must be > 0");
        static_assert(Capacity <= static_cast<size_t>(std::numeric_limits<uint16_t>::max()),
                      "StaticQueue<T, Capacity>: Capacity exceeds the uint16_t C queue capacity contract");

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

    /**
     * @brief C++ wrapper for the current owner-tracked, non-recursive mutex.
     */
    class Mutex {
    public:
        Mutex() {
            hrt_mutex_init(&_m);
        }

        int lock() {
            return hrt_mutex_lock(&_m);
        }

        int try_lock() {
            return hrt_mutex_try_lock(&_m);
        }

        int unlock() {
            return hrt_mutex_unlock(&_m);
        }

    private:
        hrt_mutex_t _m;
    };

} // namespace hardrt
