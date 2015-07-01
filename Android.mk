ifeq ($(call my-dir),$(call project-path-for,qcom-audio))

ifneq ($(filter msm8660,$(TARGET_BOARD_PLATFORM)),)

MY_LOCAL_PATH := $(call my-dir)

ifeq ($(BOARD_USES_LEGACY_ALSA_AUDIO),true)
include $(MY_LOCAL_PATH)/legacy/Android.mk
endif

endif

endif
