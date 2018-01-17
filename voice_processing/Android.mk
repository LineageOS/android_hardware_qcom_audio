LOCAL_PATH:= $(call my-dir)

# audio preprocessing wrapper
include $(CLEAR_VARS)

LOCAL_MODULE:= libqcomvoiceprocessing
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_OWNER := qcom
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_RELATIVE_PATH := soundfx

LOCAL_SRC_FILES:= \
    voice_processing.c

LOCAL_CFLAGS += \
    -Wall \
    -Werror \
    -Wno-unused-function \
    -Wno-unused-variable \

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libcutils

LOCAL_SHARED_LIBRARIES += libdl

LOCAL_CFLAGS += -fvisibility=hidden

LOCAL_HEADER_LIBRARIES += \
    libhardware_headers \
    android.hardware.audio.effect.legacy@2.0 \

include $(BUILD_SHARED_LIBRARY)
