#ifndef MCL_STATS_H
#define MCL_STATS_H

#include <time.h>

#include <atomics.h>

#ifdef _STATS
#define stats_timestamp(t) clock_gettime(CLOCK_MONOTONIC, &t);
#define stats_inc(v)       ainc(&v)
#define stats_dec(v)       adec(&v)
#define stat_tdiff(e,s) BILLION * (e.tv_sec - s.tv_sec) + e.tv_nsec - s.tv_nsec
#define stats_add(v, val)  faa(&(v), val)

#define stprintf(fmt, args...) fprintf(stderr,"[MCL STAT] " fmt "\n", ##args)
#else

#define stats_timestamp(t)
#define stats_inc(v)
#define stats_dec(v)
#define stats_max(c,n)
#define stat_tdiff(e,s)
#define stats_add(v, val)
#define stprintf(fmt, args...)
#endif

#endif
