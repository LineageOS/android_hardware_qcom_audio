ifeq ($(strip $(BOARD_USES_ALSA_AUDIO)),true)

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := AudioPolicyManager.cpp

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libutils \
    liblog

LOCAL_STATIC_LIBRARIES := \
    libmedia_helper

LOCAL_WHOLE_STATIC_LIBRARIES := \
    libaudiopolicy_legacy

LOCAL_MODULE := audio_policy.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE_TAGS := optional

ifneq ($(strip $(AUDIO_FEATURE_DISABLED_FM)),true)
LOCAL_CFLAGS += -DAUDIO_EXTN_FM_ENABLED
endif
ifneq ($(strip $(AUDIO_FEATURE_DISABLED_PROXY_DEVICE)),true)
LOCAL_CFLAGS += -DAUDIO_EXTN_AFE_PROXY_ENABLED
endif
ifneq ($(strip $(AUDIO_FEATURE_DISABLED_INCALL_MUSIC)),true)
LOCAL_CFLAGS += -DAUDIO_EXTN_INCALL_MUSIC_ENABLED
endif

include $(BUILD_SHARED_LIBRARY)

endif
