Added
- Message queues: fixed-size, copy-based FIFO with blocking, non-blocking and ISR support
- Mutexes: owner-tracked, non-recursive mutual exclusion with FIFO waiter queue and direct handoff on unlock
- Comprehensive POSIX tests for queues and mutexes
- C++ wrapper API (`hardrtpp.hpp`) providing idiomatic C++ access to HardRT primitives
- C++ example applications demonstrating mixed C/C++ usage

Notes
- Core HardRT kernel remains pure C
- C++ support is optional and zero-cost when unused
- Mutexes are task-context only and currently do not implement priority inheritance
- ABI/API compatibility with v0.3.0 is preserved
