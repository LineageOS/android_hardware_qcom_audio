LOCAL_PATH := $(call my-dir)

# stt_meta_extract
# ==============================================================================
include $(CLEAR_VARS)
LOCAL_SRC_FILES := stt_meta_extract.c
LOCAL_MODULE := stt_meta_extract
LOCAL_CFLAGS += -Wall -Werror -Wno-sign-compare
LOCAL_SHARED_LIBRARIES := \
    libtinyalsa \
    libcutils \
    liblog

LOCAL_C_INCLUDES += \
    external/tinyalsa/include

LOCAL_32_BIT_ONLY := true

LOCAL_VENDOR_MODULE := true

include $(BUILD_EXECUTABLE)
