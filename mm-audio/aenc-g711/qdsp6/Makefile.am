AM_CFLAGS = -Wall \
            -Wundef \
            -Wstrict-prototypes \
            -Wno-trigraphs \
            -fPIC \
            -g -O3 \
            -DQC_MODIFIED \
            -D_ANDROID_ \
            -D_ENABLE_QC_MSG_LOG_ \
            -DVERBOSE \
            -D_DEBUG \
            -DAUDIOV2 \
            -I inc \
            -I ${WORKSPACE}/hardware/qcom/media/mm-core/inc/

C_SOURCES = src/aenc_svr.c \
            src/omx_g711_aenc.cpp \
            src/omx_log.cpp

lib_LTLIBRARIES = libOmxG711Enc.la
libOmxG711Enc_la_SOURCES = $(C_SOURCES)
libOmxG711Enc_la_CFLAGS = $(AM_CFLAGS) -Dstrlcpy=g_strlcpy $(GLIB_CFLAGS) -include glib.h
libOmxG711Enc_la_CPPFLAGS = $(AM_CFLAGS) -Dstrlcpy=g_strlcpy $(GLIB_CFLAGS) -include glib.h
libOmxG711Enc_la_LIBADD = $(GLIB_LIBS) -lcutils -llog -lstdc++
libOmxG711Enc_la_LDFLAGS = -shared -avoid-version
