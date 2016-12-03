bin_PROGRAMS = hal_play_test

PLAY_CPPFLAGS = -Wno-sign-compare
PLAY_INCLUDES = -I $(top_srcdir)/qahw_api/inc

hal_play_test_SOURCES = qahw_playback_test.c
hal_play_test_CPPFLAGS = $(PLAY_CPPFLAGS) $(PLAY_INCLUDES)
hal_play_test_LDADD = -lutils ../libqahw.la


bin_PROGRAMS += hal_rec_test

REC_CPPFLAGS = $(PLAY_CPPFLAGS) -Dnullptr=NULL
REC_INCLUDES = $(PLAY_INCLUDES)

hal_rec_test_SOURCES = qahw_multi_record_test.c
hal_rec_test_CPPFLAGS = -Dstrlcat=g_strlcat $(GLIB_CFLAGS) -include glib.h
hal_rec_test_CPPFLAGS += $(REC_CPPFLAGS) $(REC_INCLUDES)
hal_rec_test_LDADD = -lutils ../libqahw.la $(GLIB_LIBS)
