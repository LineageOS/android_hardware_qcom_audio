LOCAL_PATH := $(call my-dir)

# audio_hal_playback_test
# ==============================================================================
include $(CLEAR_VARS)
LOCAL_SRC_FILES := qahw_playback_test.c
LOCAL_MODULE := hal_play_test

hal-play-inc     = $(TARGET_OUT_HEADERS)/mm-audio/qahw_api/inc

LOCAL_CFLAGS += -Wall -Werror -Wno-sign-compare

LOCAL_SHARED_LIBRARIES := \
    libaudioutils\
    libqahw \
    libutils

LOCAL_32_BIT_ONLY := true

LOCAL_C_INCLUDES += $(hal-play-inc)

include $(BUILD_EXECUTABLE)

# audio_hal_multi_record_test
# ==============================================================================
include $(CLEAR_VARS)
LOCAL_SRC_FILES := qahw_multi_record_test.c
LOCAL_MODULE := hal_rec_test
LOCAL_CFLAGS += -Wall -Werror -Wno-sign-compare
LOCAL_SHARED_LIBRARIES := \
    libaudioutils \
    libqahw \
    libutils

LOCAL_32_BIT_ONLY := true

hal-rec-inc     = $(TARGET_OUT_HEADERS)/mm-audio/qahw_api/inc

LOCAL_C_INCLUDES += $(hal-rec-inc)
include $(BUILD_EXECUTABLE)
