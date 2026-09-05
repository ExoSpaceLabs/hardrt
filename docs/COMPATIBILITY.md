# Compatibility and versioning policy

HardRT follows semantic-versioning conventions for release numbering, with explicit pre-1.0 limits on compatibility guarantees.

## Pre-1.0 policy

HardRT releases before 1.0.0 may change public source behavior and concrete public object layouts when a minor release introduces or corrects a documented contract. Those changes must be recorded in release and migration notes.

The project distinguishes four compatibility categories:

- **Source/API compatibility**: existing source continues to compile against the new public headers without changes.
- **Behavioral compatibility**: existing calls retain the same documented runtime semantics.
- **ABI compatibility**: already compiled objects remain binary-compatible with a newer HardRT library.
- **Package compatibility**: supported CMake package and target names remain consumable in the documented way.

These categories are not interchangeable. A source-compatible change can still alter a concrete C structure and therefore break ABI compatibility.

## Patch releases

For `0.x.Y` patch releases, HardRT should not intentionally break documented source/API or behavioral contracts. Fixes may still expose applications that depended on undocumented behavior. Public concrete structure layouts should not be changed in a patch release unless required to correct a serious defect and the release notes explicitly call out the resulting ABI impact.

## Minor releases

For `0.X.0` minor releases, HardRT may make intentional source, behavioral, or public-layout changes when needed to improve the pre-1.0 API. Such changes require migration guidance in the release notes.

ABI compatibility is **not guaranteed across pre-1.0 minor releases**. Public synchronization objects are concrete C structures, so applications should rebuild their code and HardRT together when moving between minor release lines unless a particular release explicitly provides stronger ABI evidence.

## Stable package surface

The installed CMake package name and canonical targets are intended to remain stable:

```cmake
find_package(HardRT REQUIRED)
target_link_libraries(app PRIVATE HardRT::hardrt)
```

When C++ wrappers are enabled, `HardRT::hardrtpp` is the canonical C++ target. If these package names ever change, a documented migration path is required.

Kernel/port-private headers are not part of the compatibility surface and are not installed.

## v0.5.0 compatibility boundary

v0.5.0 is a pre-1.0 minor release and intentionally changes behavior relative to v0.4.0. Important migration points include:

- `HRT_SCHED_RR` is true global, priority-independent round-robin.
- `hrt_sleep(0)` is an immediate scheduling point rather than a one-tick sleep.
- ISR wake `need_switch` is scheduler-aware rather than merely reporting that a waiter was awakened.
- lifecycle/configuration validation is explicit and returns public status codes.
- RUNNING, READY, EXITED, and TCB-slot ownership are distinct kernel concepts; EXITED slots can be reclaimed safely.
- live task-stack overlap is rejected.
- event flags and per-task notifications add new public synchronization state and extend private TCB storage.
- kernel/port implementation headers are outside the installed public API.

No ABI-compatibility claim is made between v0.4.0 and v0.5.0. Consumers should rebuild against the v0.5.0 headers and library together.

## 1.0 intent

The 1.0 release will define a stronger stable public API boundary. Any ABI guarantee for concrete public C structures will be stated separately and supported by suitable evidence rather than inferred from the version number alone.
