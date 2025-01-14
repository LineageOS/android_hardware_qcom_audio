ifeq ($(call my-dir),$(call project-path-for,qcom-audio))

MY_LOCAL_PATH := $(call my-dir)

include $(MY_LOCAL_PATH)/hal/Android.mk
include $(MY_LOCAL_PATH)/hal/audio_extn/Android.mk

endif
