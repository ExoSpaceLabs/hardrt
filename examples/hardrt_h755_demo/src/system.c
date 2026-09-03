// vendor/stm32/Templates/sysmem.c
// Minimal heap provider for newlib on GNU ld. Works with STM32 H7 linker scripts.
// SPDX-License-Identifier: Apache-2.0

#include <stddef.h>
#include <stdint.h>

// Common symbols exported by GCC STM32 linker scripts.
extern uint8_t _end;     // end of .bss (fallback heap base)
extern uint8_t _estack;  // stack top (down-growing)

// Optional symbols exported by some linker scripts. Keep them weak so this
// support file also links with scripts that provide only _end/_estack.
extern uint8_t __HeapBase __attribute__((weak));
extern uint8_t __HeapLimit __attribute__((weak));

void *_sbrk(ptrdiff_t incr)
{
    static uint8_t *heap;
    uint8_t *const heap_base = (&__HeapBase != 0) ? &__HeapBase : &_end;
    uint8_t *const heap_limit = (&__HeapLimit != 0) ? &__HeapLimit : 0;

    if (heap == 0) {
        heap = heap_base;
    }

    uint8_t *const prev = heap;
    uint8_t *const next = heap + incr;

    if (heap_limit != 0) {
        if (next > heap_limit) {
            return (void *)-1;
        }
    } else if (next + 256 > &_estack) {
        // Fallback guard when the linker script does not define a heap limit.
        return (void *)-1;
    }

    heap = next;
    return prev;
}
