ifeq ($(call my-dir),$(call project-path-for,qcom-audio))

AUDIO_HW_ROOT := $(call my-dir)

include $(AUDIO_HW_ROOT)/$(TARGET_BOARD_PLATFORM)/Android.mk

ifneq ($(filter msm8660,$(TARGET_BOARD_PLATFORM)),)
    include $(AUDIO_HW_ROOT)/mm-audio/Android.mk
endif

endif
