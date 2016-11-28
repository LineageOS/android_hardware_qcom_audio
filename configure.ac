#                                               -*- Autoconf -*-
# configure.ac -- Autoconf script for halinterface
#

# Process this file with autoconf to produce a configure script.

# Requires autoconf tool later than 2.61
AC_PREREQ([2.69])
# Initialize the hal-interface package version 1.0.0
AC_INIT(halinterface,1.0.0)
# Does not strictly follow GNU Coding standards
AM_INIT_AUTOMAKE([foreign])
# Disables auto rebuilding of configure, Makefile.ins
#AM_MAINTAINER_MODE
# defines some macros variable to be included by source
AC_CONFIG_HEADERS([config.h])
# defines some macros variable to be included by source
AC_CONFIG_MACRO_DIR([m4])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_CXX
AC_PROG_LIBTOOL
AC_PROG_AWK
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
PKG_PROG_PKG_CONFIG

PKG_CHECK_MODULES([AUDIOROUTE], [audio_route])
AC_SUBST([AUDIOROUTE_CFLAGS])

AC_ARG_WITH(sanitized-headers,
        AS_HELP_STRING([--with-sanitized-headers=DIR],
                [Specify the location of the sanitized Linux headers]),
        [CPPFLAGS="$CPPFLAGS -idirafter $withval"])


AC_ARG_WITH([glib],
      AC_HELP_STRING([--with-glib],
         [enable glib, Build against glib. Use this when building for HLOS systems which use glib]))

if (test "x${with_glib}" = "xyes"); then
        PKG_CHECK_MODULES(GTHREAD, gthread-2.0 >= 2.16, dummy=yes,
                                AC_MSG_ERROR(GThread >= 2.16 is required))
        PKG_CHECK_MODULES(GLIB, glib-2.0 >= 2.16, dummy=yes,
                                AC_MSG_ERROR(GLib >= 2.16 is required))
        GLIB_CFLAGS="$GLIB_CFLAGS $GTHREAD_CFLAGS"
        GLIB_LIBS="$GLIB_LIBS $GTHREAD_LIBS"

        AC_SUBST(GLIB_CFLAGS)
        AC_SUBST(GLIB_LIBS)
fi

AM_CONDITIONAL(USE_GLIB, test "x${with_glib}" = "xyes")

AM_CONDITIONAL([HDMI_EDID],    [test x$AUDIO_FEATURE_ENABLED_HDMI_EDID = xtrue])
AM_CONDITIONAL([FM_POWER_OPT], [test x$AUDIO_FEATURE_ENABLED_FM_POWER_OPT = xtrue])
AM_CONDITIONAL([USBAUDIO], [test x$AUDIO_FEATURE_ENABLED_USBAUDIO = xtrue])
AM_CONDITIONAL([HFP], [test x$AUDIO_FEATURE_ENABLED_HFP = xtrue])
AM_CONDITIONAL([SSR], [test x$AUDIO_FEATURE_ENABLED_SSR = xtrue])
AM_CONDITIONAL([MULTI_VOICE_SESSIONS], [test x$AUDIO_FEATURE_ENABLED_MULTI_VOICE_SESSIONS = xtrue])
AM_CONDITIONAL([COMPRESS_VOIP], [test x$AUDIO_FEATURE_ENABLED_COMPRESS_VOIP = xtrue])
AM_CONDITIONAL([SPKR_PROTECTION], [test x$AUDIO_FEATURE_ENABLED_SPKR_PROTECTION = xtrue])
AM_CONDITIONAL([HW_VARIANTS], [test x$MULTIPLE_HW_VARIANTS_ENABLED = xtrue])
AM_CONDITIONAL([COMPRESS_CAPTURE], [test x$AUDIO_FEATURE_ENABLED_COMPRESS_CAPTURE = xtrue])
AM_CONDITIONAL([DTS_EAGLE], [test x$AUDIO_FEATURE_ENABLED_DTS_EAGLE = xtrue])
AM_CONDITIONAL([DOLBY_DDP], [test x$DOLBY_DDP = xtrue])
AM_CONDITIONAL([DS1_DOLBY_DAP], [test x$DS1_DOLBY_DAP = xtrue])
AM_CONDITIONAL([DEV_ARBI], [test x$AUDIO_FEATURE_ENABLED_DEV_ARBI = xtrue])
AM_CONDITIONAL([SOURCE_TRACKING], [test x$AUDIO_FEATURE_ENABLED_SOURCE_TRACKING = xtrue])
AM_CONDITIONAL([LISTEN], [test x$AUDIO_FEATURE_ENABLED_LISTEN = xtrue])
AM_CONDITIONAL([SOUND_TRIGGER], [test x$BOARD_SUPPORTS_SOUND_TRIGGER = xtrue])
AM_CONDITIONAL([PM_SUPPORT], [test x$AUDIO_FEATURE_ENABLED_PM_SUPPORT = xtrue])
AM_CONDITIONAL([FLAC_SUPPORT], [test x$AUDIO_FEATURE_ENABLED_EXTN_FLAC_DECODER = xtrue])
AM_CONDITIONAL([ALAC_SUPPORT], [test x$AUDIO_FEATURE_ENABLED_EXTN_ALAC_DECODER = xtrue])
AM_CONDITIONAL([VORBIS_SUPPORT], [test x$AUDIO_FEATURE_ENABLED_EXTN_VORBIS_DECODER = xtrue])
AM_CONDITIONAL([WMA_SUPPORT], [test x$AUDIO_FEATURE_ENABLED_EXTN_WMA_DECODER = xtrue])

AC_CONFIG_FILES([ \
        Makefile \
        hal/Makefile \
        qahw_api/Makefile \
        qahw_api/test/Makefile
        ])

AC_OUTPUT
