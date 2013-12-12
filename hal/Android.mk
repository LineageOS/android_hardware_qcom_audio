ifeq ($(strip $(BOARD_USES_ALSA_AUDIO)),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

AUDIO_PLATFORM := $(TARGET_BOARD_PLATFORM)

ifneq ($(filter msm8974 msm8226 msm8610 apq8084,$(TARGET_BOARD_PLATFORM)),)
  # B-family platform uses msm8974 code base
  AUDIO_PLATFORM = msm8974
  MULTIPLE_HW_VARIANTS_ENABLED := true
ifneq ($(filter msm8610,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSM8610
endif
ifneq ($(filter msm8226,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSM8x26
endif
ifneq ($(filter apq8084,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_APQ8084
endif
endif

LOCAL_SRC_FILES := \
	audio_hw.c \
	voice.c \
	$(AUDIO_PLATFORM)/platform.c

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

ifneq ($(strip $(AUDIO_FEATURE_DISABLED_USBAUDIO)),true)
    LOCAL_CFLAGS += -DUSB_HEADSET_ENABLED
    LOCAL_SRC_FILES += audio_extn/usb.c
endif
ifneq ($(strip $(AUDIO_FEATURE_DISABLED_HFP)),true)
    LOCAL_CFLAGS += -DHFP_ENABLED
    LOCAL_SRC_FILES += audio_extn/hfp.c
endif

ifneq ($(strip $(AUDIO_FEATURE_DISABLED_SSR)),true)
    LOCAL_CFLAGS += -DSSR_ENABLED
    LOCAL_SRC_FILES += audio_extn/ssr.c
    LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/surround_sound/
endif

ifneq ($(strip $(AUDIO_FEATURE_DISABLED_MULTI_VOICE_SESSIONS)),true)
    LOCAL_CFLAGS += -DMULTI_VOICE_SESSION_ENABLED
    LOCAL_SRC_FILES += voice_extn/voice_extn.c
    LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
    LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

ifneq ($(strip $(AUDIO_FEATURE_DISABLED_INCALL_MUSIC)),true)
    LOCAL_CFLAGS += -DINCALL_MUSIC_ENABLED
endif

ifneq ($(strip $(AUDIO_FEATURE_DISABLED_COMPRESS_VOIP)),true)
    LOCAL_CFLAGS += -DCOMPRESS_VOIP_ENABLED
    LOCAL_SRC_FILES += voice_extn/compress_voip.c
endif
endif

ifneq ($(strip, $(AUDIO_FEATURE_DISABLED_SPKR_PROTECTION)),true)
ifneq ($(filter msm8974,$(TARGET_BOARD_PLATFORM)),)
    LOCAL_CFLAGS += -DSPKR_PROT_ENABLED
    LOCAL_SRC_FILES += audio_extn/spkr_protection.c
    LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
    LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
endif
endif

ifdef MULTIPLE_HW_VARIANTS_ENABLED
  LOCAL_CFLAGS += -DHW_VARIANTS_ENABLED
  LOCAL_SRC_FILES += $(AUDIO_PLATFORM)/hw_info.c
endif

ifneq ($(strip $(AUDIO_FEATURE_DISABLED_COMPRESS_CAPTURE)),true)
    LOCAL_CFLAGS += -DCOMPRESS_CAPTURE_ENABLED
    LOCAL_SRC_FILES += audio_extn/compress_capture.c
endif

ifneq ($(strip $(AUDIO_FEATURE_DISABLED_DS1_DOLBY_DDP)),true)
    LOCAL_CFLAGS += -DDS1_DOLBY_DDP_ENABLED
    LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
    LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
endif

LOCAL_SHARED_LIBRARIES := \
	liblog \
	libcutils \
	libtinyalsa \
	libtinycompress \
	libaudioroute \
	libdl

LOCAL_C_INCLUDES += \
	external/tinyalsa/include \
	external/tinycompress/include \
	$(call include-path-for, audio-route) \
	$(call include-path-for, audio-effects) \
	$(LOCAL_PATH)/$(AUDIO_PLATFORM) \
	$(LOCAL_PATH)/audio_extn \
	$(LOCAL_PATH)/voice_extn

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_LISTEN)),true)
    LOCAL_CFLAGS += -DAUDIO_LISTEN_ENABLED
    LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/audio-listen
    LOCAL_SRC_FILES += audio_extn/listen.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_AUXPCM_BT)),true)
    LOCAL_CFLAGS += -DAUXPCM_BT_ENABLED
endif

LOCAL_MODULE := audio.primary.$(TARGET_BOARD_PLATFORM)

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

endif
