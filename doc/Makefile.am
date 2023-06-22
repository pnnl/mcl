if HAVE_DOXYGEN
directory = $(top_srcdir)/doc/man/man3

man_MANS = $(directory)

$(directory): doxyfile.stamp

doxyfile.stamp: Doxyfile
	$(DOXYGEN) $^

CLEANFILES = doxyfile.stamp

all-local: doxyfile.stamp
clean-local:
	-rm -rf $(top_builddir)/doc/html
	-rm -rf $(top_builddir)/doc/man

endif