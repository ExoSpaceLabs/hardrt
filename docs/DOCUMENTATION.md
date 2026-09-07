# API documentation and CI drift gate

HardRT documents its public C API in `inc/`, its optional C++17 wrappers in `cpp/`, behavioral/qualification guidance under `docs/`, and example/validation procedures alongside the code they describe.

## Public documentation surface

Keep these synchronized with implementation/tests:

- lifecycle/task/scheduler/time API;
- semaphores, mutexes, and queues;
- event flags and task notifications;
- port and tick-source contracts;
- C++ wrappers;
- build/install/package usage;
- compatibility/release policy;
- example instructions;
- hardware qualification/timing interpretation.

Private helpers under `src/internal/` and `.c` implementation details are not installed public API.

## Comment style

Use `/** ... */` for Doxygen blocks on public declarations and add `@brief`, `@param`, `@return`, and `@note` where they materially clarify behavior.

## Documentation CI

`.github/workflows/ci_docs.yml` is the release-facing documentation gate. It:

1. configures/builds HardRT 0.5.1 with POSIX + C++ wrappers so generated public headers exist;
2. compiles `tests/docs/api_c_smoke.c` as strict C11;
3. compiles `tests/docs/api_cpp_smoke.cpp` as strict C++17;
4. runs `scripts/check-docs.py` across **all tracked repository Markdown** to validate local links, required command/target paths, removed-script references, release-matrix/evidence-policy invariants, and known stale version/feature wording;
5. generates Doxygen from `inc/` and `cpp/` with documentation errors treated as build failures.

A public API rename, missing documented path, stale removed hardware command, broken local link, invalid documentation-originated C/C++ usage, stale release qualification matrix, or broken Doxygen reference should therefore fail CI.

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
HAVE_DOT = NO
WARN_AS_ERROR = YES
WARN_IF_UNDOCUMENTED = NO
```

Doxygen is used to reject malformed/broken documentation, not to require a comment on every legacy field before 1.0. Tests/examples remain outside generated API docs; they are compile/runtime evidence instead.

## Related references

- [C API](API_C.md)
- [C++ wrapper](CPP.md)
- [Events and notifications](EVENTS_NOTIFICATIONS.md)
- [Build/install](BUILD.md)
- [Compatibility policy](COMPATIBILITY.md)
- [STM32 qualification](STM32_MANUAL_TESTS.md)
