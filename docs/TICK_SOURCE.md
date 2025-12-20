# Tick Source (external tick)

HardRT can use either a port‑owned periodic tick or an application‑provided (external) tick.

- `HRT_TICK_SYSTICK` (default): the active port starts its own timer and calls `hrt_tick_from_isr()` every tick.
- `HRT_TICK_EXTERNAL`: your application owns a hardware timer and must call `hrt_tick_from_isr()` from its timer ISR on every tick.

How to select it at init:
```c
#include "hardrt.h"

int main(void){
    hrt_config_t cfg = {0};
    cfg.tick_hz  = 1000;                // system tick frequency (Hz)
    cfg.tick_src = HRT_TICK_EXTERNAL;   // app provides the tick
    hrt_init(&cfg);
    hrt_start();
}
```

From your timer ISR (external mode):
```c
#include "hardrt_time.h"

void MyTimer_IRQHandler(void){
    // ... clear timer IRQ flag ...
    hrt_tick_from_isr();
}
```

Notes
- `hrt_tick_from_isr()` advances time and wakes sleepers; request a context switch using the port’s mechanism if needed.
- Some ports may also use `cfg.core_hz` to program their own timer when in `HRT_TICK_SYSTICK` mode.
