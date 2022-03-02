AM_CFLAGS=-I$(srcdir)/include -I$(top_srcdir)/src/common/include

lib_LTLIBRARIES   = libmcl.la
libmcl_la_SOURCES = api.c core.c reqs.c program.c rdata.c ../common/msg.c ../common/hash.c \
	../common/discovery.c ../common/lookup3.c ../common/ptrhash.c
libmcl_la_SOURCES += include/minos.h include/minos_internal.h ../common/include/debug.h \
	../common/include/atomics.h ../common/include/stats.h ../common/include/ptrhash.h
if OSX
AM_CFLAGS+=-DCL_SILENCE_DEPRECATION
libmcl_la_SOURCES += pbarrier.c include/pbarrier.h
endif
include_HEADERS   = $(top_srcdir)/src/lib/include/minos.h
