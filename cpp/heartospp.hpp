#pragma once
#include "heartos.h"

namespace heartos {
    struct Task {
        static int create(hrt_task_fn fn, void* arg, uint32_t* stack, size_t words,
                          hrt_prio_t prio, uint16_t slice) {
            hrt_task_attr_t a{ prio, slice };
            return hrt_create_task(fn, arg, stack, words, &a);
        }
    };
} // namespace heartos
