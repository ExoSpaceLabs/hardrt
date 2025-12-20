# C++ wrapper (optional)
>Note: Not  implemented yet. will be included in later versions.

HardRT provides an optional, header-only C++17 convenience layer over the C API.

## Enabling
Enable the C++ wrapper target at configure time:
```bash
cmake -DHARDRT_ENABLE_CPP=ON ..
```
This exposes the CMake target `HardRT::hardrtpp`, which depends on the C core.

## Usage
Include the header and use the `hardrt::Task` helper to create tasks with strong types:
```cpp
#include <hardrtpp.hpp>

void my_task(void*){}

int create_task_cpp(){
    static uint32_t stack[256];
    return hardrt::Task::create(my_task, nullptr, stack, 256, HRT_PRIO1, /*slice*/5);
}
```

- The wrapper forwards directly to the C API and does not add runtime overhead.
- Priorities and timeslices map to `hrt_prio_t` and ticks respectively.
