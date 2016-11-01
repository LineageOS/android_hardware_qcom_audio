ifeq ($(strip $(BOARD_SUPPORTS_QAHW)),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

libqahw-inc := $(LOCAL_PATH)/inc

LOCAL_MODULE := libqahw
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_OWNER := qti
LOCAL_C_INCLUDES   := $(libqahw-inc)

LOCAL_SRC_FILES := \
    src/qahw.c

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libcutils \
    libhardware

LOCAL_COPY_HEADERS_TO   := mm-audio/qahw_api/inc
LOCAL_COPY_HEADERS      := inc/qahw_api.h
LOCAL_COPY_HEADERS      += inc/qahw_defs.h

LOCAL_PRELINK_MODULE    := false

include $(BUILD_SHARED_LIBRARY)

#test app compilation
include $(LOCAL_PATH)/test/Android.mk
endif
