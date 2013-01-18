ifneq ($(filter msm8960,$(TARGET_BOARD_PLATFORM)),)

LOCAL_PATH := $(call my-dir)

ifeq ($(BOARD_USES_LEGACY_ALSA_AUDIO),true)
include $(LOCAL_PATH)/legacy/Android.mk
else
include $(LOCAL_PATH)/hal/Android.mk
endif

endif
