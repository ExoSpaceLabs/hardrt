# HardRT Message Queues

HardRT provides a fixed-size message queue implementation for inter-task communication. These queues are based on a ring buffer and support both blocking and non-blocking operations, as well as use within Interrupt Service Routines (ISRs).

## Overview

A `hrt_queue_t` object manages a buffer of items of a fixed size.
- **Copy-based**: Items are copied into and out of the queue buffer using `memcpy`.
- **FIFO**: Items are delivered in the same order they were sent.
- **Blocking**: Tasks can block indefinitely when sending to a full queue or receiving from an empty one.
- **FIFO Waiters**: If multiple tasks are blocked on a queue, they are woken in the order they began waiting.

## Initialization

Queues use application-provided storage for their internal buffer.

```c
#include "hardrt_queue.h"

#define QUEUE_CAPACITY 10
#define ITEM_SIZE sizeof(my_msg_t)

hrt_queue_t my_queue;
uint8_t queue_storage[QUEUE_CAPACITY * ITEM_SIZE];

void init(void) {
    hrt_queue_init(&my_queue, queue_storage, QUEUE_CAPACITY, ITEM_SIZE);
}
```

## Operations

### Task Context (Blocking)

- `hrt_queue_send`: Sends an item. Blocks if the queue is full.
- `hrt_queue_recv`: Receives an item. Blocks if the queue is empty.

```c
my_msg_t msg = { .id = 1 };
hrt_queue_send(&my_queue, &msg); // Blocks until space available

my_msg_t received;
hrt_queue_recv(&my_queue, &received); // Blocks until item available
```

### Task Context (Non-blocking)

- `hrt_queue_try_send`: Attempts to send. Returns `0` on success, `-1` if full.
- `hrt_queue_try_recv`: Attempts to receive. Returns `0` on success, `-1` if empty.

### ISR Context

Queues support sending and receiving from ISRs using specialized functions. These functions never block and provide a `need_switch` flag to indicate if a higher-priority task was woken.

- `hrt_queue_try_send_from_isr`
- `hrt_queue_try_recv_from_isr`

```c
void MY_IRQ_Handler(void) {
    my_msg_t msg = { .id = 99 };
    int need_switch = 0;
    if (hrt_queue_try_send_from_isr(&my_queue, &msg, &need_switch) == 0) {
        if (need_switch) {
            hrt_port_yield_from_isr(); // Port-specific yield
        }
    }
}
```

## Implementation Details

### Data Structure

```c
typedef struct {
    uint8_t *buf;
    size_t   item_size;
    uint16_t capacity;

    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint16_t count;

    /* Internal waiter FIFOs for TX and RX */
    uint8_t rx_q[HARDRT_MAX_TASKS];
    uint8_t rx_head, rx_tail, rx_wait;

    uint8_t tx_q[HARDRT_MAX_TASKS];
    uint8_t tx_head, tx_tail, tx_wait;
} hrt_queue_t;
```

### Performance Considerations

Because HardRT queues copy data, they are best suited for:
1. Small data structures (integers, small structs).
2. Pointers to larger buffers (e.g., from a memory pool).
3. Indices into an array.

Large `item_size` will increase the time spent in critical sections during `memcpy`.

## Constraints

- **No Timeouts**: Blocking operations currently block forever.
- **Fixed Size**: Queue capacity and item size are fixed at initialization.
- **Memory**: Storage must be provided by the caller and must be large enough (`capacity * item_size`).
