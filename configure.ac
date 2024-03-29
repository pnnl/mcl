AC_INIT([Minos Computing Library],[0.6],[roberto.gioiosa@pnnl.gov ],[mcl])
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects tar-pax])

AC_CONFIG_MACRO_DIRS([m4])
AM_PROG_AR

AC_CANONICAL_HOST

AC_PROG_CC
AM_PROG_CC_C_O
LT_INIT

build_linux=no
build_mac=no
build_freebsd=no
#enable_appleocl=no
#enable_stats=no
#enable_debug=no

case "${host_os}" in
    linux*)
        build_linux=yes
	platform="Linux"
        ;;
    freebsd*)
        build_freebsd=yes
	platform="FreeBSD"
	;;
    darwin*)
        build_mac=yes
	platform="OSX"
        ;;
    *)
        AC_MSG_ERROR(["OS $host_os is not supported"])
        ;;
esac

COMPILER_VERSION=`"$CC" --version 2>/dev/null`

if test x"$COMPILER_VERSION" = "x" ; then
   AC_MSG_ERROR("Unable to determine compiiler version!")
fi

IS_NVIDIA_COMPILER=`echo $COMPILER_VERSION | $EGREP -i -c -E 'NVIDIA'`
IS_GNU_COMPILER=`echo $COMPILER_VERSION | $EGREP -i -c -E 'Free Software Foundation'`

if test "$IS_NVIDIA_COMPILER" -ne "0"; then
   xcompiler="NVIDIA"
fi

if test "$IS_GNU_COMPILER" -ne "0"; then
   xcompiler="GNU"
fi

AC_ARG_ENABLE([debug],
    [AS_HELP_STRING([--enable-debug],[Enable debugging mode (default: disabled)])])

AC_ARG_ENABLE([stats],
    [AS_HELP_STRING([--enable-stats],[Enable statistics (default: disabled)])])

AC_ARG_ENABLE([trace],
    [AS_HELP_STRING([--enable-trace],[Enable tracing (default: disabled)])])

AC_ARG_ENABLE([opencl2],
    [AS_HELP_STRING([--enable-opencl2],[Enable OpenCL 2.x  (default disabled)])])

if test "$build_mac" = "yes" ; then
        enable_appleocl=yes
        enable_opencl2=no
        AC_ARG_ENABLE([appleocl],
                [AS_HELP_STRING([--enable-appleocl],[Use Apple OpenCL (default enabled)])])
fi

AC_ARG_ENABLE([shared-memory],
    [AC_HELP_STRING([--enable-shared-memory],
		    [Enable inter-process shared memory (default: disabled)])])

AC_ARG_ENABLE([pocl-extensions],
    [AC_HELP_STRING([--enable-pocl-extensions],
		    [Enable POCL extensions (default: disabled)])])

AC_CHECK_HEADERS([stdio.h],[],[AC_MSG_ERROR[stdio.h not found!]])
# AC_CHECK_HEADERS([uthash.h],[],[AC_MSG_ERROR[uthash.h not found!]])
# AC_CHECK_HEADERS([utlist.h],[],[AC_MSG_ERROR[utlist.h not found!]])


if test "$enable_appleocl" = "yes" ; then
  AC_CHECK_HEADERS([OpenCL/opencl.h],[found_opencl_h=yes])
else 
  AC_CHECK_HEADERS([CL/cl.h],[found_opencl_h=yes])
fi

if test "$enable_opencl2" = "yes" ; then
  AS_VAR_APPEND(CFLAGS, " -DOPENCL2")
fi

if test "$xcompiler" = "NVIDIA" ; then
  AS_VAR_APPEND(CFLAGS, " -Wno-deprecated-gpu-targets --compiler-options -Wall --compiler-options -Werror")
fi

if test "$xcompiler" = "GNU" ; then
  AS_VAR_APPEND(CFLAGS, " -Wall -Werror")
fi

if test "$enable_appleocl" != "yes" ; then
  AC_CHECK_LIB([OpenCL], clGetPlatformIDs, [],[AC_MSG_ERROR[OpenCL not found!]])
fi

if test "$found_opencl_h" = "yes" ; then
  AS_VAR_APPEND(CFLAGS, " -D_OPENCL")
fi

if test "$enable_debug" = "yes" ; then
  AS_VAR_APPEND(CFLAGS, " -O0 -D_DEBUG")
fi

if test "$enable_stats" = "yes" ; then
  AS_VAR_APPEND(CFLAGS, " -D_STATS=1")
fi

if test "$enable_trace" = "yes" ; then
  AS_VAR_APPEND(CFLAGS, " -D_TRACE=1")
fi

if test "$enable_shared_memory" = "yes" ; then
  AC_DEFINE([SHARED_MEM], [1], [Build shared memory.])
fi

if test "$enable_pocl_extensions" = "yes" ; then
  AC_DEFINE([USE_POCL_SHARED_MEM], [1], [Use OpenCL extensions provided by pocl.])
fi
AM_CONDITIONAL([SHARED_MEM], [test "$enable_shared_memory" = "yes"])

# Pass the conditionals to automake
AM_CONDITIONAL([LINUX],   [test "$build_linux" = "yes"])
AM_CONDITIONAL([OSX], 	  [test "$build_mac" = "yes"])
AM_CONDITIONAL([FREEBSD], [test "$build_freebsd" = "yes"])
AM_CONDITIONAL([COMPILER_NVIDIA], [test "$xcompiler" = "NVIDIA"])
AM_CONDITIONAL([COMPILER_GNU],    [test "$xcompiler" = "GNU"])
AM_CONDITIONAL([APPLEOCL],    [test "$enable_appleocl" = "yes"])

AS_VAR_APPEND(CFLAGS, " -DCL_USE_DEPRECATED_OPENCL_1_2_APIS")

AC_CHECK_PROGS([DOXYGEN], [doxygen])
if test -z "$DOXYGEN"; then
   AC_MSG_WARN([Doxygen not found - continue without Doxygen support])
fi

AM_CONDITIONAL([HAVE_DOXYGEN], [test -n "$DOXYGEN"])



AC_CONFIG_HEADERS([config.h])
AC_CONFIG_HEADERS([src/lib/include/mcl/_config.h:src/lib/include/mcl/mcl_config.h.in])
AX_PREFIX_CONFIG_H([$srcdir/src/lib/include/mcl/mcl_config.h], [MCL], [src/lib/include/mcl/_config.h])
AC_CONFIG_FILES([Makefile src/Makefile src/lib/Makefile src/sched/Makefile test/Makefile])

AM_COND_IF([HAVE_DOXYGEN], [AC_CONFIG_FILES([doc/Doxyfile])])
AM_COND_IF([HAVE_DOXYGEN], [AC_CONFIG_FILES([doc/Makefile])])

AC_OUTPUT

echo "====================================================="
echo "|                                                   |"
echo "|     Minos Computing Library                       |"
echo "|                                                   |"
echo "| Pacific Northwest National Laboratory             |"
echo "| Contact: R. Gioiosa - roberto.gioiosa@pnnl.gov    |"
echo "|                                                   |"
echo "====================================================="
echo ""
echo ""
echo "Platorm:			$platform"
echo "Compiler vendor:		$xcompiler"
echo ""
echo "Options:   "
echo "          Debug:          $enable_debug"
echo "          Stats:          $enable_stats"
echo "          OpenCL2:        $enable_opencl2"
if test "$build_mac" = "yes" ; then
echo "          Apple OCL:      $enable_appleocl"
fi
echo "          SHARED Mem:          $enable_shared_memory"
echo "          POCL Ext:       $enable_pocl_extensions"
echo ""
echo "Compilers: "
echo "		 C:		$CC"
echo "		 OpenCL:	$found_opencl_h"
echo "		 CPPFLAGS:	$CPPFLAGS"
echo "		 CFLAGS:	$CFLAGS"
echo "		 LDFLAGS:	$LDFLAGS"
echo ""
echo ""
