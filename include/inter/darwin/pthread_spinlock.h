// Copyright 2016 Amal Cao (amalcaowei@gmail.com). All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE.txt file.

#ifndef _PTHREAD_SPINLOCK_H_
#define _PTHREAD_SPINLOCK_H_

#include <errno.h>
/**
enum {
        PTHREAD_PROCESS_PRIVATE,
        PTHREAD_PROCESS_SHARED,
};
*/
typedef int pthread_spinlock_t;

static int pthread_spin_init(pthread_spinlock_t *lock, int pshared) {
  __asm__ __volatile__("" :: : "memory");
  *lock = 0;
  return 0;
}

static int pthread_spin_destroy(pthread_spinlock_t *lock) { return 0; }

static int pthread_spin_lock(pthread_spinlock_t *lock) {
  while (1) {
    int i;
    for (i = 0; i < 10000; i++) {
      if (__sync_bool_compare_and_swap(lock, 0, 1)) {
        return 0;
      }
    }
    sched_yield();
  }
}

static int pthread_spin_trylock(pthread_spinlock_t *lock) {
  if (__sync_bool_compare_and_swap(lock, 0, 1)) {
    return 0;
  }
  return EBUSY;
}

static int pthread_spin_unlock(pthread_spinlock_t *lock) {
  __asm__ __volatile__("" :: : "memory");
  *lock = 0;
  return 0;
}
#endif  // _PTHREAD_SPINLOCK_H_
