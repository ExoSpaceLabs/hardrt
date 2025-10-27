# C++ wrapper (optional)

HeaRTOS provides an optional, header-only C++17 convenience layer over the C API.

## Enabling
Enable the C++ wrapper target at configure time:
```bash
cmake -DHEARTOS_ENABLE_CPP=ON ..
```
This exposes the CMake target `HeaRTOS::heartospp`, which depends on the C core.

## Usage
Include the header and use the `heartos::Task` helper to create tasks with strong types:
```cpp
#include <heartospp.hpp>

void my_task(void*){}

int create_task_cpp(){
    static uint32_t stack[256];
    return heartos::Task::create(my_task, nullptr, stack, 256, HRT_PRIO1, /*slice*/5);
}
```

- The wrapper forwards directly to the C API and does not add runtime overhead.
- Priorities and timeslices map to `hrt_prio_t` and ticks respectively.
