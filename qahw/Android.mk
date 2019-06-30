ifneq ($(AUDIO_USE_STUB_HAL), true)
ifeq ($(strip $(BOARD_SUPPORTS_QAHW)),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

libqahw-inc := $(LOCAL_PATH)/inc

LOCAL_MODULE := libqahwwrapper
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

LOCAL_COPY_HEADERS_TO   := mm-audio/qahw/inc
LOCAL_COPY_HEADERS      := inc/qahw.h
LOCAL_COPY_HEADERS      += inc/qahw_effect_api.h

LOCAL_PRELINK_MODULE    := false
LOCAL_VENDOR_MODULE     := true

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_COPY_HEADERS_TO   := mm-audio/qahw_api/inc
LOCAL_COPY_HEADERS      := inc/qahw_defs.h

include $(BUILD_COPY_HEADERS)
endif
endif
