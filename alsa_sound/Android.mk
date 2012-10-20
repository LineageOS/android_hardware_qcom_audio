# hardware/libaudio-alsa/Android.mk
#
# Copyright 2008 Wind River Systems
#

ifeq ($(BOARD_USES_ALSA_AUDIO),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

# hack for prebuilt libacdbloader
$(shell mkdir -p $(OUT)/obj/SHARED_LIBRARIES/libacdbloader_intermediates)
$(shell touch $(OUT)/obj/SHARED_LIBRARIES/libacdbloader_intermediates/export_includes)

LOCAL_ARM_MODE := arm
LOCAL_CFLAGS := -D_POSIX_SOURCE
LOCAL_CFLAGS += -DQCOM_ACDB_ENABLED
LOCAL_CFLAGS += -DQCOM_PROXY_DEVICE_ENABLED

LOCAL_SRC_FILES := \
  AudioHardwareALSA.cpp 	\
  AudioStreamOutALSA.cpp 	\
  AudioStreamInALSA.cpp 	\
  ALSAStreamOps.cpp		\
  audio_hw_hal.cpp \
  AudioUsbALSA.cpp \
  AudioSessionOut.cpp

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
    libacdbloader \
    libalsa-intf

LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/audio-alsa
LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/audcal
LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/libalsa-intf
LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/surround_sound/
LOCAL_C_INCLUDES += hardware/libhardware/include
LOCAL_C_INCLUDES += hardware/libhardware_legacy/include
LOCAL_C_INCLUDES += frameworks/base/include
LOCAL_C_INCLUDES += system/core/include

ifeq ($(BOARD_USES_QCOM_HARDWARE),true)
  LOCAL_CFLAGS += -DQCOM_ACDB_ENABLED
endif

ifeq ($BOARD_USES_QCOM_USBAUDIO),true)
  LOCAL_CFLAGS += -DQCOM_USBAUDIO_ENABLED
endif

LOCAL_MODULE := audio.primary.msm8960
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_CFLAGS := -D_POSIX_SOURCE

LOCAL_CFLAGS += -DQCOM_ACDB_ENABLED
LOCAL_CFLAGS += -DQCOM_PROXY_DEVICE_ENABLED

ifeq ($(BOARD_HAVE_BLUETOOTH),true)
  LOCAL_CFLAGS += -DWITH_A2DP
endif

LOCAL_SRC_FILES := \
    audio_policy_hal.cpp \
    AudioPolicyManagerALSA.cpp

LOCAL_MODULE := audio_policy.msm8960
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE_TAGS := optional

LOCAL_STATIC_LIBRARIES := \
    libmedia_helper \
    libaudiohw_legacy \
    libaudiopolicy_legacy

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libutils \
    libmedia \
    libaudioparameter

LOCAL_C_INCLUDES += hardware/libhardware_legacy/audio

include $(BUILD_SHARED_LIBRARY)

# Load audio_policy.conf to system/etc/
include $(CLEAR_VARS)
LOCAL_MODULE       := audio_policy.conf
LOCAL_MODULE_TAGS  := optional
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH  := $(TARGET_OUT_ETC)/
LOCAL_SRC_FILES    := audio_policy.conf
include $(BUILD_PREBUILT)

# This is the ALSA module which behaves closely like the original

include $(CLEAR_VARS)

LOCAL_PRELINK_MODULE := false

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw

LOCAL_CFLAGS := -D_POSIX_SOURCE -Wno-multichar

LOCAL_CFLAGS += -DQCOM_ACDB_ENABLED
LOCAL_CFLAGS += -DQCOM_PROXY_DEVICE_ENABLED

ifneq ($(ALSA_DEFAULT_SAMPLE_RATE),)
    LOCAL_CFLAGS += -DALSA_DEFAULT_SAMPLE_RATE=$(ALSA_DEFAULT_SAMPLE_RATE)
endif

ifeq ($BOARD_USES_QCOM_USBAUDIO),true)
  LOCAL_CFLAGS += -DQCOM_USBAUDIO_ENABLED
endif

LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/libalsa-intf

LOCAL_SRC_FILES:= \
    alsa_default.cpp \
    ALSAControl.cpp

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    liblog    \
    libalsa-intf

ifeq ($(BOARD_HAVE_HTC_AUDIO),true)
  LOCAL_CFLAGS += -DHTC_AUDIO
endif

ifeq ($(BOARD_HAVE_SAMSUNG_AUDIO),true)
  LOCAL_CFLAGS += -DSAMSUNG_AUDIO
endif

ifeq ($(BOARD_HAVE_AUDIENCE_A2220),true)
  LOCAL_CFLAGS += -DUSE_A2220
endif

LOCAL_MODULE:= alsa.msm8960
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

endif # BOARD_USES_ALSA_AUDIO
