AM_CFLAGS=-I$(srcdir)/include -I$(abs_top_srcdir)/src/common/include -I$(abs_top_srcdir)/src/lib/include -I$(abs_top_srcdir)/src/common/nbhashmap -I$(abs_top_srcdir)/deps/uthash/include

lib_LTLIBRARIES   = libmcl_sched.la

libmcl_sched_la_SOURCES = scheduler_internal.c list.c sched_fifo.c sched_fffs.c sched_rdata.c
libmcl_sched_la_SOURCES += sched_respol/first_fit.c sched_respol/round_robin.c sched_respol/delay_sched.c sched_respol/hybrid.c eviction_pol/lru.c \
	../common/msg.c ../common/hash.c ../common/discovery.c ../common/lookup3.c ../common/ptrhash.c ../common/mem_list.c
libmcl_sched_la_SOURCES += ../lib/include/minos.h ../lib/include/minos_internal.h include/minos_sched.h include/minos_sched_internal.h \
	../common/include/debug.h ../common/include/atomics.h ../common/include/stats.h \
	../common/include/ptrhash.h ../common/include/utlist.h ../common/include/tracer.h

bin_PROGRAMS       = mcl_sched
mcl_sched_SOURCES  = scheduler.c 
mcl_sched_SOURCES += include/minos_sched.h 

mcl_sched_LDFLAGS = 

if OSX
AM_CFLAGS+=-DCL_SILENCE_DEPRECATION
else
mcl_sched_LDFLAGS += -lrt -pthread -lm 
endif 

if APPLEOCL
mcl_sched_LDFLAGS += -framework OpenCL
endif


mcl_sched_LDADD   =  libmcl_sched.la -lrt -lm

include_HEADERS   = $(top_srcdir)/src/sched/include/minos_sched.h
