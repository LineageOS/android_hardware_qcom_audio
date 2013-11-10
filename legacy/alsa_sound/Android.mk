# hardware/libaudio-alsa/Android.mk
#
# Copyright 2008 Wind River Systems
#

ifeq ($(strip $(BOARD_USES_ALSA_AUDIO)),true)

LOCAL_PATH := $(call my-dir)

common_cflags := -D_POSIX_SOURCE
ifneq ($(strip $(QCOM_ACDB_ENABLED)),false)
    common_cflags += -DQCOM_ACDB_ENABLED
endif
ifneq ($(strip $(QCOM_ANC_HEADSET_ENABLED)),false)
    common_cflags += -DQCOM_ANC_HEADSET_ENABLED
endif
ifeq ($(strip $(QCOM_MULTI_VOICE_SESSION_ENABLED)),true)
    common_cflags += -DQCOM_MULTI_VOICE_SESSION_ENABLED
endif
ifneq ($(strip $(QCOM_AUDIO_FORMAT_ENABLED)),false)
    common_cflags += -DQCOM_AUDIO_FORMAT_ENABLED
endif
ifneq ($(strip $(QCOM_CSDCLIENT_ENABLED)),false)
    common_cflags += -DQCOM_CSDCLIENT_ENABLED
endif
ifeq ($(strip $(QCOM_FM_ENABLED)),true)
    common_cflags += -DQCOM_FM_ENABLED
endif
ifneq ($(strip $(QCOM_PROXY_DEVICE_ENABLED)),false)
    common_cflags += -DQCOM_PROXY_DEVICE_ENABLED
endif
ifneq ($(strip $(QCOM_OUTPUT_FLAGS_ENABLED)),false)
    common_cflags += -DQCOM_OUTPUT_FLAGS_ENABLED
endif
ifeq ($(strip $(QCOM_SSR_ENABLED)),true)
    common_cflags += -DQCOM_SSR_ENABLED
endif
ifneq ($(strip $(QCOM_USBAUDIO_ENABLED)),false)
    common_cflags += -DQCOM_USBAUDIO_ENABLED
endif
ifneq ($(strip $(QCOM_ADSP_SSR_ENABLED)),false)
    common_cflags += -DQCOM_ADSP_SSR_ENABLED
endif
ifneq ($(strip $(QCOM_FLUENCE_ENABLED)),false)
    common_cflags += -DQCOM_FLUENCE_ENABLED
endif
ifneq ($(strip $(QCOM_TUNNEL_LPA_ENABLED)),false)
    common_cflags += -DQCOM_TUNNEL_LPA_ENABLED
endif

ifeq ($(call is-board-platform,msm8974),true)
    common_cflags += -DTARGET_8974
endif

ifneq ($(ALSA_DEFAULT_SAMPLE_RATE),)
    common_cflags += -DALSA_DEFAULT_SAMPLE_RATE=$(ALSA_DEFAULT_SAMPLE_RATE)
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_AUXPCM_BT)),true)
   LOCAL_CFLAGS += -DAUXPCM_BT_ENABLED
endif

#Do not use Dual MIC scenario in call feature
#Dual MIC solution(Fluence) feature in Built-in MIC used scenarioes.
# 1. Handset
# 2. 3-Pole Headphones
ifeq ($(strip $(BOARD_USES_FLUENCE_INCALL)),true)
    common_cflags += -DUSES_FLUENCE_INCALL
endif

ifeq ($(strip $(BOARD_USES_FLUENCE_FOR_VOIP)),true)
    common_cflags += -DUSES_FLUENCE_FOR_VOIP
endif

#Do not use separate audio Input path feature
#Separate audio input path can be set using input source of audio parameter
# 1. Voice Recognition
# 2. Camcording
# 3. etc.
ifeq ($(strip $(BOARD_USES_SEPERATED_AUDIO_INPUT)),true)
    common_cflags += -DSEPERATED_AUDIO_INPUT
endif

ifeq ($(strip $(BOARD_USES_SEPERATED_VOICE_SPEAKER)),true)
    common_cflags += -DSEPERATED_VOICE_SPEAKER
endif

ifeq ($(strip $(BOARD_USES_SEPERATED_VOICE_SPEAKER_MIC)),true)
    common_cflags += -DSEPERATED_VOICE_SPEAKER_MIC
endif

ifeq ($(strip $(BOARD_USES_SEPERATED_HEADSET_MIC)),true)
    common_cflags += -DSEPERATED_HEADSET_MIC
endif

ifeq ($(strip $(BOARD_USES_SEPERATED_VOIP)),true)
    common_cflags += -DSEPERATED_VOIP
endif

ifeq ($(BOARD_HAVE_AUDIENCE_A2220),true)
    common_cflags += -DUSE_A2220
endif

ifeq ($(BOARD_HAVE_SAMSUNG_AUDIO),true)
    common_cflags += -DSAMSUNG_AUDIO
endif

ifeq ($(BOARD_HAVE_NEW_QCOM_CSDCLIENT),true)
    common_cflags += -DNEW_CSDCLIENT
endif

ifeq ($(BOARD_HAVE_CSD_FAST_CALL_SWITCH),true)
    common_cflags += -DCSD_FAST_CALL_SWITCH
endif

ifeq ($(BOARD_HAVE_AUDIENCE_ES325_2MIC),true)
    common_cflags += -DUSE_ES325_2MIC
endif

ifeq ($(BOARD_HAVE_SAMSUNG_CSDCLIENT),true)
    common_cflags += -DSAMSUNG_CSDCLIENT
endif

ifeq ($(BOARD_HAVE_HTC_CSDCLIENT),true)
    common_cflags += -DHTC_CSDCLIENT
endif

ifneq ($(TARGET_USES_QCOM_COMPRESSED_AUDIO),false)
    common_cflags += -DQCOM_COMPRESSED_AUDIO_ENABLED
endif

ifeq ($(BOARD_USES_MOTOROLA_EMU_AUDIO),true)
    common_cflags += -DMOTOROLA_EMU_AUDIO
endif

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

LOCAL_CFLAGS += $(common_cflags)

LOCAL_SRC_FILES := \
  AudioHardwareALSA.cpp         \
  AudioStreamOutALSA.cpp        \
  AudioStreamInALSA.cpp         \
  ALSAStreamOps.cpp             \
  audio_hw_hal.cpp              \
  AudioUsbALSA.cpp              \
  AudioUtil.cpp                 \
  ALSADevice.cpp

ifneq ($(strip $(QCOM_TUNNEL_LPA_ENABLED)),false)
    LOCAL_SRC_FILES += AudioSessionOut.cpp
endif

LOCAL_STATIC_LIBRARIES := \
    libmedia_helper \
    libaudiohw_legacy \
    libaudiopolicy_legacy \

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libutils \
    libmedia \
    libhardware \
    libc        \
    libpower    \
    libalsa-intf \
    libaudioutils

ifeq ($(TARGET_SIMULATOR),true)
 LOCAL_LDLIBS += -ldl
else
 LOCAL_SHARED_LIBRARIES += libdl
endif

ifneq ($(BOARD_AUDIO_AMPLIFIER),)
LOCAL_CFLAGS += -DUSES_AUDIO_AMPLIFIER
LOCAL_SHARED_LIBRARIES += libaudioamp
LOCAL_C_INCLUDES += $(BOARD_AUDIO_AMPLIFIER)
endif

LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/audio-alsa
LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/libalsa-intf
LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/surround_sound/
LOCAL_C_INCLUDES += hardware/libhardware/include
LOCAL_C_INCLUDES += hardware/libhardware_legacy/include
LOCAL_C_INCLUDES += frameworks/base/include
LOCAL_C_INCLUDES += system/core/include
LOCAL_C_INCLUDES += system/media/audio_utils/include

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

ifeq ($(TARGET_BOARD_PLATFORM),msm8974)
  LOCAL_MODULE := audio.primary.msm8974
endif

ifeq ($(TARGET_BOARD_PLATFORM),msm8960)
  LOCAL_MODULE := audio.primary.msm8960
endif

ifeq ($(TARGET_BOARD_PLATFORM),msm8610)
  LOCAL_MODULE := audio.primary.msm8610
endif

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

ifeq ($(USE_LEGACY_AUDIO_POLICY), 1)
include $(CLEAR_VARS)

LOCAL_CFLAGS += $(common_cflags)

LOCAL_SRC_FILES := \
    audio_policy_hal.cpp \
    AudioPolicyManagerALSA.cpp

ifeq ($(call is-board-platform,msm8974),true)
  LOCAL_MODULE := audio_policy.msm8974
endif

ifeq ($(TARGET_BOARD_PLATFORM),msm8960)
  LOCAL_MODULE := audio_policy.msm8960
endif

ifeq ($(TARGET_BOARD_PLATFORM),msm8610)
  LOCAL_MODULE := audio_policy.msm8610
endif

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE_TAGS := optional

LOCAL_STATIC_LIBRARIES := \
    libmedia_helper \
    libaudiopolicy_legacy

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libutils \

LOCAL_C_INCLUDES += hardware/libhardware_legacy/audio

include $(BUILD_SHARED_LIBRARY)
endif

endif
