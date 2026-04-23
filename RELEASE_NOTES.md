Added
- Message queues: fixed-size, copy-based FIFO with blocking, non-blocking and ISR support
- Mutexes: owner-tracked, non-recursive mutual exclusion with FIFO waiter queue and direct handoff on unlock
- Mutex examples: new C and C++ examples demonstrating mutual exclusion in `examples/mutex_basic[_cpp]`
- POSIX example runner: `scripts/run-all-examples.sh` for automated verification of all POSIX-compatible examples
- Task deletion API: `hrt_task_delete()` for explicit task removal; also automatically called when a task returns
- Time helper API: `hrt_now_ms()` for easy millisecond-based time tracking
- Comprehensive POSIX tests for queues, mutexes, and new time/task APIs
- C++ wrapper API (`hardrtpp.hpp`) providing idiomatic C++ access to HardRT primitives
- C++ example applications demonstrating mixed C/C++ usage

Notes
- Core HardRT kernel remains pure C
- C++ support is optional and zero-cost when unused
- Mutexes are task-context only and currently do not implement priority inheritance
- ABI/API compatibility with v0.3.0 is preserved
- Validated on Ubuntu 22.04 / 24.04 / latest / stm32h755zi-q, failure detected on debian 13 (GCC 14 + glibc 2.41).
