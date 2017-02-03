AM_CFLAGS = -I $(top_srcdir)/qahw_api/inc

h_sources = inc/qahw_api.h \
            inc/qahw_defs.h \
            inc/qahw_effect_api.h \
            inc/qahw_effect_audiosphere.h \
            inc/qahw_effect_bassboost.h \
            inc/qahw_effect_environmentalreverb.h \
            inc/qahw_effect_equalizer.h \
            inc/qahw_effect_presetreverb.h \
            inc/qahw_effect_virtualizer.h \
            inc/qahw_effect_visualizer.h

library_include_HEADERS = $(h_sources)
library_includedir = $(includedir)/mm-audio/qahw_api/inc

lib_LTLIBRARIES = libqahw.la
libqahw_la_SOURCES = src/qahw.c \
                     src/qahw_effect.c
libqahw_la_CFLAGS = $(AM_CFLAGS) -include stddef.h
libqahw_la_CFLAGS += -D__unused=__attribute__\(\(__unused__\)\)
libqahw_la_CFLAGS += -Wno-sign-compare -Werror
libqahw_la_LIBADD = -llog -lcutils -lhardware
libqahw_la_LDFLAGS = -module -shared -avoid-version
