
ACLOCAL_AMFLAGS = -I m4
bin_PROGRAMS = hdmi_in_test
pkgconfigdir = $(libdir)/pkgconfig

hdmi_in_test_SOURCES = src/hdmi_in_event_test.c
hdmi_in_test_CFLAGS  = $(CFLAGS) -Wno-sign-compare -Werror
hdmi_in_test_LDADD = -llog -lpthread

