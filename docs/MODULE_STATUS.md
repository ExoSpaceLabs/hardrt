# Module status

Current components and their status.

- `inc/hardrt.h` — public C API (tasks, scheduler policy, time, config) [link](../inc/hardrt.h).
- `inc/hardrt_sem.h` — semaphores, including counting mode and ISR-safe give [link](../inc/hardrt_sem.h).
- `inc/hardrt_mutex.h` — mutex API with owner tracking and direct handoff [link](../inc/hardrt_mutex.h).
- `inc/hardrt_queue.h` — fixed-size message queues with task and ISR try-operations [link](../inc/hardrt_queue.h).
- `inc/hardrt_time.h` — tick ISR contract for ports (`hrt_tick_from_isr()`) [link](../inc/hardrt_time.h).
- `cpp/hardrtpp.hpp` — C++17 object-oriented wrapper (implemented); see [docs/CPP.md](CPP.md).
- Generated headers (installed alongside public headers):
  - `hardrt_version.h` — project version (from CMake) [link](../inc/hardrt_version.h.in).
  - `hardrt_port.h` — selected port identity (from CMake) [link](../inc/hardrt_port.h.in).

## Status notes

- Priority enum provides 12 symbolic levels (`HRT_PRIO0..HRT_PRIO11`); effective range is `0..HARDRT_MAX_PRIO-1` per build config.
- Max task/priority sizing is controlled at configure time; see `docs/BUILD.md` for `HARDRT_CFG_MAX_TASKS` and `HARDRT_CFG_MAX_PRIO`.
- Mutexes are implemented as **non-recursive**, **task-context-only**, and **without priority inheritance** in the current release.
- Dedicated example applications for mutex in both C and C++ are still pending, but the public API and tests are present.

Current version: `0.3.1` (see `hrt_version_string()` and `hrt_version_u32()`).
