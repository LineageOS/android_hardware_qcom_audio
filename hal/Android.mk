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

ifneq ($(filter msm8960,$(TARGET_BOARD_PLATFORM)),)
  # A-family platform uses msm8960 code base
  AUDIO_PLATFORM = msm8960
endif

LOCAL_SRC_FILES := \
	audio_hw.c \
	voice.c \
	platform_info.c \
	$(AUDIO_PLATFORM)/platform.c

LOCAL_SRC_FILES += audio_extn/audio_extn.c

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

ifeq ($(strip $(AUDIO_USE_LL_AS_PRIMARY_OUTPUT)),true)
    LOCAL_CFLAGS += -DUSE_LL_AS_PRIMARY_OUTPUT
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_PCM_OFFLOAD)),true)
    LOCAL_CFLAGS += -DPCM_OFFLOAD_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_ANC_HEADSET)),true)
    LOCAL_CFLAGS += -DANC_HEADSET_ENABLED
endif

ifneq ($(strip $(AUDIO_FEATURE_ENABLED_FLUENCE)),false)
    LOCAL_CFLAGS += -DFLUENCE_ENABLED
endif

ifneq ($(strip $(AUDIO_FEATURE_ENABLED_PROXY_DEVICE)),false)
    LOCAL_CFLAGS += -DAFE_PROXY_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_FM_POWER_OPT)),true)
    LOCAL_CFLAGS += -DFM_POWER_OPT
    LOCAL_SRC_FILES += audio_extn/fm.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_USBAUDIO)),true)
    LOCAL_CFLAGS += -DUSB_HEADSET_ENABLED
    LOCAL_SRC_FILES += audio_extn/usb.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_HFP)),true)
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
ifneq ($(strip $(AUDIO_FEATURE_ENABLED_INCALL_MUSIC)),false)
    LOCAL_CFLAGS += -DINCALL_MUSIC_ENABLED
endif
endif

ifneq ($(strip $(AUDIO_FEATURE_ENABLED_COMPRESS_VOIP)),false)
    LOCAL_CFLAGS += -DCOMPRESS_VOIP_ENABLED
    LOCAL_SRC_FILES += voice_extn/compress_voip.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_SPKR_PROTECTION)),true)
    LOCAL_CFLAGS += -DSPKR_PROT_ENABLED
    LOCAL_SRC_FILES += audio_extn/spkr_protection.c
endif

ifdef MULTIPLE_HW_VARIANTS_ENABLED
  LOCAL_CFLAGS += -DHW_VARIANTS_ENABLED
  LOCAL_SRC_FILES += $(AUDIO_PLATFORM)/hw_info.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_COMPRESS_CAPTURE)),true)
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

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_LOW_LATENCY_CAPTURE)),true)
    LOCAL_CFLAGS += -DLOW_LATENCY_CAPTURE_USE_CASE=1
endif

ifeq ($(strip $(BOARD_HAVE_NEW_QCOM_CSDCLIENT)),true)
    LOCAL_CFLAGS += -DNEW_CSDCLIENT
endif

ifeq ($(strip $(BOARD_HAVE_HTC_CSDCLIENT)),true)
    LOCAL_CFLAGS += -DHTC_CSDCLIENT
endif

ifeq ($(strip $(BOARD_HAVE_SAMSUNG_CSDCLIENT)),true)
    LOCAL_CFLAGS += -DSAMSUNG_CSDCLIENT
endif

ifeq ($(strip $(BOARD_USES_MOTOROLA_EMU_AUDIO)),true)
    LOCAL_CFLAGS += -DDOCK_SUPPORT -DMOTOROLA_EMU_AUDIO
endif

ifeq ($(strip $(BOARD_HAVE_SAMSUNG_AUDIO)),true)
    LOCAL_CFLAGS += -DDOCK_SUPPORT
endif

LOCAL_SHARED_LIBRARIES := \
	liblog \
	libcutils \
	libhardware \
	libtinyalsa \
	libtinycompress \
	libaudioroute \
	libdl \
	libexpat

LOCAL_C_INCLUDES += \
	external/tinyalsa/include \
	external/tinycompress/include \
	external/expat/lib \
	hardware/libhardware/include \
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

ifeq ($(strip $(AUDIO_FEATURE_PCM_IOCTL_ENABLED)),true)
    LOCAL_CFLAGS += -DPCM_IOCTL_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_HWDEP_CAL)),true)
    LOCAL_CFLAGS += -DHWDEP_CAL_ENABLED
endif

LOCAL_COPY_HEADERS_TO   := mm-audio
LOCAL_COPY_HEADERS      := audio_extn/audio_defs.h

LOCAL_MODULE := audio.primary.$(TARGET_BOARD_PLATFORM)

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

endif
