AM_CFLAGS = -I$(top_srcdir)/src/lib/include -D_MCL_TEST_PATH=$(srcdir)

check_PROGRAMS = mcl_init mcl_discovery mcl_null mcl_exec mcl_err mcl_saxpy ocl_saxpy mcl_vadd ocl_vadd ocl_gemm mcl_gemm mcl_resdata mcl_fft ocl_fft mcl_tiled_gemm mcl_waitlist
TESTS =  mcl_init mcl_discovery mcl_null mcl_exec mcl_err mcl_saxpy mcl_vadd mcl_gemm mcl_resdata mcl_fft mcl_tiled_gemm mcl_waitlist

linker_flags = 

if OSX
AM_CFLAGS+=-DCL_SILENCE_DEPRECATION
else
LIBS += -lm -lpthread -lrt
endif


if APPLEOCL
linker_flags += -framework OpenCL
endif

mcl_init_SOURCES      = init.c utils.c utils.h
mcl_init_CFLAGS     = $(AM_CFLAGS) -D__TEST_MCL
mcl_init_LDFLAGS      = $(linker_flags) 
mcl_init_LDADD        = ../src/lib/libmcl.la

mcl_discovery_SOURCES = discovery.c utils.c utils.h
mcl_discovery_CFLAGS     = $(AM_CFLAGS) -D__TEST_MCL
mcl_discovery_LDFLAGS = $(linker_flags) 
mcl_discovery_LDADD   = ../src/lib/libmcl.la

mcl_null_SOURCES      = null.c utils.c utils.h
mcl_null_CFLAGS     = $(AM_CFLAGS) -D__TEST_MCL
mcl_null_LDFLAGS      = $(linker_flags) 
mcl_null_LDADD        = ../src/lib/libmcl.la

mcl_exec_SOURCES      = exec.c utils.c utils.h
mcl_exec_CFLAGS     = $(AM_CFLAGS) -D__TEST_MCL
mcl_exec_LDFLAGS      = $(linker_flags) 
mcl_exec_LDADD        = ../src/lib/libmcl.la

mcl_err_SOURCES      = errors.c utils.c utils.h
mcl_err_CFLAGS     = $(AM_CFLAGS) -D__TEST_MCL
mcl_err_LDFLAGS      = $(linker_flags) 
mcl_err_LDADD        = ../src/lib/libmcl.la

mcl_vadd_SOURCES    = array_add.c utils.c utils.h
mcl_vadd_CFLAGS     = $(AM_CFLAGS) -D__TEST_MCL
mcl_vadd_LDFLAGS    = $(linker_flags)
mcl_vadd_LDADD      = ../src/lib/libmcl.la

ocl_vadd_SOURCES    = array_add.c utils.c utils.h
ocl_vadd_CFLAGS     = $(AM_CFLAGS) -D__TEST_OCL
ocl_vadd_LDFLAGS    = $(linker_flags) -D__TEST_OMP
ocl_vadd_LDADD      = ../src/lib/libmcl.la

mcl_saxpy_SOURCES     = saxpy.c utils.c utils.h
mcl_saxpy_CFLAGS      = $(AM_CFLAGS) -D__TEST_MCL
mcl_saxpy_LDFLAGS     = $(linker_flags)
mcl_saxpy_LDADD       = ../src/lib/libmcl.la

ocl_saxpy_SOURCES     = saxpy.c utils.c utils.h
ocl_saxpy_CFLAGS      = $(AM_CFLAGS) -D__TEST_OCL
ocl_saxpy_LDFLAGS     = $(linker_flags)
ocl_saxpy_LDADD       = ../src/lib/libmcl.la

mcl_gemm_SOURCES     = gemm.c utils.c utils.h
mcl_gemm_CFLAGS      = $(AM_CFLAGS) -D__TEST_MCL
mcl_gemm_LDFLAGS     = $(linker_flags)
mcl_gemm_LDADD       = ../src/lib/libmcl.la

ocl_gemm_SOURCES     = gemm.c utils.c utils.h
ocl_gemm_CFLAGS      = $(AM_CFLAGS) -D__TEST_OCL
ocl_gemm_LDFLAGS     = $(linker_flags)
ocl_gemm_LDADD       = ../src/lib/libmcl.la

mcl_resdata_SOURCES     = resdata.c utils.c utils.h
mcl_resdata_CFLAGS      = $(AM_CFLAGS) -D__TEST_MCL
mcl_resdata_LDFLAGS     = $(linker_flags)
mcl_resdata_LDADD       = ../src/lib/libmcl.la

mcl_fft_SOURCES     = fft.c utils.c utils.h
mcl_fft_CFLAGS      = $(AM_CFLAGS) -D__TEST_MCL
mcl_fft_LDFLAGS     = $(linker_flags)
mcl_fft_LDADD       = ../src/lib/libmcl.la

ocl_fft_SOURCES     = fft.c utils.c utils.h
ocl_fft_CFLAGS      = $(AM_CFLAGS) -D__TEST_OCL
ocl_fft_LDFLAGS     = $(linker_flags)
ocl_fft_LDADD       = ../src/lib/libmcl.la

if SHARED_MEM
TESTS += mcl_shm_main
check_PROGRAMS += mcl_shm_main mcl_shm_producer mcl_shm_consumer
mcl_shm_main_SOURCES     = shm_main.c utils.c utils.h
mcl_shm_main_CFLAGS      = $(AM_CFLAGS) -D__TEST_MCL
mcl_shm_main_LDFLAGS     = $(linker_flags)
mcl_shm_main_LDADD       = ../src/lib/libmcl.la

mcl_shm_producer_SOURCES     = shm_producer.c utils.c utils.h
mcl_shm_producer_CFLAGS      = $(AM_CFLAGS) -D__TEST_MCL
mcl_shm_producer_LDFLAGS     = $(linker_flags)
mcl_shm_producer_LDADD       = ../src/lib/libmcl.la

mcl_shm_consumer_SOURCES     = shm_consumer.c utils.c utils.h
mcl_shm_consumer_CFLAGS      = $(AM_CFLAGS) -D__TEST_MCL
mcl_shm_consumer_LDFLAGS     = $(linker_flags)
mcl_shm_consumer_LDADD       = ../src/lib/libmcl.la
endif

mcl_tiled_gemm_SOURCES     = tiled_gemm.c utils.c utils.h
mcl_tiled_gemm_CFLAGS      = $(AM_CFLAGS) -D__TEST_MCL
mcl_tiled_gemm_LDFLAGS     = $(linker_flags)
mcl_tiled_gemm_LDADD       = ../src/lib/libmcl.la

mcl_waitlist_SOURCES       = waitlist.c utils.c utils.h
mcl_waitlist_CFLAGS        = $(AM_CFLAGS) -D__TEST_MCL
mcl_waitlist_LDFLAGS       = $(linker_flags)
mcl_waitlist_LDADD         = ../src/lib/libmcl.la
