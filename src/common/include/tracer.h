#ifndef MCL_TRACE_H
#define MCL_TRACE_H

#include <time.h>

#include <atomics.h>

#ifdef _TRACE
#define trace_timestamp(t) clock_gettime(CLOCK_MONOTONIC, &t);
#define TRprintf(fmt, args...) fprintf(stderr,"[MCL TRACE] [%s:%d] " fmt "\n", __FILE__, __LINE__, ##args)
#else

#define trace_timestamp(t)
#define TRprintf(fmt, args...)
#endif

#endif
