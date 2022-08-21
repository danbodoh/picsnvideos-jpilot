lib_LTLIBRARIES = libpicsnvideos.la

libpicsnvideos_la_SOURCES = picsnvideos.c libplugin.h

libpicsnvideos_la_LDFLAGS = -avoid-version
libpicsnvideos_la_LIBADD = @LIBS@ @PILOT_LIBS@
libdir = $(prefix)/lib/jpilot/plugins

AM_CFLAGS = -Wall @PILOT_FLAGS@

local_install: libpicsnvideos.la
    ACLOCAL_AMFLAGS = -I m4
	$(INSTALL) -d -m 755 $(HOME)/.jpilot/plugins
	$(LIBTOOL) --mode=install $(INSTALL) -m 755 libpicsnvideos.la $(HOME)/.jpilot/plugins
