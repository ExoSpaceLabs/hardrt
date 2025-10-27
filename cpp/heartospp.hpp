#pragma once
#include "heartos.h"

namespace heartos {
    /**
     * @brief C++ convenience wrapper around the C task creation API.
     * @note This header is optional; it forwards to the C API and does not add
     *       overhead. It’s intended for codebases that prefer a namespaced
     *       interface from C++.
     */
    struct Task {
        /**
         * @brief Create a task using C++ defaults and strong types.
         * @param fn Task entry function (same signature as C API).
         * @param arg User argument forwarded to the task.
         * @param stack Pointer to stack buffer (array of 32-bit words).
         * @param words Number of 32-bit words in the stack.
         * @param prio Task priority.
         * @param slice Round-robin time slice in ticks (0 = cooperative within class).
         * @return Task id (>=0) on success; negative on failure.
         * @code
         * using namespace heartos;
         * static uint32_t stackA[256];
         * void worker(void* arg) { // body }
         * int id = Task::create(worker, nullptr, stackA, 256, HRT_PRIO1, 5);
         * @endcode
         */
        static int create(hrt_task_fn fn, void* arg, uint32_t* stack, size_t words,
                          hrt_prio_t prio, uint16_t slice) {
            hrt_task_attr_t a{ prio, slice };
            return hrt_create_task(fn, arg, stack, words, &a);
        }
    };
} // namespace heartos
