# Module status

Current components and their status.

- `inc/hardrt.h` — public C API (tasks, scheduler policy, time, config) [link](../inc/hardrt.h).
- `inc/hardrt_sem.h` — binary semaphores (implemented); see docs/SEMAPHORES.md and tests [link](../inc/hardrt_sem.h).
- `inc/hardrt_time.h` — tick ISR contract for ports (`hrt_tick_from_isr()`) [link](../inc/hardrt_time.h).
- `cpp/hardrtpp.hpp` — C++17 object-oriented wrapper (implemented); see [docs/CPP.md](CPP.md).
- Generated headers (installed alongside public headers):
  - `hardrt_version.h` — project version (from CMake) [link](../inc/hardrt_version.h.in).
  - `hardrt_port.h` — selected port identity (from CMake)[link](../inc/hardrt_port.h.in).

Notes
- Priority enum provides 12 levels (`HRT_PRIO0..HRT_PRIO11`); effective range is `0..HARDRT_MAX_PRIO-1` per build config.
- Max task/priority sizing is controlled at configured time; see docs/BUILD.md for `HARDRT_CFG_MAX_TASKS` and `HARDRT_CFG_MAX_PRIO`.

Current version: `0.3.0` (see `hrt_version_string()` and `hrt_version_u32()`).
