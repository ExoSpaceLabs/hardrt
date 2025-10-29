#include "heartos.h"
#include "heartos_sem.h"
#include <stdio.h>

static uint32_t sa[2048], sb[2048];
static hrt_sem_t sem;

static void A(void*){
    for(;;){
        hrt_sem_take(&sem);
        puts("[A] got sem");
        hrt_sleep(200);
        hrt_sem_give(&sem);
        hrt_sleep(200);
    }
}

static void B(void*){
    for(;;){
        if (hrt_sem_try_take(&sem)==0){
            puts("[B] got sem");
            hrt_sleep(100);
            hrt_sem_give(&sem);
        } else {
            puts("[B] waiting");
        }
        hrt_sleep(100);
    }
}

int main(){
    hrt_config_t cfg = { .tick_hz=1000, .policy=HRT_SCHED_PRIORITY_RR, .default_slice=5 };
    hrt_init(&cfg);
    hrt_sem_init(&sem, 1);
    hrt_task_attr_t p0={.priority=HRT_PRIO0,.timeslice=0}, p1={.priority=HRT_PRIO1,.timeslice=5};
    hrt_create_task(A,0,sa,2048,&p0);
    hrt_create_task(B,0,sb,2048,&p1);
    hrt_start();
}
