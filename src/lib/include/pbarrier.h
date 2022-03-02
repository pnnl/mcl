#ifndef PBARRIER_H
#define PBARRIER_H

#include <unistd.h>

typedef struct pthread_barrier {
  pthread_mutex_t         mutex;
  pthread_cond_t          cond;
  volatile uint32_t       flag;
  size_t                  count;
  size_t                  num;
} pthread_barrier_t;

int pthread_barrier_init(pthread_barrier_t *bar, void* attr, int num);
int pthread_barrier_wait(pthread_barrier_t *bar);

#endif
