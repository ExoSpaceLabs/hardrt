# Module status

Current components and their status.

- `inc/heartos.h` — public C API (tasks, scheduler policy, time, config) [link](../inc/heartos.h).
- `inc/heartos_sem.h` — binary semaphores (implemented); see docs/SEMAPHORES.md and tests [link](../inc/heartos_sem.h).
- `inc/heartos_time.h` — tick ISR contract for ports (`hrt__tick_isr()`) [link](../inc/heartos_time.h).
- Generated headers (installed alongside public headers):
  - `heartos_version.h` — project version (from CMake) [link](../inc/heartos_version.h.in).
  - `heartos_port.h` — selected port identity (from CMake)[link](../inc/heartos_port.h.in).

Notes
- Priority enum provides 12 levels (`HRT_PRIO0..HRT_PRIO11`); effective range is `0..HEARTOS_MAX_PRIO-1` per build config.
- Max task/priority sizing is controlled at configure time; see docs/BUILD.md for `HEARTOS_CFG_MAX_TASKS` and `HEARTOS_CFG_MAX_PRIO`.

Current version: `0.2.0` (see `hrt_version_string()` and `hrt_version_u32()`).
