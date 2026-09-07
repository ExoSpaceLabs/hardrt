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
find_package(HardRT 0.5 REQUIRED)
target_link_libraries(app PRIVATE HardRT::hardrt)
```

When C++ wrappers are enabled, `HardRT::hardrtpp` is the canonical C++ target. If these package names ever change, a documented migration path is required.

For pre-1.0 releases, HardRT's generated CMake version file uses `SameMinorVersion`. This intentionally prevents a new minor release from silently satisfying a dependency written for an older minor line whose source or behavior may have changed. Patch releases within the same minor line may satisfy the requested package version according to CMake's normal version rules.

Package resolution is therefore deliberately narrower than the stability of the canonical target names. A stable target name does not imply source, behavioral, or ABI compatibility across `0.X` minor lines.

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
- CMake package version matching is restricted to the `0.5.x` minor line; a consumer requiring `0.4.x` must migrate explicitly rather than accepting v0.5.0 implicitly.

No ABI-compatibility claim is made between v0.4.0 and v0.5.0. Consumers should rebuild against the v0.5.0 headers and library together.

## v0.5.1 corrective patch boundary

v0.5.1 corrects the hosted POSIX execution backend that was intended for v0.5.0 but did not ship in that release. The published v0.5.0 tag remains immutable.

The correction is intentionally below the public C/C++ API boundary:

- public function signatures and public object layouts are unchanged from v0.5.0;
- scheduler policy, READY ordering, blocking semantics, wake decisions, and task lifecycle remain owned by the common core;
- the POSIX backend replaces `ucontext` execution with one pthread per application task plus a hosted timer thread;
- CPU-bound hosted tasks can now be asynchronously parked so a scheduler-selected task can run without requiring the interrupted task to call a HardRT API first;
- the application-provided HardRT task-stack buffer remains part of the public task-creation/lifetime contract, but on POSIX it is not the native pthread execution stack. The host pthread stack is separately allocated by the pthread implementation and is configured to at least the port's host minimum;
- POSIX builds now export the platform thread dependency through `HardRT::hardrt`; installed CMake consumers do not need to add `Threads::Threads` themselves.

Accordingly, v0.5.1 is intended to remain source/API and ABI compatible with v0.5.0 at the HardRT public interface. The hosted POSIX runtime behavior is deliberately corrected: code that relied on a CPU-bound task preventing scheduler progress was relying on a defect, not a supported contract.

### Hosted POSIX process-level constraints

The hosted backend is a Linux functional/scheduler validation port, not a hard-real-time target. It currently reserves process-wide `SIGALRM` for task preemption and `SIGUSR2` for task/timer wake-resume handling. Applications embedding the POSIX port must not independently install incompatible handlers or use those signals for unrelated process-level protocols while HardRT is active.

The pthread backend also consumes host resources proportional to configured live tasks: one pthread per live application task, plus the scheduler/controller thread and, for an internal tick source, one timer pthread. These host resources are implementation details and are not part of the Cortex-M memory model.

## 1.0 intent

The 1.0 release will define a stronger stable public API boundary. Any ABI guarantee for concrete public C structures will be stated separately and supported by suitable evidence rather than inferred from the version number alone.
