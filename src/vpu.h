
#ifndef _LIBTSC_DNA_CORE_VPU_H_
#define _LIBTSC_DNA_CORE_VPU_H_

#include <stdint.h>
#include "coroutine.h"
#include "queue.h"
#include "support.h"

// Type of VPU information,
//  It's a OS Thread here!!
typedef struct vpu {
    TSC_OS_THREAD_T os_thr;
    uint32_t id;
    bool initialized;
    uint32_t watchdog;
    uint32_t ticks;
    tsc_coroutine_t current;
	tsc_coroutine_t scheduler;
} vpu_t;

// Type of the VPU manager
typedef struct vpu_manager {
    vpu_t *vpu;
    queue_t *xt;
    uint32_t xt_index;
    uint32_t last_pid;
    tsc_coroutine_t main;
    queue_t coroutine_list;
#ifdef ENABLE_DAEDLOCK_DETECT
    pthread_cond_t cond;
    pthread_mutex_t lock;
    uint16_t alive;
    uint16_t idle;
    queue_t wait_list;
#endif
} vpu_manager_t;

extern vpu_manager_t vpu_manager;

extern int core_wait (void *);
extern int core_yield (void *);
extern int core_exit (void *);

extern void tsc_vpu_initialize (int cpu_mp_count, tsc_coroutine_handler_t entry);

extern void vpu_suspend (queue_t * queue, void * lock, unlock_hander_t handler); 
extern void vpu_ready (tsc_coroutine_t coroutine);
extern void vpu_syscall (int (*pfn)(void *));
extern void vpu_clock_handler (int);
extern void vpu_wakeup_one (void);
#ifdef ENABLE_DAEDLOCK_DETECT
extern void vpu_backtrace (int);
#endif

#define TSC_ALLOC_TID() TSC_ATOMIC_INC(vpu_manager.last_pid)

#endif // _LIBTSC_DNA_CORE_VPU_H_