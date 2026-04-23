#include "hardrt.h"
#include "hardrt_mutex.h"
#include <stdio.h>

static uint32_t sa[2048], sb[2048];
static hrt_mutex_t mutex;

static void A(void* arg){
    (void)arg;
    for(;;){
        puts("[A] waiting for mutex");
        if (hrt_mutex_lock(&mutex) == 0) {
            puts("[A] locked mutex");
            hrt_sleep(300);
            puts("[A] unlocking mutex");
            hrt_mutex_unlock(&mutex);
        }
        hrt_sleep(100);
    }
}

static void B(void* arg){
    (void)arg;
    for(;;){
        if (hrt_mutex_try_lock(&mutex) == 0){
            puts("[B] got mutex via try_lock");
            hrt_sleep(150);
            hrt_mutex_unlock(&mutex);
        } else {
            puts("[B] mutex busy, waiting");
        }
        hrt_sleep(200);
    }
}

int main(){
    const hrt_config_t cfg = { .tick_hz=1000, .policy=HRT_SCHED_PRIORITY_RR, .default_slice=5 };
    hrt_init(&cfg);
    hrt_mutex_init(&mutex);
    
    const hrt_task_attr_t p0={.priority=HRT_PRIO0,.timeslice=5};
    const hrt_task_attr_t p1={.priority=HRT_PRIO1,.timeslice=5};
    
    hrt_create_task(A,0,sa,2048,&p0);
    hrt_create_task(B,0,sb,2048,&p1);
    
    puts("Starting mutex basic example...");
    hrt_start();
    return 0;
}
