# HardRT Porting Guide

This document describes how to port **HardRT** to a new architecture or execution environment.
HardRT is intentionally split into a *portable core* and a *thin port layer*. The port layer is
responsible only for time, interrupts, and context switching.

If porting is performed on HardRT, **read this entire document before writing code**.

---

## 1. Design Philosophy

HardRT follows these rules:

- The **core scheduler is architecture-agnostic**
- The **port owns all preemption and context switching**
- ISRs must be **short, bounded, and defer work**
- The scheduler **never blocks inside an ISR**
- Context switches are **requested**, never forced

If the port violates these, it may appear to work but will fail under load.

---

## 2. Required Port Hooks

Every port must implement the following hooks.

### 2.1 Initialization

#### `void hrt_port_init(void);`

- **Called by**: `hrt_init()`
- **Context**: Thread
- **ISR-safe**: No
- **Purpose**:
  - Initialize port state
  - Prepare context switch mechanism
  - Set interrupt priorities if required

Must not enable periodic ticks.

---

### 2.2 Tick Start

#### `void hrt_port_start_tick(uint32_t tick_hz);`

- **Called by**: `hrt_start()`
- **Context**: Thread
- **ISR-safe**: No
- **Purpose**:
  - Start the system tick source
  - Configure interrupt priorities

Notes:
- In **external tick mode**, this function must still configure PendSV (or equivalent).
- Must enable global interrupts before returning.

---

### 2.3 Yield / Preemption

#### `void hrt_port_yield(void);`

- **Called by**: Scheduler and APIs like `hrt_sleep()`
- **Context**: Thread
- **ISR-safe**: No
- **Purpose**:
  - Request a context switch

This must **not** perform the context switch directly.
It must defer to the port’s preemption mechanism.

---

#### `void hrt_port_pend_sv(void);`

- **Called by**: Core scheduler
- **Context**: Thread or ISR
- **ISR-safe**: Yes
- **Purpose**:
  - Mark that a context switch is pending

Examples:
- Cortex-M: set `SCB->ICSR.PENDSVSET`
- POSIX: set a signal flag

Must be safe to call multiple times.

---

## 3. Tick Handling

### 3.1 Internal Tick

If the port owns the tick source:

- The tick ISR must call:
  ```c
  hrt__tick_isr();
  ```

- The ISR **must not schedule directly**
- The ISR may request a context switch via `hrt_port_pend_sv()`

---

### 3.2 External Tick

If using an external tick source:

- The ISR must call:
  ```c
  hrt_tick_from_isr();
  ```

- The port must ensure:
  - Correct interrupt priority
  - No reentrancy into the scheduler

---

## 4. Critical Sections

#### `void hrt_port_crit_enter(void);`
#### `void hrt_port_crit_exit(void);`

- **Called by**: Core scheduler
- **Context**: Thread
- **ISR-safe**: No

Requirements:
- Must be nestable
- Must block preemption
- Must not mask faults

Typical implementations:
- Cortex-M: BASEPRI
- POSIX: signal masking

---

## 5. Context Switching

HardRT assumes **cooperative saving** of callee-saved registers.

### Requirements

- Save and restore:
  - r4–r11 (or equivalent)
- Switch stack pointer correctly
- Resume in thread mode
- Ensure 8-byte stack alignment

### First Switch

On first context switch:
- No task context exists yet
- Scheduler returns initial task stack pointer
- Port must transition to task context cleanly

---

## 6. Task Return Semantics

If a task function returns:

- The task is considered **terminated**
- It is **not re-queued**
- No further guarantees are made

Current behavior:
- **Cortex-M**: task yields forever
- **POSIX**: context exits

**Recommendation**: tasks should never return.

---

## 7. POSIX Port Notes

The POSIX reference port:

- Uses `ucontext` (glibc/Linux only)
- Uses `SIGALRM` as tick source
- Masks the tick signal during scheduling
- Uses `sig_atomic_t` for ISR-to-thread flags

Limitations:
- Not portable to musl or macOS
- Intended for simulation and CI only

---

## 8. Validation Checklist

Before submitting a new port:

- [ ] Preemption works under load
- [ ] Tick ISR never schedules directly
- [ ] Nested critical sections work
- [ ] Tasks resume correctly after yield
- [ ] No stack corruption under stress
- [ ] All hooks documented

---

## 9. Reference Ports

Use these as templates:

- `port/posix/`
- `port/cortex_m/`

If the port deviates, document why.

---

**HardRT Porting Rule:**  
If it works only when interrupts are slow, it is broken.
