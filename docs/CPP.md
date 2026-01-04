# C++ wrapper (optional)

HardRT provides an optional, header-only C++17 convenience layer over the C API. It is located in `cpp/hardrtpp.hpp`.

## Enabling
Enable the C++ wrapper target at configuration time:
```bash
cmake -DHARDRT_ENABLE_CPP=ON ..
```
This exposes the CMake target `hardrtpp` (aliased as `HardRT::hardrtpp`), which depends on the C core.

## Usage
The wrapper uses the `hardrt` namespace.

### Task Management
HardRT provides two ways to manage tasks in C++: using automatic stack management (recommended) or using static methods with manual stack management.

#### Automatic Stack Management (Recommended)
The `Task::create<Size, Tag>` method simplifies task creation by automatically allocating a unique static stack for each unique combination of `Size` and `Tag`.

```cpp
#include <hardrtpp.hpp>

void worker(void* arg) {
    for(;;) {
        hardrt::Task::sleep(100);
    }
}

int main() {
    // Initialize system...
    
    // Create and start the tasks with managed stacks.
    // Use different tags to ensure each task gets its own stack.
    hardrt::Task::create<1024, 0>(worker, nullptr, HRT_PRIO1);
    hardrt::Task::create<2048, 1>(worker, nullptr, HRT_PRIO0);
    
    // Start scheduler...
}
```

#### Manual Stack Management
If you prefer to manage the stack array yourself, use `hardrt::Task::create_with_stack`.

```cpp
static uint32_t my_stack[512];
hardrt::Task::create_with_stack(my_task, nullptr, my_stack, 512, HRT_PRIO1);
```

#### Task Control
The `hardrt::Task` class provides static methods for controlling the current task.

```cpp
hardrt::Task::sleep(500); // Sleep for 500ms
hardrt::Task::yield();    // Yield CPU
```

### System Management
The `hardrt::System` class provides global RTOS control.

```cpp
const hrt_config_t cfg = { ... };
hardrt::System::init(cfg);
hardrt::System::start(); // Never returns
```

### Semaphores
The `hardrt::Semaphore` class provides an object-oriented interface for binary semaphores.

```cpp
hardrt::Semaphore sem(1); // Initialize with 1 (available)

void worker(void*) {
    sem.take();
    // Critical section
    sem.give();
}
```

## Features
- **Zero Overhead**: Methods are inline and forward directly to the C API.
- **Strong Typing**: Uses RTOS types like `hrt_prio_t` while providing a cleaner C++ interface.
- **Convenience**: Internal stack management reduces boilerplate.
