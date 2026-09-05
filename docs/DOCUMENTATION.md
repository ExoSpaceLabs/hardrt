# API documentation and CI drift gate

HardRT documents its public C API in `inc/`, its optional C++17 wrappers in `cpp/`, and behavioral/qualification guidance under `docs/`.

## Public documentation surface

Keep these synchronized with implementation/tests:

- lifecycle/task/scheduler/time API;
- semaphores, mutexes, and queues;
- event flags and task notifications;
- port and tick-source contracts;
- C++ wrappers;
- build/install/package usage;
- compatibility/release policy;
- hardware qualification/timing interpretation.

Private helpers under `src/internal/` and `.c` implementation details are not installed public API.

## Comment style

Use `/** ... */` for Doxygen blocks on public declarations and add `@brief`, `@param`, `@return`, and `@note` where they materially clarify behavior.

## Documentation CI

`.github/workflows/ci_docs.yml` is the release-facing documentation gate. It:

1. configures/builds HardRT 0.5.0 with POSIX + C++ wrappers so generated public headers exist;
2. compiles `tests/docs/api_c_smoke.c` as strict C11;
3. compiles `tests/docs/api_cpp_smoke.cpp` as strict C++17;
4. runs `scripts/check-docs.py` to validate repository-local Markdown links, required command/target paths, removed-script references, and known stale version/feature wording;
5. generates Doxygen from `inc/` and `cpp/` with warnings treated as errors.

A public API rename, missing documented path, stale removed hardware command, broken local link, invalid documentation-originated C/C++ usage, or broken Doxygen reference should therefore fail CI.

Run the repository-local portion manually with:

```bash
python3 scripts/check-docs.py
```

The compile/Doxygen steps are defined in the Documentation workflow so they run in the same known Ubuntu toolchain used for the release gate.

## Doxygen input

The generated API documentation uses:

```text
INPUT = inc/ cpp/
RECURSIVE = YES
EXTRACT_ALL = NO
GENERATE_HTML = YES
GENERATE_LATEX = NO
WARN_AS_ERROR = YES
```

Tests/examples are deliberately outside generated API docs; they are compile/runtime evidence instead.

## Related references

- [C API](API_C.md)
- [C++ wrapper](CPP.md)
- [Events and notifications](EVENTS_NOTIFICATIONS.md)
- [Build/install](BUILD.md)
- [Compatibility policy](COMPATIBILITY.md)
