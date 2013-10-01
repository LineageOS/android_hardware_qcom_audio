ifeq ($(strip $(BOARD_USES_ALSA_AUDIO)),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

#MULTI_VOICE_SESSION_ENABLED := true

ifdef MULTI_VOICE_SESSION_ENABLED
LOCAL_CFLAGS += -DMULTI_VOICE_SESSION_ENABLED
endif

AUDIO_PLATFORM := $(TARGET_BOARD_PLATFORM)
ifneq ($(filter msm8974 msm8226 msm8610 apq8084,$(TARGET_BOARD_PLATFORM)),)
  # B-family platform uses msm8974 code base
  AUDIO_PLATFORM = msm8974
ifneq ($(filter msm8610,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSM8610
endif
ifneq ($(filter msm8226,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSM8x26
endif
endif

LOCAL_SRC_FILES := \
	audio_hw.c \
	voice.c \
	$(AUDIO_PLATFORM)/platform.c

ifdef MULTI_VOICE_SESSION_ENABLED
LOCAL_SRC_FILES += voice_extn/voice_extn.c
endif

LOCAL_SRC_FILES += audio_extn/audio_extn.c

ifneq ($(strip $(AUDIO_FEATURE_DISABLED_ANC_HEADSET)),true)
    LOCAL_CFLAGS += -DANC_HEADSET_ENABLED
endif

ifneq ($(strip $(AUDIO_FEATURE_DISABLED_PROXY_DEVICE)),true)
    LOCAL_CFLAGS += -DAFE_PROXY_ENABLED
endif

ifneq ($(strip $(AUDIO_FEATURE_DISABLED_FM)),true)
    LOCAL_CFLAGS += -DFM_ENABLED
    LOCAL_SRC_FILES += audio_extn/fm.c
endif

LOCAL_SHARED_LIBRARIES := \
	liblog \
	libcutils \
	libtinyalsa \
	libaudioroute \
	libdl


LOCAL_C_INCLUDES += \
	external/tinyalsa/include \
	$(call include-path-for, audio-route) \
	$(call include-path-for, audio-effects) \
	$(LOCAL_PATH)/$(AUDIO_PLATFORM) \
	$(LOCAL_PATH)/audio_extn

ifdef MULTI_VOICE_SESSION_ENABLED
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
endif

LOCAL_MODULE := audio.primary.$(TARGET_BOARD_PLATFORM)

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

endif
