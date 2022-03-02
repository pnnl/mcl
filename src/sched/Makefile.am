AM_CFLAGS=-I$(srcdir)/include -I$(top_srcdir)/src/common/include -I$(top_srcdir)/src/lib/include

bin_PROGRAMS      = mcl_sched
mcl_sched_SOURCES = scheduler.c list.c sched_fifo.c sched_fffs.c sched_rdata.c
mcl_sched_SOURCES += sched_respol/first_fit.c sched_respol/round_robin.c sched_respol/delay_sched.c sched_respol/hybrid.c
mcl_sched_SOURCES += ../lib/include/minos.h ../lib/include/minos_internal.h include/minos_sched.h \
	../common/include/debug.h ../common/include/atomics.h ../common/include/stats.h \
	../common/include/utlist.h ../common/include/tracer.h

mcl_sched_LDFLAGS = 

if OSX
AM_CFLAGS+=-DCL_SILENCE_DEPRECATION
else
mcl_sched_LDFLAGS += -lrt -pthread -lm
endif 

if APPLEOCL
mcl_sched_LDFLAGS += -framework OpenCL
endif

mcl_sched_LDADD   = ../common/msg.o ../common/hash.o ../common/discovery.o \
	../common/lookup3.o ../common/ptrhash.o
