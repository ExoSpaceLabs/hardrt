Added
- Message Queues: fixed-size, copy-based FIFO with blocking, non-blocking and ISR support.
- Comprehensive test suite for queues and idle behavior.
- C++ wrapper API (hardrtpp.hpp) providing idiomatic C++ access to HardRT
- C++ example applications demonstrating mixed C/C++ usage

Notes
- Core HardRT kernel remains pure C
- C++ support is optional and zero-cost when unused
- ABI/API compatibility with v0.3.0 is preserved