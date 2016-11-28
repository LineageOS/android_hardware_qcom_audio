AM_CFLAGS = -I $(top_srcdir)/qahw_api/inc

h_sources = inc/qahw_api.h \
            inc/qahw_defs.h

library_include_HEADERS = $(h_sources)
library_includedir = $(includedir)/mm-audio/qahw_api/inc

lib_LTLIBRARIES = libqahw.la
libqahw_la_SOURCES = src/qahw.c
libqahw_la_CFLAGS = $(AM_CFLAGS) -include stddef.h
libqahw_la_CFLAGS += -D__unused=__attribute__\(\(__unused__\)\)
libqahw_la_LIBADD = -llog -lcutils -lhardware
libqahw_la_LDFLAGS = -module -shared -avoid-version
