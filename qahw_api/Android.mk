ifeq ($(strip $(BOARD_SUPPORTS_QAHW)),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

libqahw-inc := $(LOCAL_PATH)/inc

LOCAL_MODULE := libqahw
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES   := $(libqahw-inc)

LOCAL_SRC_FILES := \
    src/qahw.c \
    src/qahw_effect.c

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libcutils \
    libhardware \
    libdl

LOCAL_CFLAGS += -Wall -Werror

LOCAL_COPY_HEADERS_TO   := mm-audio/qahw_api/inc
LOCAL_COPY_HEADERS      := inc/qahw_api.h
LOCAL_COPY_HEADERS      += inc/qahw_defs.h
LOCAL_COPY_HEADERS      += inc/qahw_effect_api.h
LOCAL_COPY_HEADERS      += inc/qahw_effect_audiosphere.h
LOCAL_COPY_HEADERS      += inc/qahw_effect_bassboost.h
LOCAL_COPY_HEADERS      += inc/qahw_effect_environmentalreverb.h
LOCAL_COPY_HEADERS      += inc/qahw_effect_equalizer.h
LOCAL_COPY_HEADERS      += inc/qahw_effect_presetreverb.h
LOCAL_COPY_HEADERS      += inc/qahw_effect_virtualizer.h
LOCAL_COPY_HEADERS      += inc/qahw_effect_visualizer.h

LOCAL_PRELINK_MODULE    := false
LOCAL_VENDOR_MODULE     := true

include $(BUILD_SHARED_LIBRARY)

#test app compilation
include $(LOCAL_PATH)/test/Android.mk
endif
