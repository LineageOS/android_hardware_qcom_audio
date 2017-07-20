LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := hdmi_in_test
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_OWNER := qti

LOCAL_SRC_FILES := \
    src/hdmi_in_event_test.c

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libcutils

include $(BUILD_EXECUTABLE)
