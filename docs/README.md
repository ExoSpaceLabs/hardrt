# 📘 Documentation Summary

This documentation set is synchronized with HardRT v0.3.1.

## Main Docs
| File                                        | Purpose                                         |
|---------------------------------------------|-------------------------------------------------|
| Main [README.md](../README.md)              | Project overview, build instructions, diagrams  |
| [INTRODUCTION.md](INTRODUCTION.md)          | Background: What is an RTOS? Why HardRT?        |
| [BUILD.md](BUILD.md)                        | Build and install guide                         |
| [API_C.md](API_C.md)                        | Public C API reference                          |
| [CPP.md](CPP.md)                            | C++ header-only wrapper reference               |
| [PORTING.md](PORTING.md)                    | Porting guide for POSIX and Cortex‑M            |
| [SEMAPHORES.md](SEMAPHORES.md)              | Semaphore design and API (binary + counting)    |
| [MUTEXES.md](MUTEXES.md)                    | Mutex design and API                            |
| [QUEUES.md](QUEUES.md)                      | Queue design and API                            |
| [EXAMPLES_C.md](EXAMPLES_C.md)              | C and C++ example overview                      |
| [MODULE_STATUS.md](MODULE_STATUS.md)        | Current module status matrix                    |
| [TESTS_POSIX.md](TESTS_POSIX.md)            | POSIX test harness notes                        |
| [STATISTICS.md](STATISTICS.md)              | Statistics on STM32H755ZI-Q                     |
| [ROADMAP.md](ROADMAP.md)                    | Release roadmap to v1.0.0                       |
| [DOCUMENTATION.md](DOCUMENTATION.md)        | Doxygen and header documentation guidance       |

## Notes
- The mutex API is now part of the public documented surface.
- Counting semaphores, mutexes, and queues are implemented in the current codebase.
- Dedicated repository examples for mutex in both C and C++ are still pending; documentation snippets are provided meanwhile.
