#include <hardrt.h>
#include <hardrt_port.h>

#include <stdint.h>

static uint8_t queue_storage[4u * sizeof(uint32_t)];

int main(void) {
    hrt_config_t cfg = {
        .tick_hz = 1000u,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 5u,
        .core_hz = 0u,
        .tick_src = HRT_TICK_SYSTICK
    };
    hrt_task_attr_t attr = {
        .priority = HRT_PRIO1,
        .timeslice = 1u
    };
    hrt_sem_t sem;
    hrt_mutex_t mutex;
    hrt_queue_t queue;

    hrt_sem_init(&sem, 0u);
    hrt_mutex_init(&mutex);
    hrt_queue_init(&queue, queue_storage, 4u, sizeof(uint32_t));

    (void)cfg;
    (void)attr;
    (void)HARDRT_PORT_ID;
    (void)HARDRT_PORT_STRING;
    (void)hrt_version_string();
    (void)hrt_version_u32();
    (void)hrt_port_name();
    (void)hrt_port_id();
    return 0;
}
