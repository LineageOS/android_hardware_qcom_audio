ifneq ($(filter msm8960 msm8226 msm8x26 msm8610 msm8974 msm8x74 apq8084,$(TARGET_BOARD_PLATFORM)),)

MY_LOCAL_PATH := $(call my-dir)

ifeq ($(BOARD_USES_LEGACY_ALSA_AUDIO),true)
include $(MY_LOCAL_PATH)/legacy/Android.mk
else
include $(MY_LOCAL_PATH)/hal/Android.mk
include $(MY_LOCAL_PATH)/voice_processing/Android.mk
include $(MY_LOCAL_PATH)/mm-audio/Android.mk
include $(MY_LOCAL_PATH)/policy_hal/Android.mk
include $(MY_LOCAL_PATH)/visualizer/Android.mk
include $(MY_LOCAL_PATH)/audiod/Android.mk
endif

endif
