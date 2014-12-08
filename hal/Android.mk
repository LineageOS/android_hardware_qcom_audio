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
	platform_info.c \
	$(AUDIO_PLATFORM)/platform.c

LOCAL_SRC_FILES += audio_extn/audio_extn.c
ifneq ($(filter msm8960,$(AUDIO_PLATFORM)),true)
    LOCAL_SRC_FILES += edid.c
endif
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_ANC_HEADSET)),true)
    LOCAL_CFLAGS += -DANC_HEADSET_ENABLED
endif

ifneq ($(strip $(AUDIO_FEATURE_ENABLED_FLUENCE)),false)
    LOCAL_CFLAGS += -DFLUENCE_ENABLED
endif

ifneq ($(strip $(AUDIO_FEATURE_ENABLED_PROXY_DEVICE)),false)
    LOCAL_CFLAGS += -DAFE_PROXY_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_FM)),true)
    LOCAL_CFLAGS += -DFM_ENABLED
    LOCAL_SRC_FILES += audio_extn/fm.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_USBAUDIO)),true)
    LOCAL_CFLAGS += -DUSB_HEADSET_ENABLED
    LOCAL_SRC_FILES += audio_extn/usb.c
endif

ifneq ($(strip $(AUDIO_FEATURE_ENABLED_HFP)),false)
    LOCAL_CFLAGS += -DHFP_ENABLED
    LOCAL_SRC_FILES += audio_extn/hfp.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_CUSTOMSTEREO)),true)
    LOCAL_CFLAGS += -DCUSTOM_STEREO_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_SSR)),true)
    LOCAL_CFLAGS += -DSSR_ENABLED
    LOCAL_SRC_FILES += audio_extn/ssr.c
    LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/surround_sound/
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_MULTI_VOICE_SESSIONS)),true)
    LOCAL_CFLAGS += -DMULTI_VOICE_SESSION_ENABLED
    LOCAL_SRC_FILES += voice_extn/voice_extn.c
    LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
    LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
ifneq ($(strip $(AUDIO_FEATURE_ENABLED_INCALL_MUSIC)),false)
    LOCAL_CFLAGS += -DINCALL_MUSIC_ENABLED
endif
endif

ifneq ($(strip $(AUDIO_FEATURE_ENABLED_COMPRESS_VOIP)),false)
    LOCAL_CFLAGS += -DCOMPRESS_VOIP_ENABLED
    LOCAL_SRC_FILES += voice_extn/compress_voip.c
endif

ifneq ($(strip $(AUDIO_FEATURE_ENABLED_EXTN_FORMATS)),false)
LOCAL_CFLAGS += -DFORMATS_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_SPKR_PROTECTION)),true)
    LOCAL_CFLAGS += -DSPKR_PROT_ENABLED
    LOCAL_SRC_FILES += audio_extn/spkr_protection.c
endif

ifdef MULTIPLE_HW_VARIANTS_ENABLED
  LOCAL_CFLAGS += -DHW_VARIANTS_ENABLED
  LOCAL_SRC_FILES += $(AUDIO_PLATFORM)/hw_info.c
endif

ifneq ($(strip $(AUDIO_FEATURE_ENABLED_COMPRESS_CAPTURE)),false)
    LOCAL_CFLAGS += -DCOMPRESS_CAPTURE_ENABLED
    LOCAL_SRC_FILES += audio_extn/compress_capture.c
endif

ifeq ($(strip $(DOLBY_DDP)),true)
    LOCAL_CFLAGS += -DDS1_DOLBY_DDP_ENABLED
    LOCAL_SRC_FILES += audio_extn/dolby.c
endif

ifeq ($(strip $(DOLBY_DAP)),true)
    LOCAL_CFLAGS += -DDS1_DOLBY_DAP_ENABLED
ifneq ($(strip $(DOLBY_DDP)),true)
    LOCAL_SRC_FILES += audio_extn/dolby.c
endif
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DS2_DOLBY_DAP)),true)
    LOCAL_CFLAGS += -DDS2_DOLBY_DAP_ENABLED
ifeq ($(strip $(AUDIO_FEATURE_DISABLED_DS1_DOLBY_DDP)),true)
    ifeq ($(strip $(AUDIO_FEATURE_DISABLED_DS1_DOLBY_DAP)),true)
        LOCAL_SRC_FILES += audio_extn/dolby.c
    endif
endif
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_HDMI_PASSTHROUGH)),true)
    LOCAL_CFLAGS += -DHDMI_PASSTHROUGH_ENABLED
endif

LOCAL_SHARED_LIBRARIES := \
	liblog \
	libcutils \
	libtinyalsa \
	libtinycompress \
	libaudioroute \
	libdl \
	libexpat

LOCAL_C_INCLUDES += \
	external/tinyalsa/include \
	external/tinycompress/include \
	external/expat/lib \
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

ifeq ($(strip $(BOARD_SUPPORTS_SOUND_TRIGGER)),true)
    LOCAL_CFLAGS += -DSOUND_TRIGGER_ENABLED
    LOCAL_CFLAGS += -DSOUND_TRIGGER_PLATFORM_NAME=$(TARGET_BOARD_PLATFORM)
    LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/sound_trigger
    LOCAL_SRC_FILES += audio_extn/soundtrigger.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_AUXPCM_BT)),true)
    LOCAL_CFLAGS += -DAUXPCM_BT_ENABLED
endif

ifeq ($(BOARD_USES_ES705),true)
	LOCAL_CFLAGS += -DUSE_ES705
endif

LOCAL_COPY_HEADERS_TO   := mm-audio
LOCAL_COPY_HEADERS      := audio_extn/audio_defs.h

LOCAL_MODULE := audio.primary.$(TARGET_BOARD_PLATFORM)

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

endif
