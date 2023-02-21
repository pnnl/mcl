AM_CFLAGS=-I$(srcdir)/include -I$(abs_top_srcdir)/src/common/include $(POCL_CFLAGS) -I$(abs_top_srcdir)/src/common/nbhashmap -I$(abs_top_srcdir)/deps/uthash/include 

lib_LTLIBRARIES   = libmcl.la
libmcl_la_SOURCES = api.c core.c reqs.c program.c rdata.c kernel.c ../common/msg.c ../common/hash.c \
	../common/discovery.c ../common/lookup3.c ../common/ptrhash.c ../common/mem_list.c \
	../common/AvlTree.c ../common/nbhashmap/nbhashmap.c
libmcl_la_SOURCES += include/minos.h include/minos_internal.h ../common/include/debug.h \
	../common/include/atomics.h ../common/include/stats.h ../common/include/ptrhash.h  \
	../common/AvlTree.h ../common/nbhashmap/nbhashmap.h ../common/nbhashmap/debug.h

if SHARED_MEM
libmcl_la_SOURCES += shared_memory.c ../common/include/mem_list.h
endif

if OSX
AM_CFLAGS+=-DCL_SILENCE_DEPRECATION
libmcl_la_SOURCES += pbarrier.c include/pbarrier.h
endif

include_HEADERS   = $(abs_top_srcdir)/src/lib/include/minos.h
nodist_pkginclude_HEADERS = $(abs_top_srcdir)/src/lib/include/mcl/mcl_config.h
