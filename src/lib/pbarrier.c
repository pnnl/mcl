#include <pthread.h>
#include <pbarrier.h>

#define PTHREAD_BARRIER_SERIAL_THREAD   1

int pthread_barrier_init(pthread_barrier_t *bar, void* attr, int num)
{
  int ret = 0;
  if ((ret = pthread_mutex_init(&(bar->mutex), 0))) return ret;
  if ((ret = pthread_cond_init(&(bar->cond), 0))) return ret;
  bar->flag = 0;
  bar->count = 0;
  bar->num = num;
  return 0;
}

int pthread_barrier_wait(pthread_barrier_t *bar)
{
  int ret = 0;
  uint32_t flag = 0;

  if ((ret = pthread_mutex_lock(&(bar->mutex)))) return ret;

  flag = bar->flag;
  bar->count++;

  if (bar->count == bar->num) {
    bar->count = 0;
    bar->flag = 1 - bar->flag;
    if ((ret = pthread_cond_broadcast(&(bar->cond)))) return ret;
    if ((ret = pthread_mutex_unlock(&(bar->mutex)))) return ret;
    return PTHREAD_BARRIER_SERIAL_THREAD;
  }

  while (1) {
    if (bar->flag == flag) {
      ret = pthread_cond_wait(&(bar->cond), &(bar->mutex));
      if (ret) return ret;
      } else { break; }
    }

  if ((ret = pthread_mutex_unlock(&(bar->mutex)))) return ret;
  return 0;
}
